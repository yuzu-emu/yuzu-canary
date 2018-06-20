// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <vector>
#include <lz4.h>
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/loader/nso.h"
#include "core/memory.h"

namespace Loader {

struct NsoSegmentHeader {
    u32_le offset;
    u32_le location;
    u32_le size;
    union {
        u32_le alignment;
        u32_le bss_size;
    };
};
static_assert(sizeof(NsoSegmentHeader) == 0x10, "NsoSegmentHeader has incorrect size.");

struct NsoHeader {
    u32_le magic;
    INSERT_PADDING_BYTES(0xc);
    std::array<NsoSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    u32_le bss_size;
    INSERT_PADDING_BYTES(0x1c);
    std::array<u32_le, 3> segments_compressed_size;
};
static_assert(sizeof(NsoHeader) == 0x6c, "NsoHeader has incorrect size.");

struct ModHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(ModHeader) == 0x1c, "ModHeader has incorrect size.");

AppLoader_NSO::AppLoader_NSO(FileUtil::IOFile&& file, std::string filepath)
    : AppLoader(std::move(file)), filepath(std::move(filepath)) {}

FileType AppLoader_NSO::IdentifyType(FileUtil::IOFile& file, const std::string&) {
    u32 magic = 0;
    file.Seek(0, SEEK_SET);
    if (1 != file.ReadArray<u32>(&magic, 1)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') == magic) {
        return FileType::NSO;
    }

    return FileType::Error;
}

static std::vector<u8> DecompressSegment(const std::vector<u8>& compressed_data,
                                         const NsoSegmentHeader& header) {
    std::vector<u8> uncompressed_data;
    uncompressed_data.resize(header.size);
    const int bytes_uncompressed = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed_data.data()),
        reinterpret_cast<char*>(uncompressed_data.data()), compressed_data.size(), header.size);

    ASSERT_MSG(bytes_uncompressed == header.size && bytes_uncompressed == uncompressed_data.size(),
               "{} != {} != {}", bytes_uncompressed, header.size, uncompressed_data.size());

    return uncompressed_data;
}

static std::vector<u8> ReadSegment(FileUtil::IOFile& file, const NsoSegmentHeader& header,
                                   size_t compressed_size) {
    std::vector<u8> compressed_data;
    compressed_data.resize(compressed_size);

    file.Seek(header.offset, SEEK_SET);
    if (compressed_size != file.ReadBytes(compressed_data.data(), compressed_size)) {
        NGLOG_CRITICAL(Loader, "Failed to read {} NSO LZ4 compressed bytes", compressed_size);
        return {};
    }

    return DecompressSegment(compressed_data, header);
}

static constexpr u32 PageAlignSize(u32 size) {
    return (size + Memory::PAGE_MASK) & ~Memory::PAGE_MASK;
}

VAddr AppLoader_NSO::LoadModule(const std::string& name, const std::vector<u8>& file_data,
                                VAddr load_base) {
    if (file_data.size() < sizeof(NsoHeader))
        return {};

    NsoHeader nso_header;
    std::memcpy(&nso_header, file_data.data(), sizeof(NsoHeader));

    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0'))
        return {};

    // Build program image
    Kernel::SharedPtr<Kernel::CodeSet> codeset = Kernel::CodeSet::Create("");
    std::vector<u8> program_image;
    for (int i = 0; i < nso_header.segments.size(); ++i) {
        std::vector<u8> compressed_data(nso_header.segments_compressed_size[i]);
        for (int j = 0; j < nso_header.segments_compressed_size[i]; ++j)
            compressed_data[j] = file_data[nso_header.segments[i].offset + j];
        std::vector<u8> data = DecompressSegment(compressed_data, nso_header.segments[i]);
        program_image.resize(nso_header.segments[i].location);
        program_image.insert(program_image.end(), data.begin(), data.end());
        codeset->segments[i].addr = nso_header.segments[i].location;
        codeset->segments[i].offset = nso_header.segments[i].location;
        codeset->segments[i].size = PageAlignSize(static_cast<u32>(data.size()));
    }

    // MOD header pointer is at .text offset + 4
    u32 module_offset;
    std::memcpy(&module_offset, program_image.data() + 4, sizeof(u32));

    // Read MOD header
    ModHeader mod_header{};
    // Default .bss to size in segment header if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nso_header.segments[2].bss_size)};
    std::memcpy(&mod_header, program_image.data() + module_offset, sizeof(ModHeader));
    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }
    codeset->data.size += bss_size;
    const u32 image_size{PageAlignSize(static_cast<u32>(program_image.size()) + bss_size)};
    program_image.resize(image_size);

    // Load codeset for current process
    codeset->name = name;
    codeset->memory = std::make_shared<std::vector<u8>>(std::move(program_image));
    Core::CurrentProcess()->LoadModule(codeset, load_base);

    return load_base + image_size;
}

VAddr AppLoader_NSO::LoadModule(const std::string& path, VAddr load_base) {
    FileUtil::IOFile file(path, "rb");
    if (!file.IsOpen()) {
        return {};
    }

    // Read NSO header
    NsoHeader nso_header{};
    file.Seek(0, SEEK_SET);
    if (sizeof(NsoHeader) != file.ReadBytes(&nso_header, sizeof(NsoHeader))) {
        return {};
    }
    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return {};
    }

    // Build program image
    Kernel::SharedPtr<Kernel::CodeSet> codeset = Kernel::CodeSet::Create("");
    std::vector<u8> program_image;
    for (int i = 0; i < nso_header.segments.size(); ++i) {
        std::vector<u8> data =
            ReadSegment(file, nso_header.segments[i], nso_header.segments_compressed_size[i]);
        program_image.resize(nso_header.segments[i].location);
        program_image.insert(program_image.end(), data.begin(), data.end());
        codeset->segments[i].addr = nso_header.segments[i].location;
        codeset->segments[i].offset = nso_header.segments[i].location;
        codeset->segments[i].size = PageAlignSize(static_cast<u32>(data.size()));
    }

    // MOD header pointer is at .text offset + 4
    u32 module_offset;
    std::memcpy(&module_offset, program_image.data() + 4, sizeof(u32));

    // Read MOD header
    ModHeader mod_header{};
    // Default .bss to size in segment header if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nso_header.segments[2].bss_size)};
    std::memcpy(&mod_header, program_image.data() + module_offset, sizeof(ModHeader));
    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }
    codeset->data.size += bss_size;
    const u32 image_size{PageAlignSize(static_cast<u32>(program_image.size()) + bss_size)};
    program_image.resize(image_size);

    // Load codeset for current process
    codeset->name = path;
    codeset->memory = std::make_shared<std::vector<u8>>(std::move(program_image));
    Core::CurrentProcess()->LoadModule(codeset, load_base);

    return load_base + image_size;
}

ResultStatus AppLoader_NSO::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }
    if (!file.IsOpen()) {
        return ResultStatus::Error;
    }

    // Load module
    LoadModule(filepath, Memory::PROCESS_IMAGE_VADDR);
    NGLOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", filepath, Memory::PROCESS_IMAGE_VADDR);

    process->svc_access_mask.set();
    process->address_mappings = default_address_mappings;
    process->resource_limit =
        Kernel::ResourceLimit::GetForCategory(Kernel::ResourceLimitCategory::APPLICATION);
    process->Run(Memory::PROCESS_IMAGE_VADDR, THREADPRIO_DEFAULT, Memory::DEFAULT_STACK_SIZE);

    is_loaded = true;
    return ResultStatus::Success;
}

} // namespace Loader
