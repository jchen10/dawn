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

// D3D12Backend.cpp: contains the definition of symbols exported by D3D12Backend.h so that they
// can be compiled twice: once export (shared library), once not exported (static library)

#include "dawn/native/D3DBackend.h"

#include <memory>
#include <utility>

#include "dawn/common/Log.h"
#include "dawn/common/Math.h"
#include "dawn/common/SwapChainUtils.h"
#include "dawn/native/d3d/DeviceD3D.h"
#include "dawn/native/d3d/ExternalImageDXGIImpl.h"
#include "dawn/native/d3d/Forward.h"

namespace dawn::native::d3d {

ExternalImageDescriptorDXGISharedHandle::ExternalImageDescriptorDXGISharedHandle()
    : ExternalImageDescriptor(ExternalImageType::DXGISharedHandle) {}

ExternalImageDXGI::ExternalImageDXGI(std::unique_ptr<ExternalImageDXGIImpl> impl)
    : mImpl(std::move(impl)) {
    ASSERT(mImpl != nullptr);
}

ExternalImageDXGI::~ExternalImageDXGI() = default;

bool ExternalImageDXGI::IsValid() const {
    return mImpl->IsValid();
}

WGPUTexture ExternalImageDXGI::ProduceTexture(
    WGPUDevice device,
    const ExternalImageDXGIBeginAccessDescriptor* descriptor) {
    return BeginAccess(descriptor);
}

WGPUTexture ExternalImageDXGI::BeginAccess(
    const ExternalImageDXGIBeginAccessDescriptor* descriptor) {
    if (!IsValid()) {
        dawn::ErrorLog() << "Cannot use external image after device destruction";
        return nullptr;
    }
    return mImpl->BeginAccess(descriptor);
}

void ExternalImageDXGI::EndAccess(WGPUTexture texture,
                                  ExternalImageDXGIFenceDescriptor* signalFence) {
    if (!IsValid()) {
        dawn::ErrorLog() << "Cannot use external image after device destruction";
        return;
    }
    mImpl->EndAccess(texture, signalFence);
}

// static
std::unique_ptr<ExternalImageDXGI> ExternalImageDXGI::Create(
    WGPUDevice device,
    const ExternalImageDescriptorDXGISharedHandle* descriptor) {
    Device* backendDevice = ToBackend(FromAPI(device));
    std::unique_ptr<ExternalImageDXGIImpl> impl =
        backendDevice->CreateExternalImageDXGIImpl(descriptor);
    if (!impl) {
        dawn::ErrorLog() << "Failed to create DXGI external image";
        return nullptr;
    }
    return std::unique_ptr<ExternalImageDXGI>(new ExternalImageDXGI(std::move(impl)));
}

AdapterDiscoveryOptions::AdapterDiscoveryOptions(WGPUBackendType type, ComPtr<IDXGIAdapter> adapter)
    : AdapterDiscoveryOptionsBase(type), dxgiAdapter(std::move(adapter)) {}

}  // namespace dawn::native::d3d
