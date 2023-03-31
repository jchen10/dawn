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

#include "dawn/native/d3d11/ExternalImageDXGIImplD3D11.h"

#include <utility>
#include <vector>

#include "dawn/common/Log.h"
#include "dawn/native/D3D11Backend.h"
#include "dawn/native/DawnNative.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/native/d3d11/FenceD3D11.h"
#include "dawn/native/d3d11/Forward.h"

namespace dawn::native::d3d11 {

ExternalImageDXGIImpl::ExternalImageDXGIImpl(Device* backendDevice,
                                             Microsoft::WRL::ComPtr<ID3D11Resource> d3d11Resource,
                                             const TextureDescriptor* textureDescriptor,
                                             bool useFenceSynchronization)
    : Base(backendDevice, textureDescriptor, useFenceSynchronization),
      mD3D11Resource(std::move(d3d11Resource)) {
    ASSERT(mD3D11Resource != nullptr);
}

ExternalImageDXGIImpl::~ExternalImageDXGIImpl() = default;

void ExternalImageDXGIImpl::Destroy() {
    Base::Destroy();
    mD3D11Resource = nullptr;
}

WGPUTexture ExternalImageDXGIImpl::BeginAccess(
    const ExternalImageDXGIBeginAccessDescriptor* descriptor) {
    ASSERT(mBackendDevice != nullptr);
    ASSERT(descriptor != nullptr);
    // Ensure the texture usage is allowed
    if (!IsSubset(descriptor->usage, static_cast<WGPUTextureUsageFlags>(mUsage))) {
        dawn::ErrorLog() << "Texture usage is not valid for external image";
        return nullptr;
    }

    TextureDescriptor textureDescriptor = {};
    textureDescriptor.usage = static_cast<wgpu::TextureUsage>(descriptor->usage);
    textureDescriptor.dimension = mDimension;
    textureDescriptor.size = {mSize.width, mSize.height, mSize.depthOrArrayLayers};
    textureDescriptor.format = mFormat;
    textureDescriptor.mipLevelCount = mMipLevelCount;
    textureDescriptor.sampleCount = mSampleCount;
    textureDescriptor.viewFormats = mViewFormats.data();
    textureDescriptor.viewFormatCount = static_cast<uint32_t>(mViewFormats.size());

    DawnTextureInternalUsageDescriptor internalDesc = {};
    if (mUsageInternal != wgpu::TextureUsage::None) {
        textureDescriptor.nextInChain = &internalDesc;
        internalDesc.internalUsage = mUsageInternal;
        internalDesc.sType = wgpu::SType::DawnTextureInternalUsageDescriptor;
    }

    std::vector<Ref<Fence>> waitFences;
    if (mUseFenceSynchronization) {
        for (const ExternalImageDXGIFenceDescriptor& fenceDescriptor : descriptor->waitFences) {
            ASSERT(fenceDescriptor.fenceHandle != nullptr);
            // TODO(sunnyps): Use a fence cache instead of re-importing fences on each BeginAccess.
            Ref<Fence> fence;
            if (mBackendDevice->ConsumedError(
                    Fence::CreateFromHandle(ToBackend(mBackendDevice)->GetD3D11Device5(),
                                            fenceDescriptor.fenceHandle,
                                            fenceDescriptor.fenceValue),
                    &fence)) {
                dawn::ErrorLog() << "Unable to create D3D11 fence for external image";
                return nullptr;
            }
            waitFences.push_back(std::move(fence));
        }
    }

    Ref<TextureBase> texture =
        ToBackend(mBackendDevice)
            ->CreateD3D11ExternalTexture(&textureDescriptor, mD3D11Resource, std::move(waitFences),
                                         descriptor->isSwapChainTexture, descriptor->isInitialized);
    return ToAPI(texture.Detach());
}

void ExternalImageDXGIImpl::EndAccess(WGPUTexture texture,
                                      ExternalImageDXGIFenceDescriptor* signalFence) {
    ASSERT(signalFence != nullptr);

    Texture* backendTexture = ToBackend(FromAPI(texture));
    ASSERT(backendTexture != nullptr);

    if (mUseFenceSynchronization) {
        ExecutionSerial fenceValue;
        if (mBackendDevice->ConsumedError(backendTexture->EndAccess(), &fenceValue)) {
            dawn::ErrorLog() << "D3D12 fence end access failed";
            return;
        }
        signalFence->fenceHandle = ToBackend(mBackendDevice)->GetFenceHandle();
        signalFence->fenceValue = static_cast<uint64_t>(fenceValue);
    }
}

}  // namespace dawn::native::d3d11
