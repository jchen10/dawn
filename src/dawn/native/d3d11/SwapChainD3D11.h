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

#ifndef SRC_DAWN_NATIVE_D3D11_SWAPCHAIND3D11_H_
#define SRC_DAWN_NATIVE_D3D11_SWAPCHAIND3D11_H_

#include <vector>

#include "dawn/native/IntegerTypes.h"
#include "dawn/native/SwapChain.h"
#include "dawn/native/d3d/d3d_platform.h"

namespace dawn::native::d3d11 {

class Device;
class Texture;

class SwapChain final : public NewSwapChainBase {
  public:
    static ResultOrError<Ref<SwapChain>> Create(Device* device,
                                                Surface* surface,
                                                NewSwapChainBase* previousSwapChain,
                                                const SwapChainDescriptor* descriptor);

  private:
    ~SwapChain() override;

    void DestroyImpl() override;

    using NewSwapChainBase::NewSwapChainBase;
    MaybeError Initialize(NewSwapChainBase* previousSwapChain);

    struct Config {
        // Information that's passed to the D3D11 swapchain creation call.
        UINT bufferCount;
        UINT swapChainFlags;
        DXGI_FORMAT format;
        DXGI_USAGE usage;
    };

    // NewSwapChainBase implementation
    MaybeError PresentImpl() override;
    ResultOrError<Ref<TextureViewBase>> GetCurrentTextureViewImpl() override;
    void DetachFromSurfaceImpl() override;

    // Does the swapchain initialization steps assuming there is nothing we can reuse.
    MaybeError InitializeSwapChainFromScratch();
    // Does the swapchain initialization step of gathering the buffers.
    MaybeError CollectSwapChainBuffers();
    // Calls DetachFromSurface but also synchronously waits until all references to the
    // swapchain and buffers are removed, as that's a constraint for some DXGI operations.
    MaybeError DetachAndWaitForDeallocation();

    Config mConfig;

    ComPtr<IDXGISwapChain3> mDXGISwapChain;
    ComPtr<ID3D11Texture2D> mBuffer;
    ExecutionSerial mBufferLastUsedSerial;
    Ref<Texture> mApiTexture;
};

}  // namespace dawn::native::d3d11

#endif  // SRC_DAWN_NATIVE_D3D11_SWAPCHAIND3D11_H_
