// Copyright 2023 The Dawn Authors
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

#ifndef INCLUDE_DAWN_NATIVE_D3D11BACKEND_H_
#define INCLUDE_DAWN_NATIVE_D3D11BACKEND_H_

#include <d3d11.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

#include "dawn/native/D3DBackend.h"
#include "dawn/native/DawnNative.h"

struct ID3D12Device;

namespace dawn::native::d3d11 {

using d3d::ExternalImageDescriptorDXGISharedHandle;
using d3d::ExternalImageDXGI;
using d3d::ExternalImageDXGIBeginAccessDescriptor;
using d3d::ExternalImageDXGIFenceDescriptor;

struct AdapterDiscoveryOptions : public d3d::AdapterDiscoveryOptions {
    AdapterDiscoveryOptions() : d3d::AdapterDiscoveryOptions(WGPUBackendType_D3D11, nullptr) {}
    explicit AdapterDiscoveryOptions(Microsoft::WRL::ComPtr<IDXGIAdapter> adapter)
        : d3d::AdapterDiscoveryOptions(WGPUBackendType_D3D11, std::move(adapter)) {}
};

}  // namespace dawn::native::d3d11

#endif  // INCLUDE_DAWN_NATIVE_D3D11BACKEND_H_
