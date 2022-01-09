// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_memory_buffer_factory_dxgi.h"

#include <vector>

#include "base/memory/unsafe_shared_memory_region.h"
#include "base/trace_event/trace_event.h"
#include "gpu/command_buffer/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/common/dxgi_helpers.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gl/gl_angle_util_win.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_image_dxgi.h"

namespace gpu {

GpuMemoryBufferFactoryDXGI::GpuMemoryBufferFactoryDXGI() {
  DETACH_FROM_THREAD(thread_checker_);
}
GpuMemoryBufferFactoryDXGI::~GpuMemoryBufferFactoryDXGI() = default;

// TODO(crbug.com/1223490): Avoid the need for a separate D3D device here by
// sharing keyed mutex state between DXGI GMBs and D3D shared image backings.
Microsoft::WRL::ComPtr<ID3D11Device>
GpuMemoryBufferFactoryDXGI::GetOrCreateD3D11Device() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!d3d11_device_) {
    // Use same adapter as ANGLE device.
    auto angle_d3d11_device = gl::QueryD3D11DeviceObjectFromANGLE();
    if (!angle_d3d11_device) {
      DLOG(ERROR) << "Failed to get ANGLE D3D11 device";
      return nullptr;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> angle_dxgi_device;
    HRESULT hr = angle_d3d11_device.As(&angle_dxgi_device);
    CHECK(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter = nullptr;
    hr = FAILED(angle_dxgi_device->GetAdapter(&dxgi_adapter));
    if (FAILED(hr)) {
      DLOG(ERROR) << "GetAdapter failed with error 0x" << std::hex << hr;
      return nullptr;
    }

    // If adapter is not null, driver type must be D3D_DRIVER_TYPE_UNKNOWN
    // otherwise D3D11CreateDevice will return E_INVALIDARG.
    // See
    // https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice#return-value
    const D3D_DRIVER_TYPE driver_type =
        dxgi_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    // It's ok to use D3D11_CREATE_DEVICE_SINGLETHREADED because this device is
    // only ever used on the IO thread (verified by |thread_checker_|).
    const UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

    // Using D3D_FEATURE_LEVEL_11_1 is ok since we only support D3D11 when the
    // platform update containing DXGI 1.2 is present on Win7.
    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1};

    hr = D3D11CreateDevice(dxgi_adapter.Get(), driver_type,
                           /*Software=*/nullptr, flags, feature_levels,
                           base::size(feature_levels), D3D11_SDK_VERSION,
                           &d3d11_device_, /*pFeatureLevel=*/nullptr,
                           /*ppImmediateContext=*/nullptr);
    if (FAILED(hr)) {
      DLOG(ERROR) << "D3D11CreateDevice failed with error 0x" << std::hex << hr;
      return nullptr;
    }

    const char* kDebugName = "GPUIPC_GpuMemoryBufferFactoryDXGI";
    d3d11_device_->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(kDebugName),
                                  kDebugName);
  }
  DCHECK(d3d11_device_);
  return d3d11_device_;
}

gfx::GpuMemoryBufferHandle GpuMemoryBufferFactoryDXGI::CreateGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    const gfx::Size& framebuffer_size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    int client_id,
    SurfaceHandle surface_handle) {
  TRACE_EVENT0("gpu", "GpuMemoryBufferFactoryDXGI::CreateGpuMemoryBuffer");
  DCHECK_EQ(framebuffer_size, size);

  gfx::GpuMemoryBufferHandle handle;

  auto d3d11_device = GetOrCreateD3D11Device();
  if (!d3d11_device)
    return handle;

  DXGI_FORMAT dxgi_format;
  switch (format) {
    case gfx::BufferFormat::RGBA_8888:
    case gfx::BufferFormat::RGBX_8888:
      dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    default:
      NOTREACHED();
      return handle;
  }

  size_t buffer_size;
  if (!BufferSizeForBufferFormatChecked(size, format, &buffer_size))
    return handle;

  // We are binding as a shader resource and render target regardless of usage,
  // so make sure that the usage is one that we support.
  DCHECK(usage == gfx::BufferUsage::GPU_READ ||
         usage == gfx::BufferUsage::SCANOUT);

  D3D11_TEXTURE2D_DESC desc = {
      static_cast<UINT>(size.width()),
      static_cast<UINT>(size.height()),
      1,
      1,
      dxgi_format,
      {1, 0},
      D3D11_USAGE_DEFAULT,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
      0,
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
          D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX};

  Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d11_texture;

  if (FAILED(d3d11_device->CreateTexture2D(&desc, nullptr, &d3d11_texture)))
    return handle;

  Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_resource;
  if (FAILED(d3d11_texture.As(&dxgi_resource)))
    return handle;

  HANDLE texture_handle;
  if (FAILED(dxgi_resource->CreateSharedHandle(
          nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
          nullptr, &texture_handle)))
    return handle;

  handle.dxgi_handle.Set(texture_handle);
  handle.dxgi_token = gfx::DXGIHandleToken();
  handle.type = gfx::DXGI_SHARED_HANDLE;
  handle.id = id;

  return handle;
}

void GpuMemoryBufferFactoryDXGI::DestroyGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    int client_id) {}

bool GpuMemoryBufferFactoryDXGI::FillSharedMemoryRegionWithBufferContents(
    gfx::GpuMemoryBufferHandle buffer_handle,
    base::UnsafeSharedMemoryRegion shared_memory) {
  DCHECK_EQ(buffer_handle.type, gfx::GpuMemoryBufferType::DXGI_SHARED_HANDLE);

  auto d3d11_device = GetOrCreateD3D11Device();
  if (!d3d11_device)
    return false;

  return CopyDXGIBufferToShMem(buffer_handle.dxgi_handle.Get(),
                               std::move(shared_memory), d3d11_device.Get(),
                               &staging_texture_);
}

ImageFactory* GpuMemoryBufferFactoryDXGI::AsImageFactory() {
  return this;
}

scoped_refptr<gl::GLImage>
GpuMemoryBufferFactoryDXGI::CreateImageForGpuMemoryBuffer(
    gfx::GpuMemoryBufferHandle handle,
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferPlane plane,
    int client_id,
    SurfaceHandle surface_handle) {
  if (handle.type != gfx::DXGI_SHARED_HANDLE)
    return nullptr;
  if (plane != gfx::BufferPlane::DEFAULT)
    return nullptr;
  // Transfer ownership of handle to GLImageDXGI.
  auto image = base::MakeRefCounted<gl::GLImageDXGI>(size, nullptr);
  if (!image->InitializeHandle(std::move(handle.dxgi_handle), 0, format))
    return nullptr;
  return image;
}

unsigned GpuMemoryBufferFactoryDXGI::RequiredTextureType() {
  return GL_TEXTURE_2D;
}

bool GpuMemoryBufferFactoryDXGI::SupportsFormatRGB() {
  return true;
}
}  // namespace gpu
