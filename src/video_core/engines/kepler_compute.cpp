// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <bitset>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

KeplerCompute::KeplerCompute(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                             MemoryManager& memory_manager)
    : system{system}, rasterizer{rasterizer}, memory_manager{memory_manager}, upload_state{
                                                                                  memory_manager,
                                                                                  regs.upload} {}

KeplerCompute::~KeplerCompute() = default;

void KeplerCompute::CallMethod(const GPU::MethodCall& method_call) {
    ASSERT_MSG(method_call.method < Regs::NUM_REGS,
               "Invalid KeplerCompute register, increase the size of the Regs structure");

    regs.reg_array[method_call.method] = method_call.argument;

    switch (method_call.method) {
    case KEPLER_COMPUTE_REG_INDEX(exec_upload): {
        upload_state.ProcessExec(regs.exec_upload.linear != 0);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(data_upload): {
        const bool is_last_call = method_call.IsLastCall();
        upload_state.ProcessData(method_call.argument, is_last_call);
        if (is_last_call) {
            system.GPU().Maxwell3D().dirty.OnMemoryWrite();
        }
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(launch):
        ProcessLaunch();
        break;
    default:
        break;
    }
}

Tegra::Texture::FullTextureInfo KeplerCompute::GetTexture(std::size_t offset) const {
    const std::bitset<8> cbuf_mask = launch_description.const_buffer_enable_mask.Value();
    ASSERT(cbuf_mask[regs.tex_cb_index]);

    const auto& texinfo = launch_description.const_buffer_config[regs.tex_cb_index];
    ASSERT(texinfo.Address() != 0);

    const GPUVAddr address = texinfo.Address() + offset * sizeof(Texture::TextureHandle);
    ASSERT(address < texinfo.Address() + texinfo.size);

    const Texture::TextureHandle tex_handle{memory_manager.Read<u32>(address)};
    return GetTextureInfo(tex_handle, offset);
}

Texture::FullTextureInfo KeplerCompute::GetTextureInfo(const Texture::TextureHandle tex_handle,
                                                       std::size_t offset) const {
    return Texture::FullTextureInfo{static_cast<u32>(offset), GetTICEntry(tex_handle.tic_id),
                                    GetTSCEntry(tex_handle.tsc_id)};
}

u32 KeplerCompute::AccessConstBuffer32(u64 const_buffer, u64 offset) const {
    const auto& buffer = launch_description.const_buffer_config[const_buffer];
    u32 result;
    std::memcpy(&result, memory_manager.GetPointer(buffer.Address() + offset), sizeof(u32));
    return result;
}

void KeplerCompute::ProcessLaunch() {
    const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();
    memory_manager.ReadBlockUnsafe(launch_desc_loc, &launch_description,
                                   LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32));

    const GPUVAddr code_addr = regs.code_loc.Address() + launch_description.program_start;
    LOG_TRACE(HW_GPU, "Compute invocation launched at address 0x{:016x}", code_addr);

    rasterizer.DispatchCompute(code_addr);
}

Texture::TICEntry KeplerCompute::GetTICEntry(u32 tic_index) const {
    const GPUVAddr tic_address_gpu{regs.tic.Address() + tic_index * sizeof(Texture::TICEntry)};

    Texture::TICEntry tic_entry;
    memory_manager.ReadBlockUnsafe(tic_address_gpu, &tic_entry, sizeof(Texture::TICEntry));

    const auto r_type{tic_entry.r_type.Value()};
    const auto g_type{tic_entry.g_type.Value()};
    const auto b_type{tic_entry.b_type.Value()};
    const auto a_type{tic_entry.a_type.Value()};

    // TODO(Subv): Different data types for separate components are not supported
    DEBUG_ASSERT(r_type == g_type && r_type == b_type && r_type == a_type);

    return tic_entry;
}

Texture::TSCEntry KeplerCompute::GetTSCEntry(u32 tsc_index) const {
    const GPUVAddr tsc_address_gpu{regs.tsc.Address() + tsc_index * sizeof(Texture::TSCEntry)};

    Texture::TSCEntry tsc_entry;
    memory_manager.ReadBlockUnsafe(tsc_address_gpu, &tsc_entry, sizeof(Texture::TSCEntry));
    return tsc_entry;
}

} // namespace Tegra::Engines
