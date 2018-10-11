// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <utility>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/nvmap.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace Service::Nvidia::Devices {

nvhost_as_gpu::nvhost_as_gpu(std::shared_ptr<nvmap> nvmap_dev) : nvmap_dev(std::move(nvmap_dev)) {}
nvhost_as_gpu::~nvhost_as_gpu() = default;

u32 nvhost_as_gpu::ioctl(Ioctl command, const std::vector<u8>& input, std::vector<u8>& output) {
    LOG_DEBUG(Service_NVDRV, "called, command=0x{:08X}, input_size=0x{:X}, output_size=0x{:X}",
              command.raw, input.size(), output.size());

    switch (static_cast<IoctlCommand>(command.raw)) {
    case IoctlCommand::IocInitalizeExCommand:
        return InitalizeEx(input, output);
    case IoctlCommand::IocAllocateSpaceCommand:
        return AllocateSpace(input, output);
    case IoctlCommand::IocMapBufferExCommand:
        return MapBufferEx(input, output);
    case IoctlCommand::IocBindChannelCommand:
        return BindChannel(input, output);
    case IoctlCommand::IocGetVaRegionsCommand:
        return GetVARegions(input, output);
    case IoctlCommand::IocUnmapBufferCommand:
        return UnmapBuffer(input, output);
    }

    if (static_cast<IoctlCommand>(command.cmd.Value()) == IoctlCommand::IocRemapCommand)
        return Remap(input, output);

    UNIMPLEMENTED_MSG("Unimplemented ioctl command");
    return 0;
}

u32 nvhost_as_gpu::InitalizeEx(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlInitalizeEx params{};
    std::memcpy(&params, input.data(), input.size());
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, big_page_size=0x{:X}", params.big_page_size);
    return 0;
}

