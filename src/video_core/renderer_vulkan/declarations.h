// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vulkan/vulkan.hpp>

namespace Vulkan {

// vulkan.hpp unique handlers use DispatchLoaderStatic
template <typename T>
using UniqueHandle = vk::UniqueHandle<T, vk::DispatchLoaderDynamic>;

using UniqueAccelerationStructureNV = UniqueHandle<vk::AccelerationStructureNV>;
using UniqueBuffer = UniqueHandle<vk::Buffer>;
using UniqueBufferView = UniqueHandle<vk::BufferView>;
using UniqueCommandBuffer = UniqueHandle<vk::CommandBuffer>;
using UniqueCommandPool = UniqueHandle<vk::CommandPool>;
using UniqueDescriptorPool = UniqueHandle<vk::DescriptorPool>;
using UniqueDescriptorSet = UniqueHandle<vk::DescriptorSet>;
using UniqueDescriptorSetLayout = UniqueHandle<vk::DescriptorSetLayout>;
using UniqueDescriptorUpdateTemplate = UniqueHandle<vk::DescriptorUpdateTemplate>;
using UniqueDevice = UniqueHandle<vk::Device>;
using UniqueDeviceMemory = UniqueHandle<vk::DeviceMemory>;
using UniqueEvent = UniqueHandle<vk::Event>;
using UniqueFence = UniqueHandle<vk::Fence>;
using UniqueFramebuffer = UniqueHandle<vk::Framebuffer>;
using UniqueImage = UniqueHandle<vk::Image>;
using UniqueImageView = UniqueHandle<vk::ImageView>;
using UniqueIndirectCommandsLayoutNVX = UniqueHandle<vk::IndirectCommandsLayoutNVX>;
using UniqueObjectTableNVX = UniqueHandle<vk::ObjectTableNVX>;
using UniquePipeline = UniqueHandle<vk::Pipeline>;
using UniquePipelineCache = UniqueHandle<vk::PipelineCache>;
using UniquePipelineLayout = UniqueHandle<vk::PipelineLayout>;
using UniqueQueryPool = UniqueHandle<vk::QueryPool>;
using UniqueRenderPass = UniqueHandle<vk::RenderPass>;
using UniqueSampler = UniqueHandle<vk::Sampler>;
using UniqueSamplerYcbcrConversion = UniqueHandle<vk::SamplerYcbcrConversion>;
using UniqueSemaphore = UniqueHandle<vk::Semaphore>;
using UniqueShaderModule = UniqueHandle<vk::ShaderModule>;
using UniqueSwapchainKHR = UniqueHandle<vk::SwapchainKHR>;
using UniqueValidationCacheEXT = UniqueHandle<vk::ValidationCacheEXT>;

} // namespace Vulkan
