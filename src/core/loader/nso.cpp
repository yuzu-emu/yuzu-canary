// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <vector>

#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "common/lz4_compression.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/loader/nso.h"
#include "core/memory.h"
#include "core/settings.h"

namespace Loader {
namespace {
struct MODHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(MODHeader) == 0x1c, "MODHeader has incorrect size.");

std::vector<u8> DecompressSegment(const std::vector<u8>& compressed_data,
                                  const NSOSegmentHeader& header) {
    const std::vector<u8> uncompressed_data =
        Common::Compression::DecompressDataLZ4(compressed_data, header.size);

    ASSERT_MSG(uncompressed_data.size() == header.size, "{} != {}", header.size,
               uncompressed_data.size());

    return uncompressed_data;
}

constexpr u32 PageAlignSize(u32 size) {
    return (size + Memory::PAGE_MASK) & ~Memory::PAGE_MASK;
}
} // Anonymous namespace

bool NSOHeader::IsSegmentCompressed(size_t segment_num) const {
    ASSERT_MSG(segment_num < 3, "Invalid segment {}", segment_num);
    return ((flags >> segment_num) & 1) != 0;
}

AppLoader_NSO::AppLoader_NSO(FileSys::VirtualFile file) : AppLoader(std::move(file)) {}

FileType AppLoader_NSO::IdentifyType(const FileSys::VirtualFile& file) {
    u32 magic = 0;
    if (file->ReadObject(&magic) != sizeof(magic)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') != magic) {
        return FileType::Error;
    }

    return FileType::NSO;
}

std::optional<VAddr> AppLoader_NSO::LoadModule(Kernel::Process& process,
                                               const FileSys::VfsFile& file, VAddr load_base,
                                               bool should_pass_arguments,
                                               std::optional<FileSys::PatchManager> pm) {
    if (file.GetSize() < sizeof(NSOHeader)) {
        return {};
    }

    NSOHeader nso_header{};
    if (sizeof(NSOHeader) != file.ReadObject(&nso_header)) {
        return {};
    }

    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0')) {
        return {};
    }

    // Build program image
    Kernel::CodeSet codeset;
    Kernel::PhysicalMemory program_image;
    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        std::vector<u8> data =
            file.ReadBytes(nso_header.segments_compressed_size[i], nso_header.segments[i].offset);
        if (nso_header.IsSegmentCompressed(i)) {
            data = DecompressSegment(data, nso_header.segments[i]);
        }
        program_image.resize(nso_header.segments[i].location);
        program_image.insert(program_image.end(), data.begin(), data.end());
        codeset.segments[i].addr = nso_header.segments[i].location;
        codeset.segments[i].offset = nso_header.segments[i].location;
        codeset.segments[i].size = PageAlignSize(static_cast<u32>(data.size()));
    }

    if (should_pass_arguments && !Settings::values.program_args.empty()) {
        const auto arg_data = Settings::values.program_args;
        codeset.DataSegment().size += NSO_ARGUMENT_DATA_ALLOCATION_SIZE;
        NSOArgumentHeader args_header{
            NSO_ARGUMENT_DATA_ALLOCATION_SIZE, static_cast<u32_le>(arg_data.size()), {}};
        const auto end_offset = program_image.size();
        program_image.resize(static_cast<u32>(program_image.size()) +
                             NSO_ARGUMENT_DATA_ALLOCATION_SIZE);
        std::memcpy(program_image.data() + end_offset, &args_header, sizeof(NSOArgumentHeader));
        std::memcpy(program_image.data() + end_offset + sizeof(NSOArgumentHeader), arg_data.data(),
                    arg_data.size());
    }

    // MOD header pointer is at .text offset + 4
    u32 module_offset;
    std::memcpy(&module_offset, program_image.data() + 4, sizeof(u32));

    // Read MOD header
    MODHeader mod_header{};
    // Default .bss to size in segment header if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nso_header.segments[2].bss_size)};
    std::memcpy(&mod_header, program_image.data() + module_offset, sizeof(MODHeader));
    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }
    codeset.DataSegment().size += bss_size;
    const u32 image_size{PageAlignSize(static_cast<u32>(program_image.size()) + bss_size)};
    program_image.resize(image_size);

    // Apply patches if necessary
    if (pm && (pm->HasNSOPatch(nso_header.build_id) || Settings::values.dump_nso)) {
        std::vector<u8> pi_header;
        pi_header.insert(pi_header.begin(), reinterpret_cast<u8*>(&nso_header),
                         reinterpret_cast<u8*>(&nso_header) + sizeof(NSOHeader));
        pi_header.insert(pi_header.begin() + sizeof(NSOHeader), program_image.begin(),
                         program_image.end());

        pi_header = pm->PatchNSO(pi_header, file.GetName());

        std::copy(pi_header.begin() + sizeof(NSOHeader), pi_header.end(), program_image.begin());
    }

    // Apply cheats if they exist and the program has a valid title ID
    if (pm) {
        auto& system = Core::System::GetInstance();
        const auto cheats = pm->CreateCheatList(system, nso_header.build_id);
        if (!cheats.empty()) {
            system.RegisterCheatList(cheats, Common::HexToString(nso_header.build_id), load_base,
                                     load_base + program_image.size());
        }
    }

    // Load codeset for current process
    codeset.memory = std::move(program_image);
    process.LoadModule(std::move(codeset), load_base);

    // Register module with GDBStub
    GDBStub::RegisterModule(file.GetName(), load_base, load_base);

    return load_base + image_size;
}

AppLoader_NSO::LoadResult AppLoader_NSO::Load(Kernel::Process& process) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    modules.clear();

    // Load module
    const VAddr base_address = process.VMManager().GetCodeRegionBaseAddress();
    if (!LoadModule(process, *file, base_address, true)) {
        return {ResultStatus::ErrorLoadingNSO, {}};
    }

    modules.insert_or_assign(base_address, file->GetName());
    LOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", file->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success,
            LoadParameters{Kernel::THREADPRIO_DEFAULT, Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NSO::ReadNSOModules(Modules& modules) {
    modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