u32 nvhost_as_gpu::AllocateSpace(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlAllocSpace params{};
    std::memcpy(&params, input.data(), input.size());
    LOG_DEBUG(Service_NVDRV, "called, pages={:X}, page_size={:X}, flags={:X}", params.pages,
              params.page_size, params.flags);

    auto& gpu = Core::System::GetInstance().GPU();
    const u64 size{static_cast<u64>(params.pages) * static_cast<u64>(params.page_size)};
    if (params.flags & 1) {
        params.offset = gpu.MemoryManager().AllocateSpace(params.offset, size, 1);
    } else {
        params.offset = gpu.MemoryManager().AllocateSpace(size, params.align);
    }

    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_as_gpu::Remap(const std::vector<u8>& input, std::vector<u8>& output) {
    std::size_t num_entries = input.size() / sizeof(IoctlRemapEntry);

    LOG_WARNING(Service_NVDRV, "(STUBBED) called, num_entries=0x{:X}", num_entries);

    std::vector<IoctlRemapEntry> entries(num_entries);
    std::memcpy(entries.data(), input.data(), input.size());

    auto& gpu = Core::System::GetInstance().GPU();

    for (const auto& entry : entries) {
        LOG_WARNING(Service_NVDRV, "remap entry, offset=0x{:X} handle=0x{:X} pages=0x{:X}",
                    entry.offset, entry.nvmap_handle, entry.pages);
        Tegra::GPUVAddr offset = static_cast<Tegra::GPUVAddr>(entry.offset) << 0x10;

        auto object = nvmap_dev->GetObject(entry.nvmap_handle);
        ASSERT(object);

        ASSERT(object->status == nvmap::Object::Status::Allocated);

        u64 size = static_cast<u64>(entry.pages) << 0x10;
        ASSERT(size <= object->size);

        Tegra::GPUVAddr returned = gpu.MemoryManager().MapBufferEx(object->addr, offset, size);
        ASSERT(returned == offset);
    }
    std::memcpy(output.data(), entries.data(), output.size());
    return 0;
}

u32 nvhost_as_gpu::MapBufferEx(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlMapBufferEx params{};
    std::memcpy(&params, input.data(), input.size());

    LOG_DEBUG(Service_NVDRV,
              "called, flags={:X}, nvmap_handle={:X}, buffer_offset={}, mapping_size={}"
              ", offset={}",
              params.flags, params.nvmap_handle, params.buffer_offset, params.mapping_size,
              params.offset);

    if (!params.nvmap_handle) {
        return 0;
    }

    auto object = nvmap_dev->GetObject(params.nvmap_handle);
    ASSERT(object);

    // We can only map objects that have already been assigned a CPU address.
    ASSERT(object->status == nvmap::Object::Status::Allocated);

    ASSERT(params.buffer_offset == 0);

    // The real nvservices doesn't make a distinction between handles and ids, and
    // object can only have one handle and it will be the same as its id. Assert that this is the
    // case to prevent unexpected behavior.
    ASSERT(object->id == params.nvmap_handle);

    auto& gpu = Core::System::GetInstance().GPU();

    if (params.flags & 1) {
        params.offset = gpu.MemoryManager().MapBufferEx(object->addr, params.offset, object->size);
    } else {
        params.offset = gpu.MemoryManager().MapBufferEx(object->addr, object->size);
    }

    // Create a new mapping entry for this operation.
    ASSERT_MSG(buffer_mappings.find(params.offset) == buffer_mappings.end(),
               "Offset is already mapped");

    BufferMapping mapping{};
    mapping.nvmap_handle = params.nvmap_handle;
    mapping.offset = params.offset;
    mapping.size = object->size;

    buffer_mappings[params.offset] = mapping;

    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_as_gpu::UnmapBuffer(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlUnmapBuffer params{};
    std::memcpy(&params, input.data(), input.size());

    LOG_DEBUG(Service_NVDRV, "called, offset=0x{:X}", params.offset);

    const auto itr = buffer_mappings.find(params.offset);
    if (itr == buffer_mappings.end()) {
        LOG_WARNING(Service_NVDRV, "Tried to unmap an invalid offset 0x{:X}", params.offset);
        // Hardware tests shows that unmapping an already unmapped buffer always returns successful
        // and doesn't fail.
        return 0;
    }

    auto& system_instance = Core::System::GetInstance();

    // Remove this memory region from the rasterizer cache.
    auto& gpu = system_instance.GPU();
    auto cpu_addr = gpu.MemoryManager().GpuToCpuAddress(params.offset);
    ASSERT(cpu_addr);
    system_instance.Renderer().Rasterizer().FlushAndInvalidateRegion(*cpu_addr, itr->second.size);

    params.offset = gpu.MemoryManager().UnmapBuffer(params.offset, itr->second.size);

    buffer_mappings.erase(itr->second.offset);

    std::memcpy(output.data(), &params, output.size());
    return 0;
}

u32 nvhost_as_gpu::BindChannel(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlBindChannel params{};
    std::memcpy(&params, input.data(), input.size());
    LOG_DEBUG(Service_NVDRV, "called, fd={:X}", params.fd);
    channel = params.fd;
    return 0;
}

u32 nvhost_as_gpu::GetVARegions(const std::vector<u8>& input, std::vector<u8>& output) {
    IoctlGetVaRegions params{};
    std::memcpy(&params, input.data(), input.size());
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, buf_addr={:X}, buf_size={:X}", params.buf_addr,
                params.buf_size);

    params.buf_size = 0x30;
    params.regions[0].offset = 0x04000000;
    params.regions[0].page_size = 0x1000;
    params.regions[0].pages = 0x3fbfff;

    params.regions[1].offset = 0x04000000;
    params.regions[1].page_size = 0x10000;
    params.regions[1].pages = 0x1bffff;
    // TODO(ogniK): This probably can stay stubbed but should add support way way later
    std::memcpy(output.data(), &params, output.size());
    return 0;
}

} // namespace Service::Nvidia::Devices
