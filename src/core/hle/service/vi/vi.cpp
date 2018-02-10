// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>

#include "common/alignment.h"
#include "common/scope_exit.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/nvflinger/buffer_queue.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/vi/vi_m.h"
#include "core/hle/service/vi/vi_s.h"
#include "core/hle/service/vi/vi_u.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Service {
namespace VI {

struct DisplayInfo {
    char display_name[0x40]{"Default"};
    u64 unknown_1{1};
    u64 unknown_2{1};
    u64 width{1920};
    u64 height{1080};
};
static_assert(sizeof(DisplayInfo) == 0x60, "DisplayInfo has wrong size");

class Parcel {
public:
    // This default size was chosen arbitrarily.
    static constexpr size_t DefaultBufferSize = 0x40;
    Parcel() : buffer(DefaultBufferSize) {}
    explicit Parcel(std::vector<u8> data) : buffer(std::move(data)) {}
    virtual ~Parcel() = default;

    template <typename T>
    T Read() {
        T val;
        std::memcpy(&val, buffer.data() + read_index, sizeof(T));
        read_index += sizeof(T);
        read_index = Common::AlignUp(read_index, 4);
        return val;
    }

    template <typename T>
    T ReadUnaligned() {
        T val;
        std::memcpy(&val, buffer.data() + read_index, sizeof(T));
        read_index += sizeof(T);
        return val;
    }

    std::vector<u8> ReadBlock(size_t length) {
        const u8* const begin = buffer.data() + read_index;
        const u8* const end = begin + length;
        std::vector<u8> data(begin, end);
        read_index += length;
        read_index = Common::AlignUp(read_index, 4);
        return data;
    }

    std::u16string ReadInterfaceToken() {
        u32 unknown = Read<u32_le>();
        u32 length = Read<u32_le>();

        std::u16string token{};

        for (u32 ch = 0; ch < length + 1; ++ch) {
            token.push_back(ReadUnaligned<u16_le>());
        }

        read_index = Common::AlignUp(read_index, 4);

        return token;
    }

    template <typename T>
    void Write(const T& val) {
        if (buffer.size() < write_index + sizeof(T))
            buffer.resize(buffer.size() + sizeof(T) + DefaultBufferSize);
        std::memcpy(buffer.data() + write_index, &val, sizeof(T));
        write_index += sizeof(T);
        write_index = Common::AlignUp(write_index, 4);
    }

    void Deserialize() {
        Header header{};
        std::memcpy(&header, buffer.data(), sizeof(Header));

        read_index = header.data_offset;
        DeserializeData();
    }

    std::vector<u8> Serialize() {
        ASSERT(read_index == 0);
        write_index = sizeof(Header);

        SerializeData();

        Header header{};
        header.data_offset = sizeof(Header);
        header.data_size = static_cast<u32_le>(write_index - sizeof(Header));
        std::memcpy(buffer.data(), &header, sizeof(Header));

        return buffer;
    }

protected:
    virtual void SerializeData() {}

    virtual void DeserializeData() {}

private:
    struct Header {
        u32_le data_size;
        u32_le data_offset;
        u32_le objects_size;
        u32_le objects_offset;
    };
    static_assert(sizeof(Header) == 16, "ParcelHeader has wrong size");

    std::vector<u8> buffer;
    size_t read_index = 0;
    size_t write_index = 0;
};

class NativeWindow : public Parcel {
public:
    explicit NativeWindow(u32 id) : Parcel() {
        data.id = id;
    }
    ~NativeWindow() override = default;

protected:
    void SerializeData() override {
        Write(data);
    }

private:
    struct Data {
        u32_le magic = 2;
        u32_le process_id;
        u32_le id;
        INSERT_PADDING_BYTES(0xC);
        std::array<u8, 8> dispdrv = {'d', 'i', 's', 'p', 'd', 'r', 'v', '\0'};
        INSERT_PADDING_BYTES(8);
    };
    static_assert(sizeof(Data) == 0x28, "ParcelData has wrong size");

