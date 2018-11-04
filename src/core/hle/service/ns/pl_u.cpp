// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include <vector>

#include <FontChineseSimplified.h>
#include <FontChineseTraditional.h>
#include <FontExtendedChineseSimplified.h>
#include <FontKorean.h>
#include <FontNintendoExtended.h>
#include <FontStandard.h>

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/pl_u.h"

namespace Service::NS {

enum class FontArchives : u64 {
    Extension = 0x0100000000000810,
    Standard = 0x0100000000000811,
    Korean = 0x0100000000000812,
    ChineseTraditional = 0x0100000000000813,
    ChineseSimple = 0x0100000000000814,
};

struct FontRegion {
    u32 offset;
    u32 size;
};

constexpr std::array<std::pair<FontArchives, const char*>, 7> SHARED_FONTS{
    std::make_pair(FontArchives::Standard, "nintendo_udsg-r_std_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_org_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseSimple, "nintendo_udsg-r_ext_zh-cn_003.bfttf"),
    std::make_pair(FontArchives::ChineseTraditional, "nintendo_udjxh-db_zh-tw_003.bfttf"),
    std::make_pair(FontArchives::Korean, "nintendo_udsg-r_ko_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext_003.bfttf"),
    std::make_pair(FontArchives::Extension, "nintendo_ext2_003.bfttf"),
};

constexpr std::array<const char*, 7> SHARED_FONTS_TTF{
    "FontStandard.ttf",
    "FontChineseSimplified.ttf",
    "FontExtendedChineseSimplified.ttf",
    "FontChineseTraditional.ttf",
    "FontKorean.ttf",
    "FontNintendoExtended.ttf",
    "FontNintendoExtended2.ttf",
};

// The below data is specific to shared font data dumped from Switch on f/w 2.2
// Virtual address and offsets/sizes likely will vary by dump
constexpr VAddr SHARED_FONT_MEM_VADDR{0x00000009d3016000ULL};
constexpr u32 EXPECTED_RESULT{0x7f9a0218}; // What we expect the decrypted bfttf first 4 bytes to be
constexpr u32 EXPECTED_MAGIC{0x36f81a1e};  // What we expect the encrypted bfttf first 4 bytes to be
constexpr u64 SHARED_FONT_MEM_SIZE{0x1100000};
constexpr FontRegion EMPTY_REGION{0, 0};

enum class LoadState : u32 {
    Loading = 0,
    Done = 1,
};

static void DecryptSharedFont(const std::vector<u32>& input, std::vector<u8>& output,
                              std::size_t& offset) {
    ASSERT_MSG(offset + (input.size() * sizeof(u32)) < SHARED_FONT_MEM_SIZE,
               "Shared fonts exceeds 17mb!");
    ASSERT_MSG(input[0] == EXPECTED_MAGIC, "Failed to derive key, unexpected magic number");

    const u32 KEY = input[0] ^ EXPECTED_RESULT; // Derive key using an inverse xor
    std::vector<u32> transformed_font(input.size());
    // TODO(ogniK): Figure out a better way to do this
    std::transform(input.begin(), input.end(), transformed_font.begin(),
                   [&KEY](u32 font_data) { return Common::swap32(font_data ^ KEY); });
    transformed_font[1] = Common::swap32(transformed_font[1]) ^ KEY; // "re-encrypt" the size
    std::memcpy(output.data() + offset, transformed_font.data(),
                transformed_font.size() * sizeof(u32));
    offset += transformed_font.size() * sizeof(u32);
}

static void EncryptSharedFont(const std::vector<u8>& input, std::vector<u8>& output,
                              std::size_t& offset) {
    ASSERT_MSG(offset + input.size() + 8 < SHARED_FONT_MEM_SIZE, "Shared fonts exceeds 17mb!");
    const u32 KEY = EXPECTED_MAGIC ^ EXPECTED_RESULT;
    std::memcpy(output.data() + offset, &EXPECTED_RESULT, sizeof(u32)); // Magic header
    const u32 ENC_SIZE = static_cast<u32>(input.size()) ^ KEY;
    std::memcpy(output.data() + offset + sizeof(u32), &ENC_SIZE, sizeof(u32));
    std::memcpy(output.data() + offset + (sizeof(u32) * 2), input.data(), input.size());
    offset += input.size() + (sizeof(u32) * 2);
}

// Helper function to make BuildSharedFontsRawRegions a bit nicer
static u32 GetU32Swapped(const u8* data) {
    u32 value;
    std::memcpy(&value, data, sizeof(value));
    return Common::swap32(value);
}

struct PL_U::Impl {
    const FontRegion& GetSharedFontRegion(std::size_t index) const {
        if (index >= shared_font_regions.size() || shared_font_regions.empty()) {
            // No font fallback
            return EMPTY_REGION;
        }
        return shared_font_regions.at(index);
    }

    void BuildSharedFontsRawRegions(const std::vector<u8>& input) {
        // As we can derive the xor key we can just populate the offsets
        // based on the shared memory dump
        unsigned cur_offset = 0;

        for (std::size_t i = 0; i < SHARED_FONTS.size(); i++) {
            // Out of shared fonts/invalid font
            if (GetU32Swapped(input.data() + cur_offset) != EXPECTED_RESULT) {
                break;
            }

            // Derive key withing inverse xor
            const u32 KEY = GetU32Swapped(input.data() + cur_offset) ^ EXPECTED_MAGIC;
            const u32 SIZE = GetU32Swapped(input.data() + cur_offset + 4) ^ KEY;
            shared_font_regions.push_back(FontRegion{cur_offset + 8, SIZE});
            cur_offset += SIZE + 8;
        }
    }

    /// Handle to shared memory region designated for a shared font
    Kernel::SharedPtr<Kernel::SharedMemory> shared_font_mem;

    /// Backing memory for the shared font data
    std::shared_ptr<std::vector<u8>> shared_font;

    // Automatically populated based on shared_fonts dump or system archives.
    std::vector<FontRegion> shared_font_regions;
};

PL_U::PL_U() : ServiceFramework("pl:u"), impl{std::make_unique<Impl>()} {
    static const FunctionInfo functions[] = {
        {0, &PL_U::RequestLoad, "RequestLoad"},
        {1, &PL_U::GetLoadState, "GetLoadState"},
        {2, &PL_U::GetSize, "GetSize"},
        {3, &PL_U::GetSharedMemoryAddressOffset, "GetSharedMemoryAddressOffset"},
        {4, &PL_U::GetSharedMemoryNativeHandle, "GetSharedMemoryNativeHandle"},
        {5, &PL_U::GetSharedFontInOrderOfPriority, "GetSharedFontInOrderOfPriority"},
    };
    RegisterHandlers(functions);
    // Attempt to load shared font data from disk
    const auto* nand = FileSystem::GetSystemNANDContents();
    std::size_t offset = 0;
    // Rebuild shared fonts from data ncas
    if (nand->HasEntry(static_cast<u64>(FontArchives::Standard),
                       FileSys::ContentRecordType::Data)) {
        impl->shared_font = std::make_shared<std::vector<u8>>(SHARED_FONT_MEM_SIZE);
        for (auto font : SHARED_FONTS) {
            const auto nca =
                nand->GetEntry(static_cast<u64>(font.first), FileSys::ContentRecordType::Data);
            if (!nca) {
                LOG_ERROR(Service_NS, "Failed to find {:016X}! Skipping",
                          static_cast<u64>(font.first));
                continue;
            }
            const auto romfs = nca->GetRomFS();
            if (!romfs) {
                LOG_ERROR(Service_NS, "{:016X} has no RomFS! Skipping",
                          static_cast<u64>(font.first));
                continue;
            }
            const auto extracted_romfs = FileSys::ExtractRomFS(romfs);
            if (!extracted_romfs) {
                LOG_ERROR(Service_NS, "Failed to extract RomFS for {:016X}! Skipping",
                          static_cast<u64>(font.first));
                continue;
            }
            const auto font_fp = extracted_romfs->GetFile(font.second);
            if (!font_fp) {
                LOG_ERROR(Service_NS, "{:016X} has no file \"{}\"! Skipping",
                          static_cast<u64>(font.first), font.second);
                continue;
            }
            std::vector<u32> font_data_u32(font_fp->GetSize() / sizeof(u32));
            font_fp->ReadBytes<u32>(font_data_u32.data(), font_fp->GetSize());
            // We need to be BigEndian as u32s for the xor encryption
            std::transform(font_data_u32.begin(), font_data_u32.end(), font_data_u32.begin(),
                           Common::swap32);
            FontRegion region{
                static_cast<u32>(offset + 8),
                static_cast<u32>((font_data_u32.size() * sizeof(u32)) -
                                 8)}; // Font offset and size do not account for the header
            DecryptSharedFont(font_data_u32, *impl->shared_font, offset);
            impl->shared_font_regions.push_back(region);
        }

    } else {
        impl->shared_font = std::make_shared<std::vector<u8>>(
            SHARED_FONT_MEM_SIZE); // Shared memory needs to always be allocated and a fixed size

        const std::string user_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir);
        const std::string filepath{user_path + SHARED_FONT};

        // Create path if not already created
        if (!FileUtil::CreateFullPath(filepath)) {
            LOG_ERROR(Service_NS, "Failed to create sharedfonts path \"{}\"!", filepath);
            return;
        }

        bool using_ttf = false;
        for (const char* font_ttf : SHARED_FONTS_TTF) {
            if (FileUtil::Exists(user_path + font_ttf)) {
                using_ttf = true;
                FileUtil::IOFile file(user_path + font_ttf, "rb");
                if (file.IsOpen()) {
                    std::vector<u8> ttf_bytes(file.GetSize());
                    file.ReadBytes<u8>(ttf_bytes.data(), ttf_bytes.size());
                    FontRegion region{
                        static_cast<u32>(offset + 8),
                        static_cast<u32>(ttf_bytes.size())}; // Font offset and size do not account
                                                             // for the header
                    EncryptSharedFont(ttf_bytes, *impl->shared_font, offset);
                    impl->shared_font_regions.push_back(region);
                } else {
                    LOG_WARNING(Service_NS, "Unable to load font: {}", font_ttf);
                }
            } else if (using_ttf) {
                LOG_WARNING(Service_NS, "Unable to find font: {}", font_ttf);
            }
        }
        if (using_ttf)
            return;
        FileUtil::IOFile file(filepath, "rb");

        if (file.IsOpen()) {
            // Read shared font data
            ASSERT(file.GetSize() == SHARED_FONT_MEM_SIZE);
            file.ReadBytes(impl->shared_font->data(), impl->shared_font->size());
            impl->BuildSharedFontsRawRegions(*impl->shared_font);
        } else {
            LOG_WARNING(Service_NS,
                        "Shared Font file missing. Loading open source replacement from memory");

            // clang-format off
            const std::vector<std::vector<u8>> open_source_shared_fonts_ttf = {
                {std::begin(FontChineseSimplified), std::end(FontChineseSimplified)},
                {std::begin(FontChineseTraditional), std::end(FontChineseTraditional)},
                {std::begin(FontExtendedChineseSimplified), std::end(FontExtendedChineseSimplified)},
                {std::begin(FontKorean), std::end(FontKorean)},
                {std::begin(FontNintendoExtended), std::end(FontNintendoExtended)},
                {std::begin(FontStandard), std::end(FontStandard)},
            };
            // clang-format on

            for (const std::vector<u8>& font_ttf : open_source_shared_fonts_ttf) {
                const FontRegion region{static_cast<u32>(offset + 8),
                                        static_cast<u32>(font_ttf.size())};
                EncryptSharedFont(font_ttf, *impl->shared_font, offset);
                impl->shared_font_regions.push_back(region);
            }
        }
    }
}

PL_U::~PL_U() = default;

void PL_U::RequestLoad(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 shared_font_type{rp.Pop<u32>()};
    // Games don't call this so all fonts should be loaded
    LOG_DEBUG(Service_NS, "called, shared_font_type={}", shared_font_type);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void PL_U::GetLoadState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 font_id{rp.Pop<u32>()};

    LOG_DEBUG(Service_NS, "called, font_id={}", font_id);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(static_cast<u32>(LoadState::Done));
}

void PL_U::GetSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 font_id{rp.Pop<u32>()};

