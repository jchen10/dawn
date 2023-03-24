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

#include "dawn/native/d3d11/TextureD3D11.h"

#include <algorithm>
#include <string>
#include <utility>

#include "dawn/common/Constants.h"
#include "dawn/common/Math.h"
#include "dawn/native/DynamicUploader.h"
#include "dawn/native/EnumMaskIterator.h"
#include "dawn/native/IntegerTypes.h"
#include "dawn/native/ToBackend.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d/UtilsD3D.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/native/d3d11/Forward.h"

namespace dawn::native::d3d11 {
namespace {

bool IsDepthOrStencilFormat(wgpu::TextureFormat format) {
    switch (format) {
        case wgpu::TextureFormat::Depth24Plus:
        case wgpu::TextureFormat::Depth24PlusStencil8:
        case wgpu::TextureFormat::Depth32Float:
        case wgpu::TextureFormat::Depth32FloatStencil8:
        case wgpu::TextureFormat::Stencil8:
            return true;
        default:
            return false;
    }
}

UINT D3D11TextureBindFlags(wgpu::TextureUsage usage, wgpu::TextureFormat format) {
    bool isDepthOrStencilFormat = IsDepthOrStencilFormat(format);
    UINT bindFlags = 0;
    if (usage & wgpu::TextureUsage::TextureBinding) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (usage & wgpu::TextureUsage::StorageBinding) {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (usage & wgpu::TextureUsage::RenderAttachment) {
        bindFlags |= isDepthOrStencilFormat ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
    }
    return bindFlags;
}

}  // namespace

// static
ResultOrError<Ref<Texture>> Texture::Create(Device* device, const TextureDescriptor* descriptor) {
    Ref<Texture> texture = AcquireRef(new Texture(device, descriptor, TextureState::OwnedInternal));

    DAWN_INVALID_IF(texture->GetFormat().IsMultiPlanar(),
                    "Cannot create a multi-planar formatted texture directly");

    DAWN_TRY(texture->InitializeAsInternalTexture());
    return std::move(texture);
}

// static
ResultOrError<Ref<Texture>> Texture::Create(Device* device,
                                            const TextureDescriptor* descriptor,
                                            ComPtr<ID3D11Resource> d3d11Texture) {
    Ref<Texture> dawnTexture =
        AcquireRef(new Texture(device, descriptor, TextureState::OwnedExternal));
    DAWN_TRY(dawnTexture->InitializeAsSwapChainTexture(std::move(d3d11Texture)));
    return std::move(dawnTexture);
}

MaybeError Texture::InitializeAsInternalTexture() {
    Device* device = ToBackend(GetDevice());

    const bool needsTypelessFormat = false;
    DXGI_FORMAT dxgiFormat = needsTypelessFormat
                                 ? d3d::DXGITypelessTextureFormat(GetFormat().format)
                                 : d3d::DXGITextureFormat(GetFormat().format);

    switch (GetDimension()) {
        case wgpu::TextureDimension::e1D: {
            D3D11_TEXTURE1D_DESC textureDescriptor;
            textureDescriptor.Width = GetSize().width;
            textureDescriptor.MipLevels = static_cast<UINT16>(GetNumMipLevels());
            textureDescriptor.ArraySize = 1;
            textureDescriptor.Format = dxgiFormat;
            textureDescriptor.Usage = D3D11_USAGE_DEFAULT;
            textureDescriptor.BindFlags = D3D11TextureBindFlags(GetUsage(), GetFormat().format);
            textureDescriptor.CPUAccessFlags = 0;
            textureDescriptor.MiscFlags = 0;
            ComPtr<ID3D11Texture1D> d3d11Texture1D;
            DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateTexture1D(
                                      &textureDescriptor, nullptr, &d3d11Texture1D),
                                  "D3D11 create texture1d"));
            mD3D11Resource = std::move(d3d11Texture1D);
            break;
        }
        case wgpu::TextureDimension::e2D: {
            D3D11_TEXTURE2D_DESC textureDescriptor;
            textureDescriptor.Width = GetSize().width;
            textureDescriptor.Height = GetSize().height;
            textureDescriptor.MipLevels = static_cast<UINT16>(GetNumMipLevels());
            textureDescriptor.ArraySize = GetArrayLayers();
            textureDescriptor.Format = dxgiFormat;
            textureDescriptor.SampleDesc.Count = GetSampleCount();
            textureDescriptor.SampleDesc.Quality = 0;
            textureDescriptor.Usage = D3D11_USAGE_DEFAULT;
            textureDescriptor.BindFlags = D3D11TextureBindFlags(GetUsage(), GetFormat().format);
            textureDescriptor.CPUAccessFlags = 0;
            textureDescriptor.MiscFlags = 0;
            if (GetArrayLayers() >= 6) {
                // Texture layers are more than 6. It can be used as a cube map.
                textureDescriptor.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
            }
            ComPtr<ID3D11Texture2D> d3d11Texture2D;
            DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateTexture2D(
                                      &textureDescriptor, nullptr, &d3d11Texture2D),
                                  "D3D11 create texture2d"));
            mD3D11Resource = std::move(d3d11Texture2D);
            break;
        }
        case wgpu::TextureDimension::e3D: {
            D3D11_TEXTURE3D_DESC textureDescriptor;
            textureDescriptor.Width = GetSize().width;
            textureDescriptor.Height = GetSize().height;
            textureDescriptor.Depth = GetSize().depthOrArrayLayers;
            textureDescriptor.MipLevels = static_cast<UINT16>(GetNumMipLevels());
            textureDescriptor.Format = dxgiFormat;
            textureDescriptor.Usage = D3D11_USAGE_DEFAULT;
            textureDescriptor.BindFlags = D3D11TextureBindFlags(GetUsage(), GetFormat().format);
            textureDescriptor.CPUAccessFlags = 0;
            textureDescriptor.MiscFlags = 0;
            ComPtr<ID3D11Texture3D> d3d11Texture3D;
            DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateTexture3D(
                                      &textureDescriptor, nullptr, &d3d11Texture3D),
                                  "D3D11 create texture3d"));
            mD3D11Resource = std::move(d3d11Texture3D);
            break;
        }
    }

    SetLabelImpl();

    if (device->IsToggleEnabled(Toggle::NonzeroClearResourcesOnCreationForTesting)) {
        CommandRecordingContext* commandContext;
        DAWN_TRY_ASSIGN(commandContext, device->GetPendingCommandContext());
        DAWN_TRY(
            ClearTexture(commandContext, GetAllSubresources(), TextureBase::ClearValue::NonZero));
    }

    return {};
}