    Data data{};
};

class IGBPConnectRequestParcel : public Parcel {
public:
    explicit IGBPConnectRequestParcel(const std::vector<u8>& buffer) : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPConnectRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        data = Read<Data>();
    }

    struct Data {
        u32_le unk;
        u32_le api;
        u32_le producer_controlled_by_app;
    };

    Data data;
};

class IGBPConnectResponseParcel : public Parcel {
public:
    explicit IGBPConnectResponseParcel(u32 width, u32 height) : Parcel() {
        data.width = width;
        data.height = height;
    }
    ~IGBPConnectResponseParcel() override = default;

protected:
    void SerializeData() override {
        Write(data);
    }

private:
    struct Data {
        u32_le width;
        u32_le height;
        u32_le transform_hint;
        u32_le num_pending_buffers;
        u32_le status;
    };
    static_assert(sizeof(Data) == 20, "ParcelData has wrong size");

    Data data{};
};

class IGBPSetPreallocatedBufferRequestParcel : public Parcel {
public:
    explicit IGBPSetPreallocatedBufferRequestParcel(const std::vector<u8>& buffer)
        : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPSetPreallocatedBufferRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        data = Read<Data>();
        buffer = Read<NVFlinger::IGBPBuffer>();
    }

    struct Data {
        u32_le slot;
        INSERT_PADDING_WORDS(1);
        u32_le graphic_buffer_length;
        INSERT_PADDING_WORDS(1);
    };

    Data data;
    NVFlinger::IGBPBuffer buffer;
};

class IGBPSetPreallocatedBufferResponseParcel : public Parcel {
public:
    IGBPSetPreallocatedBufferResponseParcel() : Parcel() {}
    ~IGBPSetPreallocatedBufferResponseParcel() override = default;

protected:
    void SerializeData() override {
        // TODO(Subv): Find out what this means
        Write<u32>(0);
    }
};

class IGBPDequeueBufferRequestParcel : public Parcel {
public:
    explicit IGBPDequeueBufferRequestParcel(const std::vector<u8>& buffer) : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPDequeueBufferRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        data = Read<Data>();
    }

    struct Data {
        u32_le pixel_format;
        u32_le width;
        u32_le height;
        u32_le get_frame_timestamps;
        u32_le usage;
    };

    Data data;
};

class IGBPDequeueBufferResponseParcel : public Parcel {
public:
    explicit IGBPDequeueBufferResponseParcel(u32 slot) : Parcel(), slot(slot) {}
    ~IGBPDequeueBufferResponseParcel() override = default;

protected:
    void SerializeData() override {
        Write(slot);
        // TODO(Subv): Find out how this Fence is used.
        std::array<u32_le, 11> fence = {};
        Write(fence);
        Write<u32_le>(0);
    }

    u32_le slot;
};

class IGBPRequestBufferRequestParcel : public Parcel {
public:
    explicit IGBPRequestBufferRequestParcel(const std::vector<u8>& buffer) : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPRequestBufferRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        slot = Read<u32_le>();
    }

    u32_le slot;
};

class IGBPRequestBufferResponseParcel : public Parcel {
public:
    explicit IGBPRequestBufferResponseParcel(NVFlinger::IGBPBuffer buffer)
        : Parcel(), buffer(buffer) {}
    ~IGBPRequestBufferResponseParcel() override = default;

protected:
    void SerializeData() override {
        // TODO(bunnei): Find out what this all means. Writing anything non-zero here breaks libnx.
        Write<u32_le>(0);
        Write<u32_le>(0);
        Write<u32_le>(0);
        Write(buffer);
        Write<u32_le>(0);
    }

    NVFlinger::IGBPBuffer buffer;
};

class IGBPQueueBufferRequestParcel : public Parcel {
public:
    explicit IGBPQueueBufferRequestParcel(const std::vector<u8>& buffer) : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPQueueBufferRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        data = Read<Data>();
    }

    struct Data {
        u32_le slot;
        INSERT_PADDING_WORDS(2);
        u32_le timestamp;
        INSERT_PADDING_WORDS(20);
    };
    static_assert(sizeof(Data) == 96, "ParcelData has wrong size");

    Data data;
};

