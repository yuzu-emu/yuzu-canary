// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/kernel_executable.h"
#include "core/file_sys/program_metadata.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/process.h"
#include "core/loader/kip.h"

namespace Loader {

namespace {
constexpr u32 PageAlignSize(u32 size) {
    return (size + Memory::PAGE_MASK) & ~Memory::PAGE_MASK;
}
} // Anonymous namespace

AppLoader_KIP::AppLoader_KIP(FileSys::VirtualFile file_)
    : AppLoader(std::move(file_)), kip(std::make_unique<FileSys::KIP>(file)) {}

AppLoader_KIP::~AppLoader_KIP() = default;

FileType AppLoader_KIP::IdentifyType(const FileSys::VirtualFile& file) {
    u32_le magic{};
    if (file->GetSize() < sizeof(u32) || file->ReadObject(&magic) != sizeof(u32)) {
        return FileType::Error;
    }

    if (magic == Common::MakeMagic('K', 'I', 'P', '1')) {
        return FileType::KIP;
    }

    return FileType::Error;
}

FileType AppLoader_KIP::GetFileType() const {
    return (kip != nullptr && kip->GetStatus() == ResultStatus::Success) ? FileType::KIP
                                                                         : FileType::Error;
}

AppLoader::LoadResult AppLoader_KIP::Load(Kernel::Process& process) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (kip == nullptr) {
        return {ResultStatus::ErrorNullFile, {}};
    }

    if (kip->GetStatus() != ResultStatus::Success) {
        return {kip->GetStatus(), {}};
    }

    const auto get_kip_address_space_type = [](const auto& kip) {
        return kip.Is64Bit()
                   ? (kip.Is39BitAddressSpace() ? FileSys::ProgramAddressSpaceType::Is39Bit
                                                : FileSys::ProgramAddressSpaceType::Is36Bit)
                   : FileSys::ProgramAddressSpaceType::Is32Bit;
    };

    const auto address_space = get_kip_address_space_type(*kip);

    FileSys::ProgramMetadata metadata;
    metadata.LoadManual(kip->Is64Bit(), address_space, kip->GetMainThreadPriority(),
                        kip->GetMainThreadCpuCore(), kip->GetMainThreadStackSize(),
                        kip->GetTitleID(), 0xFFFFFFFFFFFFFFFF, kip->GetKernelCapabilities());

    const VAddr base_address = process.VMManager().GetCodeRegionBaseAddress();
    Kernel::CodeSet codeset;
    std::vector<u8> program_image;

    const auto load_segment = [&program_image](Kernel::CodeSet::Segment& segment,
                                               const std::vector<u8>& data, u32 offset) {
        segment.addr = offset;
        segment.offset = offset;
        segment.size = PageAlignSize(static_cast<u32>(data.size()));
        program_image.resize(offset);
        program_image.insert(program_image.end(), data.begin(), data.end());
    };

    load_segment(codeset.CodeSegment(), kip->GetTextSection(), kip->GetTextOffset());
    load_segment(codeset.RODataSegment(), kip->GetRODataSection(), kip->GetRODataOffset());
    load_segment(codeset.DataSegment(), kip->GetDataSection(), kip->GetDataOffset());

    program_image.resize(PageAlignSize(kip->GetBSSOffset()) + kip->GetBSSSize());
    codeset.DataSegment().size += kip->GetBSSSize();

    GDBStub::RegisterModule(kip->GetName(), base_address, base_address + program_image.size());

    codeset.memory = std::move(program_image);
    process.LoadModule(std::move(codeset), base_address);

    LOG_DEBUG(Loader, "loaded module {} @ 0x{:X}", kip->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success,
            LoadParameters{kip->GetMainThreadPriority(), kip->GetMainThreadStackSize()}};
}

} // namespace Loader