MaybeError Texture::InitializeAsSwapChainTexture(ComPtr<ID3D11Resource> d3d11Texture) {
    mD3D11Resource = std::move(d3d11Texture);
    SetLabelHelper("Dawn_SwapChainTexture");
    return {};
}

Texture::Texture(Device* device, const TextureDescriptor* descriptor, TextureState state)
    : TextureBase(device, descriptor, state) {}

Texture::~Texture() {}

void Texture::DestroyImpl() {
    TextureBase::DestroyImpl();
    mD3D11Resource = nullptr;
}

DXGI_FORMAT Texture::GetD3D11Format() const {
    return d3d::DXGITextureFormat(GetFormat().format);
}

ID3D11Resource* Texture::GetD3D11Resource() const {
    return mD3D11Resource.Get();
}

DXGI_FORMAT Texture::GetD3D11CopyableSubresourceFormat(Aspect aspect) const {
    ASSERT(GetFormat().aspects & aspect);

    switch (GetFormat().format) {
        case wgpu::TextureFormat::Depth24PlusStencil8:
        case wgpu::TextureFormat::Depth32FloatStencil8:
        case wgpu::TextureFormat::Stencil8:
            switch (aspect) {
                case Aspect::Depth:
                    return DXGI_FORMAT_R32_FLOAT;
                case Aspect::Stencil:
                    return DXGI_FORMAT_R8_UINT;
                default:
                    UNREACHABLE();
            }
        default:
            ASSERT(HasOneBit(GetFormat().aspects));
            return GetD3D11Format();
    }
}

