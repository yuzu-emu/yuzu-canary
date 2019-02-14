// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <vector>
#include "common/common_types.h"
#include "video_core/renderer_vulkan/declarations.h"

namespace Vulkan {

/// Format usage descriptor
enum class FormatType { Linear, Optimal, Buffer };

/// Handles data specific to a physical device.
class VKDevice final {
public:
    explicit VKDevice(const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical,
                      vk::SurfaceKHR surface);
    ~VKDevice();

    /// Initializes the device. Returns true on success.
    bool Create(const vk::DispatchLoaderDynamic& dldi, vk::Instance instance);

    /**
     * Returns a format supported by the device for the passed requeriments.
     * @param wanted_format The ideal format to be returned. It may not be the returned format.
     * @param wanted_usage The usage that must be fulfilled even if the format is not supported.
     * @param format_type Format type usage.
     * @returns A format supported by the device.
     */
    vk::Format GetSupportedFormat(vk::Format wanted_format, vk::FormatFeatureFlags wanted_usage,
                                  FormatType format_type) const;

    /// Returns the dispatch loader with direct function pointers of the device
    const vk::DispatchLoaderDynamic& GetDispatchLoader() const {
        return dld;
    }

    /// Returns the logical device
    vk::Device GetLogical() const {
        return logical.get();
    }

    /// Returns the physical device.
    vk::PhysicalDevice GetPhysical() const {
        return physical;
    }

    /// Returns the main graphics queue.
    vk::Queue GetGraphicsQueue() const {
        return graphics_queue;
    }

    /// Returns the main present queue.
    vk::Queue GetPresentQueue() const {
        return present_queue;
    }

    /// Returns main graphics queue family index.
    u32 GetGraphicsFamily() const {
        return graphics_family;
    }

    /// Returns main present queue family index.
    u32 GetPresentFamily() const {
        return present_family;
    }

    /// Returns if the device is integrated with the host CPU
    bool IsIntegrated() const {
        return device_type == vk::PhysicalDeviceType::eIntegratedGpu;
    }

    /// Returns uniform buffer alignment requeriment
    u64 GetUniformBufferAlignment() const {
        return uniform_buffer_alignment;
    }

    /// Checks if the physical device is suitable.
    static bool IsSuitable(const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical,
                           vk::SurfaceKHR surface);

private:
    /// Sets up queue families.
    void SetupFamilies(const vk::DispatchLoaderDynamic& dldi, vk::SurfaceKHR surface);

    /// Sets up device properties.
    void SetupProperties(const vk::DispatchLoaderDynamic& dldi);

    /// Returns a list of queue initialization descriptors.
    std::vector<vk::DeviceQueueCreateInfo> GetDeviceQueueCreateInfos() const;

    /// Returns true if a format is supported.
    bool IsFormatSupported(vk::Format wanted_format, vk::FormatFeatureFlags wanted_usage,
                           FormatType format_type) const;

    /// Returns the device properties for Vulkan formats.
    static std::map<vk::Format, vk::FormatProperties> GetFormatProperties(
        const vk::DispatchLoaderDynamic& dldi, vk::PhysicalDevice physical);

    const vk::PhysicalDevice physical;  ///< Physical device
    vk::DispatchLoaderDynamic dld;      ///< Device function pointers
    UniqueDevice logical;               ///< Logical device
    vk::Queue graphics_queue;           ///< Main graphics queue
    vk::Queue present_queue;            ///< Main present queue
    u32 graphics_family{};              ///< Main graphics queue family index
    u32 present_family{};               ///< Main present queue family index
    vk::PhysicalDeviceType device_type; ///< Physical device type
    u64 uniform_buffer_alignment{};     ///< Uniform buffer alignment requeriment
    std::map<vk::Format, vk::FormatProperties> format_properties; ///< Format properties dictionary
};

} // namespace Vulkan
