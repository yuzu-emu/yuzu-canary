// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/vector_math.h"
#include "core/memory.h"
#include "core/tracer/recorder.h"
#include "video_core/command_processor.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_compute.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Tegra {

enum class BufferMethods {
    BindObject = 0,
    CountBufferMethods = 0x40,
};

MICROPROFILE_DEFINE(ProcessCommandLists, "GPU", "Execute command buffer", MP_RGB(128, 128, 192));

void GPU::ProcessCommandLists(const std::vector<CommandListHeader>& commands) {
    MICROPROFILE_SCOPE(ProcessCommandLists);

    auto WriteReg = [this](u32 method, u32 subchannel, u32 value, u32 remaining_params) {
        LOG_TRACE(HW_GPU,
                  "Processing method {:08X} on subchannel {} value "
                  "{:08X} remaining params {}",
                  method, subchannel, value, remaining_params);

        ASSERT(subchannel < bound_engines.size());

        if (method == static_cast<u32>(BufferMethods::BindObject)) {
            // Bind the current subchannel to the desired engine id.
            LOG_DEBUG(HW_GPU, "Binding subchannel {} to engine {}", subchannel, value);
            bound_engines[subchannel] = static_cast<EngineID>(value);
            return;
        }

        if (method < static_cast<u32>(BufferMethods::CountBufferMethods)) {
            // TODO(Subv): Research and implement these methods.
            LOG_ERROR(HW_GPU, "Special buffer methods other than Bind are not implemented");
            return;
        }

        const EngineID engine = bound_engines[subchannel];

        switch (engine) {
        case EngineID::FERMI_TWOD_A:
            fermi_2d->WriteReg(method, value);
            break;
        case EngineID::MAXWELL_B:
            maxwell_3d->WriteReg(method, value, remaining_params);
            break;
        case EngineID::MAXWELL_COMPUTE_B:
            maxwell_compute->WriteReg(method, value);
            break;
        case EngineID::MAXWELL_DMA_COPY_A:
            maxwell_dma->WriteReg(method, value);
            break;
        default:
            UNIMPLEMENTED_MSG("Unimplemented engine");
        }
    };

    for (auto entry : commands) {
        Tegra::GPUVAddr address = entry.Address();
        u32 size = entry.sz;
        const boost::optional<VAddr> head_address = memory_manager->GpuToCpuAddress(address);
        VAddr current_addr = *head_address;
        while (current_addr < *head_address + size * sizeof(CommandHeader)) {
            const CommandHeader header = {Memory::Read32(current_addr)};
            current_addr += sizeof(u32);

            switch (header.mode.Value()) {
            case SubmissionMode::IncreasingOld:
            case SubmissionMode::Increasing: {
                // Increase the method value with each argument.
                for (unsigned i = 0; i < header.arg_count; ++i) {
                    WriteReg(header.method + i, header.subchannel, Memory::Read32(current_addr),
                             header.arg_count - i - 1);
                    current_addr += sizeof(u32);
                }
                break;
            }
            case SubmissionMode::NonIncreasingOld:
            case SubmissionMode::NonIncreasing: {
                // Use the same method value for all arguments.
                for (unsigned i = 0; i < header.arg_count; ++i) {
                    WriteReg(header.method, header.subchannel, Memory::Read32(current_addr),
                             header.arg_count - i - 1);
                    current_addr += sizeof(u32);
                }
                break;
            }
            case SubmissionMode::IncreaseOnce: {
                ASSERT(header.arg_count.Value() >= 1);

                // Use the original method for the first argument and then the next method for all
                // other arguments.
                WriteReg(header.method, header.subchannel, Memory::Read32(current_addr),
                         header.arg_count - 1);
                current_addr += sizeof(u32);

                for (unsigned i = 1; i < header.arg_count; ++i) {
                    WriteReg(header.method + 1, header.subchannel, Memory::Read32(current_addr),
                             header.arg_count - i - 1);
                    current_addr += sizeof(u32);
                }
                break;
            }
            case SubmissionMode::Inline: {
                // The register value is stored in the bits 16-28 as an immediate
                WriteReg(header.method, header.subchannel, header.inline_data, 0);
                break;
            }
            default:
                UNIMPLEMENTED();
            }
        }
    }
}

} // namespace Tegra
