// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <opus.h>

#include "common/common_funcs.h"
#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/audio/hwopus.h"

namespace Service::Audio {

struct OpusDeleter {
    void operator()(void* ptr) const {
        operator delete(ptr);
    }
};

class IHardwareOpusDecoderManager final : public ServiceFramework<IHardwareOpusDecoderManager> {
public:
    IHardwareOpusDecoderManager(std::unique_ptr<OpusDecoder, OpusDeleter> decoder, u32 sample_rate,
                                u32 channel_count)
        : ServiceFramework("IHardwareOpusDecoderManager"), decoder(std::move(decoder)),
          sample_rate(sample_rate), channel_count(channel_count) {
        static const FunctionInfo functions[] = {
            {0, &IHardwareOpusDecoderManager::DecodeInterleaved, "DecodeInterleaved"},
            {1, nullptr, "SetContext"},
            {2, nullptr, "DecodeInterleavedForMultiStream"},
            {3, nullptr, "SetContextForMultiStream"},
            {4, &IHardwareOpusDecoderManager::DecodeInterleavedWithPerformance,
             "DecodeInterleavedWithPerformance"},
            {5, nullptr, "Unknown5"},
            {6, nullptr, "Unknown6"},
            {7, nullptr, "Unknown7"},
        };
        RegisterHandlers(functions);
    }

private:
    void DecodeInterleaved(Kernel::HLERequestContext& ctx) {
        u32 consumed = 0;
        u32 sample_count = 0;
        std::vector<opus_int16> samples(ctx.GetWriteBufferSize() / sizeof(opus_int16));
        if (!Decoder_DecodeInterleaved(consumed, sample_count, ctx.ReadBuffer(), samples)) {
            IPC::ResponseBuilder rb{ctx, 2};
            // TODO(ogniK): Use correct error code
            rb.Push(ResultCode(-1));
            return;
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(consumed);
        rb.Push<u32>(sample_count);
        ctx.WriteBuffer(samples.data(), samples.size() * sizeof(s16));
    }

    void DecodeInterleavedWithPerformance(Kernel::HLERequestContext& ctx) {
        u32 consumed = 0;
        u32 sample_count = 0;
        u64 performance = 0;
        std::vector<opus_int16> samples(ctx.GetWriteBufferSize() / sizeof(opus_int16));
        if (!Decoder_DecodeInterleaved(consumed, sample_count, ctx.ReadBuffer(), samples,
                                       performance)) {
            IPC::ResponseBuilder rb{ctx, 2};
            // TODO(ogniK): Use correct error code
            rb.Push(ResultCode(-1));
            return;
        }
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(consumed);
        rb.Push<u64>(performance);
        rb.Push<u32>(sample_count);
        ctx.WriteBuffer(samples.data(), samples.size() * sizeof(s16));
    }

    bool Decoder_DecodeInterleaved(
        u32& consumed, u32& sample_count, const std::vector<u8>& input,
        std::vector<opus_int16>& output,
        std::optional<std::reference_wrapper<u64>> performance_time = std::nullopt) {
        const auto start_time = std::chrono::high_resolution_clock::now();
        std::size_t raw_output_sz = output.size() * sizeof(opus_int16);
        if (sizeof(OpusHeader) > input.size())
            return false;
        OpusHeader hdr{};
        std::memcpy(&hdr, input.data(), sizeof(OpusHeader));
        if (sizeof(OpusHeader) + static_cast<u32>(hdr.sz) > input.size()) {
            return false;
        }
        auto frame = input.data() + sizeof(OpusHeader);
        auto decoded_sample_count = opus_packet_get_nb_samples(
            frame, static_cast<opus_int32>(input.size() - sizeof(OpusHeader)),
            static_cast<opus_int32>(sample_rate));
        if (decoded_sample_count * channel_count * sizeof(u16) > raw_output_sz)
            return false;
        auto out_sample_count =
            opus_decode(decoder.get(), frame, hdr.sz, output.data(),
                        (static_cast<int>(raw_output_sz / sizeof(s16) / channel_count)), 0);
        if (out_sample_count < 0)
            return false;
        const auto end_time = std::chrono::high_resolution_clock::now() - start_time;
        sample_count = out_sample_count;
        consumed = static_cast<u32>(sizeof(OpusHeader) + hdr.sz);
        if (performance_time.has_value()) {
            performance_time->get() =
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time).count();
        }
        return true;
    }

    struct OpusHeader {
        u32_be sz; // Needs to be BE for some odd reason
        INSERT_PADDING_WORDS(1);
    };
    static_assert(sizeof(OpusHeader) == 0x8, "OpusHeader is an invalid size");

    std::unique_ptr<OpusDecoder, OpusDeleter> decoder;
    u32 sample_rate;
    u32 channel_count;
};

static std::size_t WorkerBufferSize(u32 channel_count) {
    ASSERT_MSG(channel_count == 1 || channel_count == 2, "Invalid channel count");
    return opus_decoder_get_size(static_cast<int>(channel_count));
}

void HwOpus::GetWorkBufferSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto sample_rate = rp.Pop<u32>();
    auto channel_count = rp.Pop<u32>();
    ASSERT_MSG(sample_rate == 48000 || sample_rate == 24000 || sample_rate == 16000 ||
                   sample_rate == 12000 || sample_rate == 8000,
               "Invalid sample rate");
    ASSERT_MSG(channel_count == 1 || channel_count == 2, "Invalid channel count");
    u32 worker_buffer_sz = static_cast<u32>(WorkerBufferSize(channel_count));
    LOG_DEBUG(Audio, "called worker_buffer_sz={}", worker_buffer_sz);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(worker_buffer_sz);
}

void HwOpus::OpenOpusDecoder(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto sample_rate = rp.Pop<u32>();
    auto channel_count = rp.Pop<u32>();
    auto buffer_sz = rp.Pop<u32>();
    LOG_DEBUG(Audio, "called sample_rate={}, channel_count={}, buffer_size={}", sample_rate,
              channel_count, buffer_sz);
    ASSERT_MSG(sample_rate == 48000 || sample_rate == 24000 || sample_rate == 16000 ||
                   sample_rate == 12000 || sample_rate == 8000,
               "Invalid sample rate");
    ASSERT_MSG(channel_count == 1 || channel_count == 2, "Invalid channel count");

    std::size_t worker_sz = WorkerBufferSize(channel_count);
    ASSERT_MSG(buffer_sz < worker_sz, "Worker buffer too large");
    std::unique_ptr<OpusDecoder, OpusDeleter> decoder{
        static_cast<OpusDecoder*>(operator new(worker_sz))};
    if (opus_decoder_init(decoder.get(), sample_rate, channel_count)) {
        IPC::ResponseBuilder rb{ctx, 2};
        // TODO(ogniK): Use correct error code
        rb.Push(ResultCode(-1));
        return;
    }

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IHardwareOpusDecoderManager>(std::move(decoder), sample_rate,
                                                     channel_count);
}

HwOpus::HwOpus() : ServiceFramework("hwopus") {
    static const FunctionInfo functions[] = {
        {0, &HwOpus::OpenOpusDecoder, "OpenOpusDecoder"},
        {1, &HwOpus::GetWorkBufferSize, "GetWorkBufferSize"},
        {2, nullptr, "OpenOpusDecoderForMultiStream"},
        {3, nullptr, "GetWorkBufferSizeForMultiStream"},
    };
    RegisterHandlers(functions);
}

HwOpus::~HwOpus() = default;

} // namespace Service::Audio
