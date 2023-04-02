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

#ifndef SRC_DAWN_NATIVE_D3D11_TEXTURED3D11_H_
#define SRC_DAWN_NATIVE_D3D11_TEXTURED3D11_H_

#include <array>
#include <optional>
#include <vector>

#include "dawn/native/DawnNative.h"
#include "dawn/native/Error.h"
#include "dawn/native/IntegerTypes.h"
#include "dawn/native/PassResourceUsage.h"
#include "dawn/native/Texture.h"
#include "dawn/native/d3d/d3d_platform.h"

namespace dawn::native::d3d11 {

class CommandRecordingContext;
class Device;

MaybeError ValidateD3D11TextureCanBeWrapped(ID3D11Resource* d3d11Resource,
                                            const TextureDescriptor* descriptor);
MaybeError ValidateTextureDescriptorCanBeWrapped(const TextureDescriptor* descriptor);
MaybeError ValidateD3D11VideoTextureCanBeShared(Device* device, DXGI_FORMAT textureFormat);

class Texture final : public TextureBase {
  public:
    static ResultOrError<Ref<Texture>> Create(Device* device, const TextureDescriptor* descriptor);
    static ResultOrError<Ref<Texture>> Create(Device* device,
                                              const TextureDescriptor* descriptor,
                                              ComPtr<ID3D11Resource> d3d11Texture);

    DXGI_FORMAT GetD3D11Format() const;
    ID3D11Resource* GetD3D11Resource() const;

    DXGI_FORMAT GetD3D11CopyableSubresourceFormat(Aspect aspect) const;
    // D3D11_RESOURCE_FLAGS GetD3D11ResourceFlags() const;

    D3D11_RENDER_TARGET_VIEW_DESC GetRTVDescriptor(const Format& format,
                                                   uint32_t mipLevel,
                                                   uint32_t baseSlice,
                                                   uint32_t sliceCount) const;
    D3D11_DEPTH_STENCIL_VIEW_DESC GetDSVDescriptor(uint32_t mipLevel,
                                                   uint32_t baseArrayLayer,
                                                   uint32_t layerCount,
                                                   Aspect aspects,
                                                   bool depthReadOnly,
                                                   bool stencilReadOnly) const;

    MaybeError EnsureSubresourceContentInitialized(CommandRecordingContext* commandContext,
                                                   const SubresourceRange& range);

    void TrackUsageAndTransitionNow(CommandRecordingContext* commandContext,
                                    wgpu::TextureUsage usage,
                                    const SubresourceRange& range) {}

  private:
    Texture(Device* device, const TextureDescriptor* descriptor, TextureState state);
    ~Texture() override;
    using TextureBase::TextureBase;

    MaybeError InitializeAsInternalTexture();
    MaybeError InitializeAsSwapChainTexture(ComPtr<ID3D11Resource> d3d11Texture);

    void SetLabelHelper(const char* prefix);

    // Dawn API
    void SetLabelImpl() override;
    void DestroyImpl() override;

    MaybeError ClearTexture(CommandRecordingContext* commandContext,
                            const SubresourceRange& range,
                            TextureBase::ClearValue clearValue);

    ComPtr<ID3D11Resource> mD3D11Resource;
};

class TextureView final : public TextureViewBase {
  public:
    static Ref<TextureView> Create(TextureBase* texture, const TextureViewDescriptor* descriptor);

    DXGI_FORMAT GetD3D11Format() const;
    ResultOrError<ID3D11ShaderResourceView*> GetD3D11ShaderResourceView() const;
    ResultOrError<ID3D11RenderTargetView*> GetD3D11RenderTargetView() const;
    ResultOrError<ID3D11DepthStencilView*> GetD3D11DepthStencilView(bool depthReadOnly,
                                                                    bool stencilReadOnly) const;
    ResultOrError<ID3D11UnorderedAccessView*> GetD3D11UnorderedAccessView() const;

  private:
    using TextureViewBase::TextureViewBase;

    ~TextureView() override = default;

    mutable std::optional<ComPtr<ID3D11ShaderResourceView>> mShaderResourceView;
    mutable std::optional<ComPtr<ID3D11RenderTargetView>> mRenderTargetView;
    mutable std::array<std::optional<ComPtr<ID3D11DepthStencilView>>, 4> mDepthStencilViews;
    mutable std::optional<ComPtr<ID3D11UnorderedAccessView>> mUnorderedAccessView;
};

}  // namespace dawn::native::d3d11

#endif  // SRC_DAWN_NATIVE_D3D11_TEXTURED3D11_H_
