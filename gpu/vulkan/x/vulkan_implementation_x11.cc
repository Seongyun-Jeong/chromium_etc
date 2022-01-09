// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/vulkan/x/vulkan_implementation_x11.h"

#include "base/base_paths.h"
#include "base/callback_helpers.h"
#include "base/files/file_path.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/scoped_environment_variable_override.h"
#include "gpu/vulkan/vulkan_function_pointers.h"
#include "gpu/vulkan/vulkan_image.h"
#include "gpu/vulkan/vulkan_instance.h"
#include "gpu/vulkan/vulkan_surface.h"
#include "gpu/vulkan/vulkan_util.h"
#include "gpu/vulkan/x/vulkan_surface_x11.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/gpu_fence.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/x/connection.h"

namespace gpu {

namespace {

bool LoadVulkanLibrary(
    const base::FilePath& path,
    VulkanFunctionPointers* vulkan_function_pointers) {
  base::NativeLibraryLoadError native_library_load_error;
  vulkan_function_pointers->vulkan_loader_library =
      base::LoadNativeLibrary(path, &native_library_load_error);
  return !!vulkan_function_pointers->vulkan_loader_library;
}

}  // namespace

VulkanImplementationX11::VulkanImplementationX11(bool use_swiftshader)
    : VulkanImplementation(use_swiftshader) {
  x11::Connection::Get();
}

VulkanImplementationX11::~VulkanImplementationX11() = default;

bool VulkanImplementationX11::InitializeVulkanInstance(bool using_surface) {
  if (using_surface && !use_swiftshader() && !ui::IsVulkanSurfaceSupported())
    using_surface = false;
  using_surface_ = using_surface;
  // Unset DISPLAY env, so the vulkan can be initialized successfully, if the X
  // server doesn't support Vulkan surface.
  absl::optional<base::ScopedEnvironmentVariableOverride> unset_display;
  if (!using_surface_) {
    unset_display =
        absl::optional<base::ScopedEnvironmentVariableOverride>("DISPLAY");
  }

  std::vector<const char*> required_extensions = {
      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
  if (using_surface_) {
    required_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    required_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
  }

  if (!vulkan_instance_.is_from_angle()) {
    base::FilePath path;
    if (use_swiftshader()) {
      if (!base::PathService::Get(base::DIR_MODULE, &path))
        return false;

      path = path.Append("libvk_swiftshader.so");
    } else {
      path = base::FilePath("libvulkan.so.1");
    }

    VulkanFunctionPointers* vulkan_function_pointers =
        gpu::GetVulkanFunctionPointers();
    if (!LoadVulkanLibrary(path, vulkan_function_pointers))
      return false;
  }

  if (!vulkan_instance_.Initialize(required_extensions, {}))
    return false;
  return true;
}

VulkanInstance* VulkanImplementationX11::GetVulkanInstance() {
  return &vulkan_instance_;
}

std::unique_ptr<VulkanSurface> VulkanImplementationX11::CreateViewSurface(
    gfx::AcceleratedWidget window) {
  if (!using_surface_)
    return nullptr;
  return VulkanSurfaceX11::Create(vulkan_instance_.vk_instance(),
                                  static_cast<x11::Window>(window));
}

bool VulkanImplementationX11::GetPhysicalDevicePresentationSupport(
    VkPhysicalDevice device,
    const std::vector<VkQueueFamilyProperties>& queue_family_properties,
    uint32_t queue_family_index) {
  // TODO(samans): Don't early out once Swiftshader supports this method.
  // https://crbug.com/swiftshader/129
  if (use_swiftshader())
    return true;
  auto* connection = x11::Connection::Get();
  return vkGetPhysicalDeviceXcbPresentationSupportKHR(
      device, queue_family_index,
      connection->GetXlibDisplay().GetXcbConnection(),
      static_cast<xcb_visualid_t>(connection->default_root_visual().visual_id));
}

std::vector<const char*>
VulkanImplementationX11::GetRequiredDeviceExtensions() {
  std::vector<const char*> extensions = {};
  if (using_surface_)
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  return extensions;
}

std::vector<const char*>
VulkanImplementationX11::GetOptionalDeviceExtensions() {
  return {
      VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
      VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
      VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
  };
}

VkFence VulkanImplementationX11::CreateVkFenceForGpuFence(VkDevice vk_device) {
  NOTREACHED();
  return VK_NULL_HANDLE;
}

std::unique_ptr<gfx::GpuFence> VulkanImplementationX11::ExportVkFenceToGpuFence(
    VkDevice vk_device,
    VkFence vk_fence) {
  NOTREACHED();
  return nullptr;
}

VkSemaphore VulkanImplementationX11::CreateExternalSemaphore(
    VkDevice vk_device) {
  return CreateExternalVkSemaphore(
      vk_device, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
}

VkSemaphore VulkanImplementationX11::ImportSemaphoreHandle(
    VkDevice vk_device,
    SemaphoreHandle sync_handle) {
  return ImportVkSemaphoreHandle(vk_device, std::move(sync_handle));
}

SemaphoreHandle VulkanImplementationX11::GetSemaphoreHandle(
    VkDevice vk_device,
    VkSemaphore vk_semaphore) {
  return GetVkSemaphoreHandle(vk_device, vk_semaphore,
                              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
}

VkExternalMemoryHandleTypeFlagBits
VulkanImplementationX11::GetExternalImageHandleType() {
  return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
}

bool VulkanImplementationX11::CanImportGpuMemoryBuffer(
    gfx::GpuMemoryBufferType memory_buffer_type) {
  if (memory_buffer_type == gfx::GpuMemoryBufferType::NATIVE_PIXMAP)
    return true;
  return false;
}

std::unique_ptr<VulkanImage>
VulkanImplementationX11::CreateImageFromGpuMemoryHandle(
    VulkanDeviceQueue* device_queue,
    gfx::GpuMemoryBufferHandle gmb_handle,
    gfx::Size size,
    VkFormat vk_format) {
  constexpr auto kUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  auto tiling = gmb_handle.native_pixmap_handle.modifier ==
                        gfx::NativePixmapHandle::kNoModifier
                    ? VK_IMAGE_TILING_OPTIMAL
                    : VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
  return VulkanImage::CreateFromGpuMemoryBufferHandle(
      device_queue, std::move(gmb_handle), size, vk_format, kUsage, /*flags=*/0,
      tiling, VK_QUEUE_FAMILY_EXTERNAL);
}

}  // namespace gpu