D3D11_RENDER_TARGET_VIEW_DESC Texture::GetRTVDescriptor(const Format& format,
                                                        uint32_t mipLevel,
                                                        uint32_t baseSlice,
                                                        uint32_t sliceCount) const {
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
    rtvDesc.Format = d3d::DXGITextureFormat(format.format);
    if (IsMultisampledTexture()) {
        ASSERT(GetDimension() == wgpu::TextureDimension::e2D);
        ASSERT(GetNumMipLevels() == 1);
        ASSERT(sliceCount == 1);
        ASSERT(baseSlice == 0);
        ASSERT(mipLevel == 0);
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        return rtvDesc;
    }
    switch (GetDimension()) {
        case wgpu::TextureDimension::e2D:
            // Currently we always use D3D11_TEX2D_ARRAY_RTV because we cannot specify base
            // array layer and layer count in D3D11_TEX2D_RTV. For 2D texture views, we treat
            // them as 1-layer 2D array textures. (Just like how we treat SRVs)
            // https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/ns-d3d11-d3d11_tex2d_rtv
            // https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/ns-d3d11-d3d11_tex2d_array
            // _rtv
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = mipLevel;
            rtvDesc.Texture2DArray.FirstArraySlice = baseSlice;
            rtvDesc.Texture2DArray.ArraySize = sliceCount;
            break;
        case wgpu::TextureDimension::e3D:
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            rtvDesc.Texture3D.MipSlice = mipLevel;
            rtvDesc.Texture3D.FirstWSlice = baseSlice;
            rtvDesc.Texture3D.WSize = sliceCount;
            break;
        case wgpu::TextureDimension::e1D:
            UNREACHABLE();
            break;
    }
    return rtvDesc;
}

D3D11_DEPTH_STENCIL_VIEW_DESC Texture::GetDSVDescriptor(uint32_t mipLevel,
                                                        uint32_t baseArrayLayer,
                                                        uint32_t layerCount,
                                                        Aspect aspects,
                                                        bool depthReadOnly,
                                                        bool stencilReadOnly) const {
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;

    dsvDesc.Format = GetD3D11Format();
    dsvDesc.Flags = 0;
    if (depthReadOnly && aspects & Aspect::Depth) {
        dsvDesc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
    }
    if (stencilReadOnly && aspects & Aspect::Stencil) {
        dsvDesc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
    }

    if (IsMultisampledTexture()) {
        ASSERT(GetNumMipLevels() == 1);
        ASSERT(layerCount == 1);
        ASSERT(baseArrayLayer == 0);
        ASSERT(mipLevel == 0);
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    } else {
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = baseArrayLayer;
        dsvDesc.Texture2DArray.ArraySize = layerCount;
        dsvDesc.Texture2DArray.MipSlice = mipLevel;
    }

    return dsvDesc;
}

MaybeError Texture::ClearTexture(CommandRecordingContext* commandContext,
                                 const SubresourceRange& range,
                                 TextureBase::ClearValue clearValue) {
    return DAWN_UNIMPLEMENTED_ERROR("ClearTexture");
}

void Texture::SetLabelHelper(const char* prefix) {
    std::string name(prefix);
    name += " " + GetLabel();
    mD3D11Resource->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
}

void Texture::SetLabelImpl() {
    SetLabelHelper("Dawn_InternalTexture");
}

MaybeError Texture::EnsureSubresourceContentInitialized(CommandRecordingContext* commandContext,
                                                        const SubresourceRange& range) {
    if (!ToBackend(GetDevice())->IsToggleEnabled(Toggle::LazyClearResourceOnFirstUse)) {
        return {};
    }
    if (!IsSubresourceContentInitialized(range)) {
        // If subresource has not been initialized, clear it to black as it could contain
        // dirty bits from recycled memory
        DAWN_TRY(ClearTexture(commandContext, range, TextureBase::ClearValue::Zero));
    }
    return {};
}

// static
Ref<TextureView> TextureView::Create(TextureBase* texture,
                                     const TextureViewDescriptor* descriptor) {
    return AcquireRef(new TextureView(texture, descriptor));
}

DXGI_FORMAT TextureView::GetD3D11Format() const {
    return d3d::DXGITextureFormat(GetFormat().format);
}