    LOG_DEBUG(Service_NS, "called, font_id={}", font_id);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(impl->GetSharedFontRegion(font_id).size);
}

void PL_U::GetSharedMemoryAddressOffset(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u32 font_id{rp.Pop<u32>()};

    LOG_DEBUG(Service_NS, "called, font_id={}", font_id);
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(impl->GetSharedFontRegion(font_id).offset);
}

void PL_U::GetSharedMemoryNativeHandle(Kernel::HLERequestContext& ctx) {
    // Map backing memory for the font data
    Core::CurrentProcess()->VMManager().MapMemoryBlock(SHARED_FONT_MEM_VADDR, impl->shared_font, 0,
                                                       SHARED_FONT_MEM_SIZE,
                                                       Kernel::MemoryState::Shared);

    // Create shared font memory object
    auto& kernel = Core::System::GetInstance().Kernel();
    impl->shared_font_mem = Kernel::SharedMemory::Create(
        kernel, Core::CurrentProcess(), SHARED_FONT_MEM_SIZE, Kernel::MemoryPermission::ReadWrite,
        Kernel::MemoryPermission::Read, SHARED_FONT_MEM_VADDR, Kernel::MemoryRegion::BASE,
        "PL_U:shared_font_mem");

    LOG_DEBUG(Service_NS, "called");
    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(impl->shared_font_mem);
}

