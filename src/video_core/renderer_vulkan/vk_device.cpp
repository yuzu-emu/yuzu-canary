// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <optional>
#include <set>
#include <vector>
#include "common/assert.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_device.h"

namespace Vulkan {

namespace Alternatives {

constexpr std::array<vk::Format, 3> Depth24UnormS8Uint = {
    vk::Format::eD32SfloatS8Uint, vk::Format::eD16UnormS8Uint, {}};
constexpr std::array<vk::Format, 3> Depth16UnormS8Uint = {
    vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint, {}};

} // namespace Alternatives

constexpr const vk::Format* GetFormatAlternatives(vk::Format format) {
    switch (format) {
    case vk::Format::eD24UnormS8Uint:
        return Alternatives::Depth24UnormS8Uint.data();
    case vk::Format::eD16UnormS8Uint:
        return Alternatives::Depth16UnormS8Uint.data();
    default:
        return nullptr;
    }
}

constexpr vk::FormatFeatureFlags GetFormatFeatures(vk::FormatProperties properties,
                                                   FormatType format_type) {
    switch (format_type) {
    case FormatType::Linear:
        return properties.linearTilingFeatures;
    case FormatType::Optimal:
        return properties.optimalTilingFeatures;
    case FormatType::Buffer:
        return properties.bufferFeatures;
    default:
        return {};
    }
}

VKDevice::VKDevice(const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical,
                   vk::SurfaceKHR surface)
    : physical{physical}, format_properties{GetFormatProperties(dldi, physical)} {
    SetupFamilies(dldi, surface);
    SetupProperties(dldi);
}

VKDevice::~VKDevice() = default;

bool VKDevice::Create(const vk::DispatchLoaderDynamic& dldi, vk::Instance instance) {
    const auto queue_cis = GetDeviceQueueCreateInfos();
    vk::PhysicalDeviceFeatures device_features{};

    const std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const vk::DeviceCreateInfo device_ci({}, static_cast<u32>(queue_cis.size()), queue_cis.data(),
                                         0, nullptr, static_cast<u32>(extensions.size()),
                                         extensions.data(), &device_features);
    vk::Device dummy_logical;
    if (physical.createDevice(&device_ci, nullptr, &dummy_logical, dldi) != vk::Result::eSuccess) {
        LOG_CRITICAL(Render_Vulkan, "Logical device failed to be created!");
        return false;
    }

    dld.init(instance, dldi.vkGetInstanceProcAddr, dummy_logical, dldi.vkGetDeviceProcAddr);
    logical = UniqueDevice(
        dummy_logical, vk::ObjectDestroy<vk::NoParent, vk::DispatchLoaderDynamic>(nullptr, dld));

    graphics_queue = logical->getQueue(graphics_family, 0, dld);
    present_queue = logical->getQueue(present_family, 0, dld);
    return true;
}

vk::Format VKDevice::GetSupportedFormat(vk::Format wanted_format,
                                        vk::FormatFeatureFlags wanted_usage,
                                        FormatType format_type) const {
    if (IsFormatSupported(wanted_format, wanted_usage, format_type)) {
        return wanted_format;
    }
    // The wanted format is not supported by hardware, search for alternatives
    const vk::Format* alternatives = GetFormatAlternatives(wanted_format);
    if (alternatives == nullptr) {
        LOG_CRITICAL(Render_Vulkan,
                     "Format={} with usage={} and type={} has no defined alternatives and host "
                     "hardware does not support it",
                     static_cast<u32>(wanted_format), static_cast<u32>(wanted_usage),
                     static_cast<u32>(format_type));
        UNREACHABLE();
        return wanted_format;
    }

    std::size_t i = 0;
    for (vk::Format alternative = alternatives[0]; alternative != vk::Format{};
         alternative = alternatives[++i]) {
        if (!IsFormatSupported(alternative, wanted_usage, format_type))
            continue;
        LOG_WARNING(Render_Vulkan,
                    "Emulating format={} with alternative format={} with usage={} and type={}",
                    static_cast<u32>(wanted_format), static_cast<u32>(alternative),
                    static_cast<u32>(wanted_usage), static_cast<u32>(format_type));
        return alternative;
    }

    // No alternatives found, panic
    LOG_CRITICAL(Render_Vulkan,
                 "Format={} with usage={} and type={} is not supported by the host hardware and "
                 "doesn't support any of the alternatives",
                 static_cast<u32>(wanted_format), static_cast<u32>(wanted_usage),
                 static_cast<u32>(format_type));
    UNREACHABLE();
    return wanted_format;
}

bool VKDevice::IsFormatSupported(vk::Format wanted_format, vk::FormatFeatureFlags wanted_usage,
                                 FormatType format_type) const {
    const auto it = format_properties.find(wanted_format);
    if (it == format_properties.end()) {
        LOG_CRITICAL(Render_Vulkan, "Unimplemented format query={}",
                     static_cast<u32>(wanted_format));
        UNREACHABLE();
        return true;
    }
    const vk::FormatFeatureFlags supported_usage = GetFormatFeatures(it->second, format_type);
    return (supported_usage & wanted_usage) == wanted_usage;
}

bool VKDevice::IsSuitable(const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical,
                          vk::SurfaceKHR surface) {
    const std::string swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    bool has_swapchain{};
    for (const auto& prop : physical.enumerateDeviceExtensionProperties(nullptr, dldi)) {
        has_swapchain |= prop.extensionName == swapchain_extension;
    }
    if (!has_swapchain) {
        // The device doesn't support creating swapchains.
        return false;
    }

    bool has_graphics{}, has_present{};
    const auto queue_family_properties = physical.getQueueFamilyProperties(dldi);
    for (u32 i = 0; i < static_cast<u32>(queue_family_properties.size()); ++i) {
        const auto& family = queue_family_properties[i];
        if (family.queueCount == 0)
            continue;

        has_graphics |=
            (family.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlagBits>(0);
        has_present |= physical.getSurfaceSupportKHR(i, surface, dldi) != 0;
    }
    if (!has_graphics || !has_present) {
        // The device doesn't have a graphics and present queue.
        return false;
    }

    // TODO(Rodrigo): Check if the device matches all requeriments.
    const vk::PhysicalDeviceProperties props = physical.getProperties(dldi);
    if (props.limits.maxUniformBufferRange < 65536) {
        return false;
    }

    // Device is suitable.
    return true;
}

void VKDevice::SetupFamilies(const vk::DispatchLoaderDynamic& dldi, vk::SurfaceKHR surface) {
    std::optional<u32> graphics_family_, present_family_;

    const auto queue_family_properties = physical.getQueueFamilyProperties(dldi);
    for (u32 i = 0; i < static_cast<u32>(queue_family_properties.size()); ++i) {
        if (graphics_family_ && present_family_)
            break;

        const auto& queue_family = queue_family_properties[i];
        if (queue_family.queueCount == 0)
            continue;

        if (queue_family.queueFlags & vk::QueueFlagBits::eGraphics)
            graphics_family_ = i;
        if (physical.getSurfaceSupportKHR(i, surface, dldi))
            present_family_ = i;
    }
    ASSERT(graphics_family_ && present_family_);

    graphics_family = *graphics_family_;
    present_family = *present_family_;
}

void VKDevice::SetupProperties(const vk::DispatchLoaderDynamic& dldi) {
    const vk::PhysicalDeviceProperties props = physical.getProperties(dldi);
    device_type = props.deviceType;
    uniform_buffer_alignment = static_cast<u64>(props.limits.minUniformBufferOffsetAlignment);
}

std::vector<vk::DeviceQueueCreateInfo> VKDevice::GetDeviceQueueCreateInfos() const {
    static const float QUEUE_PRIORITY = 1.f;

    std::set<u32> unique_queue_families = {graphics_family, present_family};
    std::vector<vk::DeviceQueueCreateInfo> queue_cis;

    for (u32 queue_family : unique_queue_families)
        queue_cis.push_back({{}, queue_family, 1, &QUEUE_PRIORITY});

    return queue_cis;
}

std::map<vk::Format, vk::FormatProperties> VKDevice::GetFormatProperties(
    const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical) {
    std::map<vk::Format, vk::FormatProperties> format_properties;

    const auto AddFormatQuery = [&format_properties, &dldi, physical](vk::Format format) {
        format_properties.emplace(format, physical.getFormatProperties(format, dldi));
    };
    AddFormatQuery(vk::Format::eA8B8G8R8UnormPack32);
    AddFormatQuery(vk::Format::eR5G6B5UnormPack16);
    AddFormatQuery(vk::Format::eD32Sfloat);
    AddFormatQuery(vk::Format::eD16UnormS8Uint);
    AddFormatQuery(vk::Format::eD24UnormS8Uint);
    AddFormatQuery(vk::Format::eD32SfloatS8Uint);

    return format_properties;
}

} // namespace Vulkan
