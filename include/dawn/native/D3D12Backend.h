// Copyright 2018 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDE_DAWN_NATIVE_D3D12BACKEND_H_
#define INCLUDE_DAWN_NATIVE_D3D12BACKEND_H_

#include <DXGI1_4.h>
#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

#include "dawn/dawn_wsi.h"
#include "dawn/native/D3DBackend.h"
#include "dawn/native/DawnNative.h"

struct ID3D12Device;
struct ID3D12Resource;

namespace dawn::native::d3d12 {

using d3d::ExternalImageDescriptorDXGISharedHandle;
using d3d::ExternalImageDXGI;
using d3d::ExternalImageDXGIBeginAccessDescriptor;
using d3d::ExternalImageDXGIFenceDescriptor;

class D3D11on12ResourceCache;
class Device;
class ExternalImageDXGIImpl;

DAWN_NATIVE_EXPORT Microsoft::WRL::ComPtr<ID3D12Device> GetD3D12Device(WGPUDevice device);
DAWN_NATIVE_EXPORT DawnSwapChainImplementation CreateNativeSwapChainImpl(WGPUDevice device,
                                                                         HWND window);
DAWN_NATIVE_EXPORT WGPUTextureFormat
GetNativeSwapChainPreferredFormat(const DawnSwapChainImplementation* swapChain);

enum MemorySegment {
    Local,
    NonLocal,
};

DAWN_NATIVE_EXPORT uint64_t SetExternalMemoryReservation(WGPUDevice device,
                                                         uint64_t requestedReservationSize,
                                                         MemorySegment memorySegment);

// Keyed mutex acquire/release uses a fixed key of 0 to match Chromium behavior.
constexpr UINT64 kDXGIKeyedMutexAcquireReleaseKey = 0;

// TODO(dawn:576): Remove after changing Chromium code to use the new struct name.
struct DAWN_NATIVE_EXPORT ExternalImageAccessDescriptorDXGIKeyedMutex
    : ExternalImageDXGIBeginAccessDescriptor {
  public:
    // TODO(chromium:1241533): Remove deprecated keyed mutex params after removing associated
    // code from Chromium - we use a fixed key of 0 for acquire and release everywhere now.
    uint64_t acquireMutexKey;
    uint64_t releaseMutexKey;
};

struct AdapterDiscoveryOptions : public d3d::AdapterDiscoveryOptions {
    AdapterDiscoveryOptions() : d3d::AdapterDiscoveryOptions(WGPUBackendType_D3D12, nullptr) {}
    explicit AdapterDiscoveryOptions(Microsoft::WRL::ComPtr<IDXGIAdapter> adapter)
        : d3d::AdapterDiscoveryOptions(WGPUBackendType_D3D12, std::move(adapter)) {}
};

}  // namespace dawn::native::d3d12

#endif  // INCLUDE_DAWN_NATIVE_D3D12BACKEND_H_