ResultOrError<ID3D11ShaderResourceView*> TextureView::GetD3D11ShaderResourceView() const {
    if (!mShaderResourceView.has_value()) {
        Device* device = ToBackend(GetDevice());
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = d3d::DXGITextureFormat(GetFormat().format);

        const Format& textureFormat = GetTexture()->GetFormat();
        // TODO(dawn:1705): share below code with D3D12?
        if (textureFormat.HasDepthOrStencil()) {
            // Configure the SRV descriptor to reinterpret the texture allocated as
            // TYPELESS as a single-plane shader-accessible view.
            switch (textureFormat.format) {
                case wgpu::TextureFormat::Depth32Float:
                case wgpu::TextureFormat::Depth24Plus:
                    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
                    break;
                case wgpu::TextureFormat::Depth16Unorm:
                    srvDesc.Format = DXGI_FORMAT_R16_UNORM;
                    break;
                case wgpu::TextureFormat::Stencil8: {
                    Aspect aspects = GetAspects();
                    ASSERT(aspects != Aspect::None);
                    if (!HasZeroOrOneBits(aspects)) {
                        // A single aspect is not selected. The texture view must not be
                        // sampled.
                        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                        break;
                    }
                    switch (aspects) {
                        case Aspect::Depth:
                            srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                            break;
                        case Aspect::Stencil:
                            srvDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
                            break;
                        default:
                            UNREACHABLE();
                            break;
                    }
                    break;
                }
                case wgpu::TextureFormat::Depth24PlusStencil8:
                case wgpu::TextureFormat::Depth32FloatStencil8: {
                    Aspect aspects = GetAspects();
                    ASSERT(aspects != Aspect::None);
                    if (!HasZeroOrOneBits(aspects)) {
                        // A single aspect is not selected. The texture view must not be
                        // sampled.
                        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                        break;
                    }
                    switch (aspects) {
                        case Aspect::Depth:
                            srvDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                            break;
                        case Aspect::Stencil:
                            srvDesc.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
                            break;
                        default:
                            UNREACHABLE();
                            break;
                    }
                    break;
                }
                default:
                    UNREACHABLE();
                    break;
            }
        }

        // Per plane view formats must have the plane slice number be the index of the plane in
        // the array of textures.
        if (GetTexture()->GetFormat().IsMultiPlanar()) {
            Aspect planeAspects = GetAspects();
            srvDesc.Format = d3d::DXGITextureFormat(
                GetTexture()->GetFormat().GetAspectInfo(planeAspects).format);
        }

        // Currently we always use D3D11_TEX2D_ARRAY_SRV because we cannot specify base array
        // layer and layer count in D3D11_TEX2D_SRV. For 2D texture views, we treat them as
        // 1-layer 2D array textures. Multisampled textures may only be one array layer, so we
        // use D3D11_SRV_DIMENSION_TEXTURE2DMS.
        // https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/ns-d3d11-d3d11_tex2d_srv
        // https://docs.microsoft.com/en-us/windows/desktop/api/d3d11/ns-d3d11-d3d11_tex2d_array_srv
        if (GetTexture()->IsMultisampledTexture()) {
            switch (GetDimension()) {
                case wgpu::TextureViewDimension::e2DArray:
                    ASSERT(GetTexture()->GetArrayLayers() == 1);
                    [[fallthrough]];
                case wgpu::TextureViewDimension::e2D:
                    ASSERT(GetTexture()->GetDimension() == wgpu::TextureDimension::e2D);
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                    break;

                default:
                    UNREACHABLE();
            }
        } else {
            switch (GetDimension()) {
                case wgpu::TextureViewDimension::e1D:
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                    srvDesc.Texture1D.MipLevels = GetLevelCount();
                    srvDesc.Texture1D.MostDetailedMip = GetBaseMipLevel();
                    break;

                case wgpu::TextureViewDimension::e2D:
                case wgpu::TextureViewDimension::e2DArray:
                    ASSERT(GetTexture()->GetDimension() == wgpu::TextureDimension::e2D);
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvDesc.Texture2DArray.ArraySize = GetLayerCount();
                    srvDesc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
                    srvDesc.Texture2DArray.MipLevels = GetLevelCount();
                    srvDesc.Texture2DArray.MostDetailedMip = GetBaseMipLevel();
                    break;
                case wgpu::TextureViewDimension::Cube:
                case wgpu::TextureViewDimension::CubeArray:
                    ASSERT(GetTexture()->GetDimension() == wgpu::TextureDimension::e2D);
                    ASSERT(GetLayerCount() % 6 == 0);
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                    srvDesc.TextureCubeArray.First2DArrayFace = GetBaseArrayLayer();
                    srvDesc.TextureCubeArray.NumCubes = GetLayerCount() / 6;
                    srvDesc.TextureCubeArray.MipLevels = GetLevelCount();
                    srvDesc.TextureCubeArray.MostDetailedMip = GetBaseMipLevel();
                    break;
                case wgpu::TextureViewDimension::e3D:
                    ASSERT(GetTexture()->GetDimension() == wgpu::TextureDimension::e3D);
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                    srvDesc.Texture3D.MostDetailedMip = GetBaseMipLevel();
                    srvDesc.Texture3D.MipLevels = GetLevelCount();
                    break;

                case wgpu::TextureViewDimension::Undefined:
                    UNREACHABLE();
            }
        }

        mShaderResourceView.emplace();
        ComPtr<ID3D11ShaderResourceView>& srv = mShaderResourceView.value();
        DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateShaderResourceView(
                                  ToBackend(GetTexture())->GetD3D11Resource(), &srvDesc, &srv),
                              "CreateShaderResourceView"));
    }

    return mShaderResourceView.value().Get();
}