class IGBPQueueBufferResponseParcel : public Parcel {
public:
    explicit IGBPQueueBufferResponseParcel(u32 width, u32 height) : Parcel() {
        data.width = width;
        data.height = height;
    }
    ~IGBPQueueBufferResponseParcel() override = default;

protected:
    void SerializeData() override {
        Write(data);
    }

private:
    struct Data {
        u32_le width;
        u32_le height;
        u32_le transform_hint;
        u32_le num_pending_buffers;
        u32_le status;
    };
    static_assert(sizeof(Data) == 20, "ParcelData has wrong size");

    Data data{};
};

class IGBPQueryRequestParcel : public Parcel {
public:
    explicit IGBPQueryRequestParcel(const std::vector<u8>& buffer) : Parcel(buffer) {
        Deserialize();
    }
    ~IGBPQueryRequestParcel() override = default;

    void DeserializeData() override {
        std::u16string token = ReadInterfaceToken();
        type = Read<u32_le>();
    }

    u32 type;
};

class IGBPQueryResponseParcel : public Parcel {
public:
    explicit IGBPQueryResponseParcel(u32 value) : Parcel(), value(value) {}
    ~IGBPQueryResponseParcel() override = default;

protected:
    void SerializeData() override {
        Write(value);
    }

private:
    u32_le value;
};

class IHOSBinderDriver final : public ServiceFramework<IHOSBinderDriver> {
public:
    explicit IHOSBinderDriver(std::shared_ptr<NVFlinger::NVFlinger> nv_flinger)
        : ServiceFramework("IHOSBinderDriver"), nv_flinger(std::move(nv_flinger)) {
        static const FunctionInfo functions[] = {
            {0, &IHOSBinderDriver::TransactParcel, "TransactParcel"},
            {1, &IHOSBinderDriver::AdjustRefcount, "AdjustRefcount"},
            {2, &IHOSBinderDriver::GetNativeHandle, "GetNativeHandle"},
            {3, &IHOSBinderDriver::TransactParcelAuto, "TransactParcelAuto"},
        };
        RegisterHandlers(functions);
    }
    ~IHOSBinderDriver() = default;

private:
    enum class TransactionId {
        RequestBuffer = 1,
        SetBufferCount = 2,
        DequeueBuffer = 3,
        DetachBuffer = 4,
        DetachNextBuffer = 5,
        AttachBuffer = 6,
        QueueBuffer = 7,
        CancelBuffer = 8,
        Query = 9,
        Connect = 10,
        Disconnect = 11,

        AllocateBuffers = 13,
        SetPreallocatedBuffer = 14
    };

    void TransactParcel(u32 id, TransactionId transaction, const std::vector<u8>& input_data,
                        VAddr output_addr, u64 output_size) {
        auto buffer_queue = nv_flinger->GetBufferQueue(id);
        std::vector<u8> response_buffer;
        if (transaction == TransactionId::Connect) {
            IGBPConnectRequestParcel request{input_data};
            IGBPConnectResponseParcel response{1280, 720};
            response_buffer = response.Serialize();
        } else if (transaction == TransactionId::SetPreallocatedBuffer) {
            IGBPSetPreallocatedBufferRequestParcel request{input_data};

            buffer_queue->SetPreallocatedBuffer(request.data.slot, request.buffer);

            IGBPSetPreallocatedBufferResponseParcel response{};
            response_buffer = response.Serialize();
        } else if (transaction == TransactionId::DequeueBuffer) {
            IGBPDequeueBufferRequestParcel request{input_data};

            u32 slot = buffer_queue->DequeueBuffer(request.data.pixel_format, request.data.width,
                                                   request.data.height);

            IGBPDequeueBufferResponseParcel response{slot};
            response_buffer = response.Serialize();
        } else if (transaction == TransactionId::RequestBuffer) {
            IGBPRequestBufferRequestParcel request{input_data};

            auto& buffer = buffer_queue->RequestBuffer(request.slot);

            IGBPRequestBufferResponseParcel response{buffer};
            response_buffer = response.Serialize();
        } else if (transaction == TransactionId::QueueBuffer) {
            IGBPQueueBufferRequestParcel request{input_data};

            buffer_queue->QueueBuffer(request.data.slot);

            IGBPQueueBufferResponseParcel response{1280, 720};
            response_buffer = response.Serialize();
        } else if (transaction == TransactionId::Query) {
            IGBPQueryRequestParcel request{input_data};

            u32 value =
                buffer_queue->Query(static_cast<NVFlinger::BufferQueue::QueryType>(request.type));

            IGBPQueryResponseParcel response{value};
            response_buffer = response.Serialize();

        } else {
            ASSERT_MSG(false, "Unimplemented");
        }

        Memory::WriteBlock(output_addr, response_buffer.data(), output_size);
    }