void PL_U::GetSharedFontInOrderOfPriority(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u64 language_code{rp.Pop<u64>()}; // TODO(ogniK): Find out what this is used for
    LOG_DEBUG(Service_NS, "called, language_code={:X}", language_code);
    IPC::ResponseBuilder rb{ctx, 4};
    std::vector<u32> font_codes;
    std::vector<u32> font_offsets;
    std::vector<u32> font_sizes;

    // TODO(ogniK): Have actual priority order
    for (std::size_t i = 0; i < impl->shared_font_regions.size(); i++) {
        font_codes.push_back(static_cast<u32>(i));
        auto region = impl->GetSharedFontRegion(i);
        font_offsets.push_back(region.offset);
        font_sizes.push_back(region.size);
    }

    // Resize buffers if game requests smaller size output.
    font_codes.resize(
        std::min<std::size_t>(font_codes.size(), ctx.GetWriteBufferSize(0) / sizeof(u32)));
    font_offsets.resize(
        std::min<std::size_t>(font_offsets.size(), ctx.GetWriteBufferSize(1) / sizeof(u32)));
    font_sizes.resize(
        std::min<std::size_t>(font_sizes.size(), ctx.GetWriteBufferSize(2) / sizeof(u32)));

    ctx.WriteBuffer(font_codes, 0);
    ctx.WriteBuffer(font_offsets, 1);
    ctx.WriteBuffer(font_sizes, 2);

    rb.Push(RESULT_SUCCESS);
    rb.Push<u8>(static_cast<u8>(LoadState::Done)); // Fonts Loaded
    rb.Push<u32>(static_cast<u32>(font_codes.size()));
}

} // namespace Service::NS