ResultOrError<ID3D11RenderTargetView*> TextureView::GetD3D11RenderTargetView() const {
    if (!mRenderTargetView.has_value()) {
        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc =
            ToBackend(GetTexture())
                ->GetRTVDescriptor(GetFormat(), GetBaseMipLevel(), GetBaseArrayLayer(),
                                   GetLayerCount());
        mRenderTargetView.emplace();
        ComPtr<ID3D11RenderTargetView>& rtv = mRenderTargetView.value();
        DAWN_TRY(CheckHRESULT(ToBackend(GetDevice())
                                  ->GetD3D11Device()
                                  ->CreateRenderTargetView(
                                      ToBackend(GetTexture())->GetD3D11Resource(), &rtvDesc, &rtv),
                              "CreateRenderTargetView"));
    }
    return mRenderTargetView.value().Get();
}

ResultOrError<ID3D11DepthStencilView*> TextureView::GetD3D11DepthStencilView(
    bool depthReadOnly,
    bool stencilReadOnly) const {
    size_t i = (depthReadOnly ? 1 : 0) + (stencilReadOnly ? 2 : 0);
    if (!mDepthStencilViews[i].has_value()) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc =
            ToBackend(GetTexture())
                ->GetDSVDescriptor(GetBaseMipLevel(), GetBaseArrayLayer(), GetLayerCount(),
                                   GetAspects(), depthReadOnly, stencilReadOnly);
        mDepthStencilViews[i].emplace();
        ComPtr<ID3D11DepthStencilView>& dsv = mDepthStencilViews[i].value();
        DAWN_TRY(CheckHRESULT(ToBackend(GetDevice())
                                  ->GetD3D11Device()
                                  ->CreateDepthStencilView(
                                      ToBackend(GetTexture())->GetD3D11Resource(), &dsvDesc, &dsv),
                              "CreateDepthStencilView"));
    }
    return mDepthStencilViews[i].value().Get();
}

ResultOrError<ID3D11UnorderedAccessView*> TextureView::GetD3D11UnorderedAccessView() const {
    if (!mUnorderedAccessView.has_value()) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        uavDesc.Format = GetD3D11Format();

        ASSERT(!GetTexture()->IsMultisampledTexture());
        switch (GetDimension()) {
            case wgpu::TextureViewDimension::e1D:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
                uavDesc.Texture1D.MipSlice = GetBaseMipLevel();
                break;
            case wgpu::TextureViewDimension::e2D:
            case wgpu::TextureViewDimension::e2DArray:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                uavDesc.Texture2DArray.FirstArraySlice = GetBaseArrayLayer();
                uavDesc.Texture2DArray.ArraySize = GetLayerCount();
                uavDesc.Texture2DArray.MipSlice = GetBaseMipLevel();
                break;
            case wgpu::TextureViewDimension::e3D:
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                uavDesc.Texture3D.FirstWSlice = 0;
                uavDesc.Texture3D.WSize = GetTexture()->GetDepth() >> GetBaseMipLevel();
                uavDesc.Texture3D.MipSlice = GetBaseMipLevel();
                break;
            // Cube and Cubemap can't be used as storage texture. So there is no need to create UAV
            // descriptor for them.
            case wgpu::TextureViewDimension::Cube:
            case wgpu::TextureViewDimension::CubeArray:
            case wgpu::TextureViewDimension::Undefined:
                UNREACHABLE();
        }
        mUnorderedAccessView.emplace();
        ComPtr<ID3D11UnorderedAccessView>& uav = mUnorderedAccessView.value();
        DAWN_TRY(CheckHRESULT(ToBackend(GetDevice())
                                  ->GetD3D11Device()
                                  ->CreateUnorderedAccessView(
                                      ToBackend(GetTexture())->GetD3D11Resource(), &uavDesc, &uav),
                              "CreateUnorderedAccessView"));
    }
    return mUnorderedAccessView.value().Get();
}

}  // namespace dawn::native::d3d11