    void TransactParcel(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        u32 id = rp.Pop<u32>();
        auto transaction = static_cast<TransactionId>(rp.Pop<u32>());
        u32 flags = rp.Pop<u32>();
        LOG_DEBUG(Service_VI, "called, transaction=%x", transaction);

        auto& input_buffer = ctx.BufferDescriptorA()[0];
        auto& output_buffer = ctx.BufferDescriptorB()[0];
        std::vector<u8> input_data(input_buffer.Size());
        Memory::ReadBlock(input_buffer.Address(), input_data.data(), input_buffer.Size());

        TransactParcel(id, transaction, input_data, output_buffer.Address(), output_buffer.Size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void TransactParcelAuto(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        u32 id = rp.Pop<u32>();
        auto transaction = static_cast<TransactionId>(rp.Pop<u32>());
        u32 flags = rp.Pop<u32>();
        LOG_DEBUG(Service_VI, "called, transaction=%x", transaction);

        auto& input_buffer = ctx.BufferDescriptorX()[0];
        auto& output_buffer = ctx.BufferDescriptorC()[0];
        std::vector<u8> input_data(input_buffer.size);
        Memory::ReadBlock(input_buffer.Address(), input_data.data(), input_buffer.size);

        TransactParcel(id, transaction, input_data, output_buffer.Address(), output_buffer.Size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void AdjustRefcount(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        u32 id = rp.Pop<u32>();
        s32 addval = rp.PopRaw<s32>();
        u32 type = rp.Pop<u32>();

        LOG_WARNING(Service_VI, "(STUBBED) called id=%u, addval=%08X, type=%08X", id, addval, type);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetNativeHandle(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        u32 id = rp.Pop<u32>();
        u32 unknown = rp.Pop<u32>();

        auto buffer_queue = nv_flinger->GetBufferQueue(id);

        // TODO(Subv): Find out what this actually is.

        LOG_WARNING(Service_VI, "(STUBBED) called id=%u, unknown=%08X", id, unknown);
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(buffer_queue->GetNativeHandle());
    }

    std::shared_ptr<NVFlinger::NVFlinger> nv_flinger;
};

class ISystemDisplayService final : public ServiceFramework<ISystemDisplayService> {
public:
    ISystemDisplayService() : ServiceFramework("ISystemDisplayService") {
        static const FunctionInfo functions[] = {
            {1200, nullptr, "GetZOrderCountMin"},
            {2205, &ISystemDisplayService::SetLayerZ, "SetLayerZ"},
        };
        RegisterHandlers(functions);
    }
    ~ISystemDisplayService() = default;

private:
    void SetLayerZ(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_VI, "(STUBBED) called");
        IPC::RequestParser rp{ctx};
        u64 layer_id = rp.Pop<u64>();
        u64 z_value = rp.Pop<u64>();

        IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
        rb.Push(RESULT_SUCCESS);
    }
};

class IManagerDisplayService final : public ServiceFramework<IManagerDisplayService> {
public:
    explicit IManagerDisplayService(std::shared_ptr<NVFlinger::NVFlinger> nv_flinger)
        : ServiceFramework("IManagerDisplayService"), nv_flinger(std::move(nv_flinger)) {
        static const FunctionInfo functions[] = {
            {1020, &IManagerDisplayService::CloseDisplay, "CloseDisplay"},
            {1102, nullptr, "GetDisplayResolution"},
            {2010, &IManagerDisplayService::CreateManagedLayer, "CreateManagedLayer"},
            {6000, &IManagerDisplayService::AddToLayerStack, "AddToLayerStack"},
        };
        RegisterHandlers(functions);
    }
    ~IManagerDisplayService() = default;

private:
    void CloseDisplay(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_VI, "(STUBBED) called");
        IPC::RequestParser rp{ctx};
        u64 display = rp.Pop<u64>();

        IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
        rb.Push(RESULT_SUCCESS);
    }

    void CreateManagedLayer(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_VI, "(STUBBED) called");
        IPC::RequestParser rp{ctx};
        u32 unknown = rp.Pop<u32>();
        rp.Skip(1, false);
        u64 display = rp.Pop<u64>();
        u64 aruid = rp.Pop<u64>();

        u64 layer_id = nv_flinger->CreateLayer(display);

        IPC::ResponseBuilder rb = rp.MakeBuilder(4, 0, 0);
        rb.Push(RESULT_SUCCESS);
        rb.Push(layer_id);
    }

    void AddToLayerStack(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_VI, "(STUBBED) called");
        IPC::RequestParser rp{ctx};
        u32 stack = rp.Pop<u32>();
        u64 layer_id = rp.Pop<u64>();

        IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
        rb.Push(RESULT_SUCCESS);
    }

    std::shared_ptr<NVFlinger::NVFlinger> nv_flinger;
};

void IApplicationDisplayService::GetRelayService(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IHOSBinderDriver>(nv_flinger);
}

void IApplicationDisplayService::GetSystemDisplayService(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISystemDisplayService>();
}

void IApplicationDisplayService::GetManagerDisplayService(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IManagerDisplayService>(nv_flinger);
}

void IApplicationDisplayService::GetIndirectDisplayTransactionService(
    Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IHOSBinderDriver>(nv_flinger);
}

void IApplicationDisplayService::OpenDisplay(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    IPC::RequestParser rp{ctx};
    auto name_buf = rp.PopRaw<std::array<u8, 0x40>>();
    auto end = std::find(name_buf.begin(), name_buf.end(), '\0');

    std::string name(name_buf.begin(), end);

    ASSERT_MSG(name == "Default", "Non-default displays aren't supported yet");

    IPC::ResponseBuilder rb = rp.MakeBuilder(4, 0, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(nv_flinger->OpenDisplay(name));
}

void IApplicationDisplayService::CloseDisplay(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    IPC::RequestParser rp{ctx};
    u64 display_id = rp.Pop<u64>();

    IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
    rb.Push(RESULT_SUCCESS);
}

void IApplicationDisplayService::OpenLayer(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    IPC::RequestParser rp{ctx};
    auto name_buf = rp.PopRaw<std::array<u8, 0x40>>();
    auto end = std::find(name_buf.begin(), name_buf.end(), '\0');

    std::string display_name(name_buf.begin(), end);

    u64 layer_id = rp.Pop<u64>();
    u64 aruid = rp.Pop<u64>();

    auto& buffer = ctx.BufferDescriptorB()[0];

    u64 display_id = nv_flinger->OpenDisplay(display_name);
    u32 buffer_queue_id = nv_flinger->GetBufferQueueId(display_id, layer_id);

    NativeWindow native_window{buffer_queue_id};
    auto data = native_window.Serialize();
    Memory::WriteBlock(buffer.Address(), data.data(), data.size());

    IPC::ResponseBuilder rb = rp.MakeBuilder(4, 0, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(data.size());
}

void IApplicationDisplayService::CreateStrayLayer(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::RequestParser rp{ctx};
    u32 flags = rp.Pop<u32>();
    rp.Pop<u32>(); // padding
    u64 display_id = rp.Pop<u64>();

    auto& buffer = ctx.BufferDescriptorB()[0];

    // TODO(Subv): What's the difference between a Stray and a Managed layer?

    u64 layer_id = nv_flinger->CreateLayer(display_id);
    u32 buffer_queue_id = nv_flinger->GetBufferQueueId(display_id, layer_id);

    NativeWindow native_window{buffer_queue_id};
    auto data = native_window.Serialize();
    Memory::WriteBlock(buffer.Address(), data.data(), data.size());

    IPC::ResponseBuilder rb = rp.MakeBuilder(6, 0, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(layer_id);
    rb.Push<u64>(data.size());
}

void IApplicationDisplayService::DestroyStrayLayer(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    IPC::RequestParser rp{ctx};
    u64 layer_id = rp.Pop<u64>();

    IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
    rb.Push(RESULT_SUCCESS);
}

void IApplicationDisplayService::SetLayerScalingMode(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    IPC::RequestParser rp{ctx};
    u32 scaling_mode = rp.Pop<u32>();
    u64 unknown = rp.Pop<u64>();

    IPC::ResponseBuilder rb = rp.MakeBuilder(2, 0, 0);
    rb.Push(RESULT_SUCCESS);
}

void IApplicationDisplayService::ListDisplays(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    DisplayInfo display_info;
    auto& buffer = ctx.BufferDescriptorB()[0];
    Memory::WriteBlock(buffer.Address(), &display_info, sizeof(DisplayInfo));
    IPC::ResponseBuilder rb = rp.MakeBuilder(4, 0, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push<u64>(1);
    LOG_WARNING(Service_VI, "(STUBBED) called");
}

void IApplicationDisplayService::GetDisplayVsyncEvent(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    IPC::RequestParser rp{ctx};
    u64 display_id = rp.Pop<u64>();

    auto vsync_event = nv_flinger->GetVsyncEvent(display_id);

    IPC::ResponseBuilder rb = rp.MakeBuilder(2, 1, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(vsync_event);
}

IApplicationDisplayService::IApplicationDisplayService(
    std::shared_ptr<NVFlinger::NVFlinger> nv_flinger)
    : ServiceFramework("IApplicationDisplayService"), nv_flinger(std::move(nv_flinger)) {
    static const FunctionInfo functions[] = {
        {100, &IApplicationDisplayService::GetRelayService, "GetRelayService"},
        {101, &IApplicationDisplayService::GetSystemDisplayService, "GetSystemDisplayService"},
        {102, &IApplicationDisplayService::GetManagerDisplayService, "GetManagerDisplayService"},
        {103, &IApplicationDisplayService::GetIndirectDisplayTransactionService,
         "GetIndirectDisplayTransactionService"},
        {1000, &IApplicationDisplayService::ListDisplays, "ListDisplays"},
        {1010, &IApplicationDisplayService::OpenDisplay, "OpenDisplay"},
        {1020, &IApplicationDisplayService::CloseDisplay, "CloseDisplay"},
        {2101, &IApplicationDisplayService::SetLayerScalingMode, "SetLayerScalingMode"},
        {2020, &IApplicationDisplayService::OpenLayer, "OpenLayer"},
        {2030, &IApplicationDisplayService::CreateStrayLayer, "CreateStrayLayer"},
        {2031, &IApplicationDisplayService::DestroyStrayLayer, "DestroyStrayLayer"},
        {5202, &IApplicationDisplayService::GetDisplayVsyncEvent, "GetDisplayVsyncEvent"},
    };
    RegisterHandlers(functions);
}

void InstallInterfaces(SM::ServiceManager& service_manager,
                       std::shared_ptr<NVFlinger::NVFlinger> nv_flinger) {
    std::make_shared<VI_M>(nv_flinger)->InstallAsService(service_manager);
    std::make_shared<VI_S>(nv_flinger)->InstallAsService(service_manager);
    std::make_shared<VI_U>(nv_flinger)->InstallAsService(service_manager);
}

} // namespace VI
} // namespace Service
