// Copyright 2022 The Dawn Authors
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

#include <vector>

#include "dawn/tests/DawnTest.h"
#include "dawn/utils/ComboRenderPipelineDescriptor.h"
#include "dawn/utils/WGPUHelpers.h"

namespace {

wgpu::Texture Create2DTexture(wgpu::Device device,
                              uint32_t width,
                              uint32_t height,
                              wgpu::TextureFormat format,
                              wgpu::TextureUsage usage) {
    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size.width = width;
    descriptor.size.height = height;
    descriptor.size.depthOrArrayLayers = 1;
    descriptor.sampleCount = 1;
    descriptor.format = format;
    descriptor.mipLevelCount = 1;
    descriptor.usage = usage;
    return device.CreateTexture(&descriptor);
}

static constexpr uint32_t kWidth = 4;
static constexpr uint32_t kHeight = 4;

std::array<std::array<utils::RGBA8, 4>, 4> kDefaultExpectedRGBA = {
    std::array<utils::RGBA8, 4>(
        {utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 4>(
        {utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 4>(
        {utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue}),
    std::array<utils::RGBA8, 4>(
        {utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue})};

std::array<std::array<utils::RGBA8, 2>, 2> kDownScaledExpectedRGBA = {
    std::array<utils::RGBA8, 2>({utils::RGBA8::kBlack, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 2>({utils::RGBA8::kGreen, utils::RGBA8::kBlue})};

std::array<std::array<utils::RGBA8, 8>, 8> kUpScaledExpectedRGBA = {
    std::array<utils::RGBA8, 8>({utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kBlack,
                                 utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed,
                                 utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kBlack,
                                 utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed,
                                 utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kBlack,
                                 utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed,
                                 utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kBlack, utils::RGBA8::kBlack, utils::RGBA8::kBlack,
                                 utils::RGBA8::kBlack, utils::RGBA8::kRed, utils::RGBA8::kRed,
                                 utils::RGBA8::kRed, utils::RGBA8::kRed}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kGreen,
                                 utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue,
                                 utils::RGBA8::kBlue, utils::RGBA8::kBlue}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kGreen,
                                 utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue,
                                 utils::RGBA8::kBlue, utils::RGBA8::kBlue}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kGreen,
                                 utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue,
                                 utils::RGBA8::kBlue, utils::RGBA8::kBlue}),
    std::array<utils::RGBA8, 8>({utils::RGBA8::kGreen, utils::RGBA8::kGreen, utils::RGBA8::kGreen,
                                 utils::RGBA8::kGreen, utils::RGBA8::kBlue, utils::RGBA8::kBlue,
                                 utils::RGBA8::kBlue, utils::RGBA8::kBlue})};

template <typename Parent>
class CopyExternalTextureForBrowserTests : public Parent {
  protected:
    wgpu::ExternalTexture CreateDefaultExternalTexture() {
        // y plane
        wgpu::TextureDescriptor externalTexturePlane0Desc = {};
        externalTexturePlane0Desc.size = {kWidth, kHeight, 1};
        externalTexturePlane0Desc.usage = wgpu::TextureUsage::TextureBinding |
                                          wgpu::TextureUsage::CopyDst |
                                          wgpu::TextureUsage::RenderAttachment;
        externalTexturePlane0Desc.format = wgpu::TextureFormat::R8Unorm;
        wgpu::Texture externalTexturePlane0 =
            this->device.CreateTexture(&externalTexturePlane0Desc);

        // The value Ref to ExternalTextureTest.cpp:
        //  {0.0, .5, .5, utils::RGBA8::kBlack, 0.0f},
        //  {0.2126, 0.4172, 1.0, utils::RGBA8::kRed, 1.0f},
        //  {0.7152, 0.1402, 0.0175, utils::RGBA8::kGreen, 0.0f},
        //  {0.0722, 1.0, 0.4937, utils::RGBA8::kBlue, 0.0f},
        wgpu::ImageCopyTexture plane0 = {};
        plane0.texture = externalTexturePlane0;
        std::array<uint8_t, 16> yPlaneData = {0,   0,   54, 54, 0,   0,   54, 54,
                                              182, 182, 18, 18, 182, 182, 18, 18};

        wgpu::TextureDataLayout externalTexturePlane0DataLayout = {};
        externalTexturePlane0DataLayout.bytesPerRow = 4;

        this->queue.WriteTexture(&plane0, yPlaneData.data(),
                                 yPlaneData.size() * sizeof(yPlaneData[0]),
                                 &externalTexturePlane0DataLayout, &externalTexturePlane0Desc.size);

        // uv plane
        wgpu::TextureDescriptor externalTexturePlane1Desc = {};
        externalTexturePlane1Desc.size = {kWidth / 2, kHeight / 2, 1};
        externalTexturePlane1Desc.usage = wgpu::TextureUsage::TextureBinding |
                                          wgpu::TextureUsage::CopyDst |
                                          wgpu::TextureUsage::RenderAttachment;
        externalTexturePlane1Desc.format = wgpu::TextureFormat::RG8Unorm;
        wgpu::Texture externalTexturePlane1 =
            this->device.CreateTexture(&externalTexturePlane1Desc);

        wgpu::ImageCopyTexture plane1 = {};
        plane1.texture = externalTexturePlane1;
        std::array<uint8_t, 8> uvPlaneData = {
            128, 128, 106, 255, 36, 4, 255, 126,
        };

        wgpu::TextureDataLayout externalTexturePlane1DataLayout = {};
        externalTexturePlane1DataLayout.bytesPerRow = 4;

        this->queue.WriteTexture(&plane1, uvPlaneData.data(),
                                 uvPlaneData.size() * sizeof(uvPlaneData[0]),
                                 &externalTexturePlane1DataLayout, &externalTexturePlane1Desc.size);

        // Create an ExternalTextureDescriptor from the texture views
        wgpu::ExternalTextureDescriptor externalDesc;
        utils::ColorSpaceConversionInfo info =
            utils::GetYUVBT709ToRGBSRGBColorSpaceConversionInfo();
        externalDesc.yuvToRgbConversionMatrix = info.yuvToRgbConversionMatrix.data();
        externalDesc.gamutConversionMatrix = info.gamutConversionMatrix.data();
        externalDesc.srcTransferFunctionParameters = info.srcTransferFunctionParameters.data();
        externalDesc.dstTransferFunctionParameters = info.dstTransferFunctionParameters.data();

        externalDesc.plane0 = externalTexturePlane0.CreateView();
        externalDesc.plane1 = externalTexturePlane1.CreateView();

        externalDesc.visibleOrigin = {0, 0};
        externalDesc.visibleSize = {kWidth, kHeight};

        // Import the external texture
        return this->device.CreateExternalTexture(&externalDesc);
    }

    std::vector<utils::RGBA8> GetExpectedData(bool flipY,
                                              wgpu::Origin3D srcOrigin,
                                              wgpu::Extent3D rect,
                                              wgpu::Extent2D naturalSize) {
        std::vector<utils::RGBA8> expected;
        for (uint32_t rowInRect = 0; rowInRect < rect.height; ++rowInRect) {
            for (uint32_t colInRect = 0; colInRect < rect.width; ++colInRect) {
                uint32_t row = rowInRect + srcOrigin.y;
                uint32_t col = colInRect + srcOrigin.x;

                if (flipY) {
                    row = (rect.height - rowInRect - 1) + srcOrigin.y;
                }

                // Upscale case
                if (naturalSize.width > kWidth) {
                    expected.push_back(kUpScaledExpectedRGBA[row][col]);
                } else if (naturalSize.width < kWidth) {
                    expected.push_back(kDownScaledExpectedRGBA[row][col]);
                } else {
                    expected.push_back(kDefaultExpectedRGBA[row][col]);
                }
            }
        }

        return expected;
    }
};

enum class CopyRect {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    FullSize,
};

enum class ScaleType {
    UpScale,
    DownScale,
    NoScale,
};

using FlipY = bool;
using CopySrcRect = CopyRect;
using CopyDstRect = CopyRect;

std::ostream& operator<<(std::ostream& o, ScaleType scaleType) {
    switch (scaleType) {
        case ScaleType::UpScale:
            o << "UpScale";
            break;
        case ScaleType::DownScale:
            o << "DownScale";
            break;
        case ScaleType::NoScale:
            o << "DefaultSize";
            break;
        default:
            UNREACHABLE();
            break;
    }
    return o;
}

std::ostream& operator<<(std::ostream& o, CopyRect copyRect) {
    switch (copyRect) {
        case CopyRect::TopLeft:
            o << "TopLeftCopy";
            break;
        case CopyRect::TopRight:
            o << "TopRightCopy";
            break;
        case CopyRect::BottomLeft:
            o << "BottomLeftCopy";
            break;
        case CopyRect::BottomRight:
            o << "BottomRightCopy";
            break;
        case CopyRect::FullSize:
            o << "FullSizeCopy";
            break;
        default:
            UNREACHABLE();
            break;
    }
    return o;
}

DAWN_TEST_PARAM_STRUCT(CopyTestParams, CopySrcRect, CopyDstRect, ScaleType, FlipY);

class CopyExternalTextureForBrowserTests_Basic
    : public CopyExternalTextureForBrowserTests<DawnTestWithParams<CopyTestParams>> {
  protected:
    void DoBasicCopyTest(const wgpu::Origin3D& srcOrigin,
                         const wgpu::Origin3D& dstOrigin,
                         const wgpu::Extent3D& copySize,
                         const wgpu::Extent2D& naturalSize,
                         const wgpu::Extent3D& dstTextureSize,
                         const wgpu::CopyTextureForBrowserOptions options = {}) {
        wgpu::ExternalTexture externalTexture = CreateDefaultExternalTexture();
        wgpu::ImageCopyExternalTexture srcImageCopyExternalTexture;
        srcImageCopyExternalTexture.externalTexture = externalTexture;
        srcImageCopyExternalTexture.origin = srcOrigin;
        srcImageCopyExternalTexture.naturalSize = naturalSize;

        wgpu::Texture dstTexture = Create2DTexture(
            device, dstTextureSize.width, dstTextureSize.height, wgpu::TextureFormat::RGBA8Unorm,
            wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
                wgpu::TextureUsage::CopyDst);
        wgpu::ImageCopyTexture dstImageCopyTexture =
            utils::CreateImageCopyTexture(dstTexture, 0, dstOrigin);

        queue.CopyExternalTextureForBrowser(&srcImageCopyExternalTexture, &dstImageCopyTexture,
                                            &copySize, &options);

        std::vector<utils::RGBA8> expected = GetExpectedData(
            options.flipY, srcImageCopyExternalTexture.origin, copySize, naturalSize);

        EXPECT_TEXTURE_EQ(expected.data(), dstTexture, dstOrigin, copySize);
    }
};
}  // anonymous namespace

TEST_P(CopyExternalTextureForBrowserTests_Basic, Copy) {
    DAWN_SUPPRESS_TEST_IF(IsOpenGLES());
    DAWN_SUPPRESS_TEST_IF(IsOpenGL() && IsLinux());

    wgpu::CopyTextureForBrowserOptions options = {};
    options.flipY = GetParam().mFlipY;

    CopyRect srcCopyRect = GetParam().mCopySrcRect;
    CopyRect dstCopyRect = GetParam().mCopyDstRect;
    ScaleType scaleType = GetParam().mScaleType;

    // Test skip due to crbug.com/dawn/1719
    DAWN_SUPPRESS_TEST_IF(IsWARP() && srcCopyRect != CopyRect::TopLeft &&
                          srcCopyRect != CopyRect::FullSize && dstCopyRect != CopyRect::TopLeft &&
                          dstCopyRect != CopyRect::FullSize && scaleType == ScaleType::DownScale);

    float scaleFactor = 1.0;

    switch (scaleType) {
        case ScaleType::UpScale:
            scaleFactor = 2.0;
            break;
        case ScaleType::DownScale:
            scaleFactor = 0.5;
            break;
        case ScaleType::NoScale:
            break;
        default:
            UNREACHABLE();
            break;
    }

    float defaultWidth = static_cast<float>(kWidth);
    float defaultHeight = static_cast<float>(kHeight);
    wgpu::Extent2D naturalSize = {static_cast<uint32_t>(defaultWidth * scaleFactor),
                                  static_cast<uint32_t>(defaultHeight * scaleFactor)};

    wgpu::Origin3D srcOrigin = {};
    wgpu::Origin3D dstOrigin = {};

    // Set copy size to sub rect copy size.
    wgpu::Extent3D copySize = {naturalSize.width / 2, naturalSize.height / 2};
    switch (srcCopyRect) {
        // origin = {0, 0};
        case CopyRect::TopLeft:
            break;
        case CopyRect::TopRight:
            srcOrigin.x = naturalSize.width / 2;
            srcOrigin.y = 0;
            break;
        case CopyRect::BottomLeft:
            srcOrigin.x = 0;
            srcOrigin.y = naturalSize.height / 2;
            break;
        case CopyRect::BottomRight:
            srcOrigin.x = naturalSize.width / 2;
            srcOrigin.y = naturalSize.height / 2;
            break;

        // origin = {0, 0}, copySize = naturalSize
        case CopyRect::FullSize:
            copySize.width = naturalSize.width;
            copySize.height = naturalSize.height;
            break;
        default:
            UNREACHABLE();
            break;
    }

    wgpu::Extent3D dstTextureSize = {copySize.width * 2, copySize.height * 2};
    switch (dstCopyRect) {
        case CopyRect::TopLeft:
            break;
        case CopyRect::TopRight:
            dstOrigin.x = dstTextureSize.width / 2;
            dstOrigin.y = 0;
            break;
        case CopyRect::BottomLeft:
            dstOrigin.x = 0;
            dstOrigin.y = dstTextureSize.height / 2;
            break;
        case CopyRect::BottomRight:
            dstOrigin.x = dstTextureSize.width / 2;
            dstOrigin.y = dstTextureSize.height / 2;
            break;
        case CopyRect::FullSize:
            if (srcCopyRect != CopyRect::FullSize) {
                dstTextureSize.width = copySize.width;
                dstTextureSize.height = copySize.height;
            }
            break;
        default:
            UNREACHABLE();
            break;
    }

    DoBasicCopyTest(srcOrigin, dstOrigin, copySize, naturalSize, dstTextureSize, options);
}

DAWN_INSTANTIATE_TEST_P(
    CopyExternalTextureForBrowserTests_Basic,
    {D3D12Backend(), MetalBackend(), OpenGLBackend(), OpenGLESBackend(), VulkanBackend()},
    std::vector<CopyRect>({CopyRect::TopLeft, CopyRect::TopRight, CopyRect::BottomLeft,
                           CopyRect::BottomRight, CopyRect::FullSize}),
    std::vector<CopyRect>({CopyRect::TopLeft, CopyRect::TopRight, CopyRect::BottomLeft,
                           CopyRect::BottomRight, CopyRect::FullSize}),
    std::vector<ScaleType>({ScaleType::UpScale, ScaleType::DownScale, ScaleType::NoScale}),
    std::vector<FlipY>({false, true}));
