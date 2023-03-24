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

#include "dawn/native/d3d11/RenderPipelineD3D11.h"

#include <d3dcompiler.h>

#include <memory>
#include <utility>

#include "dawn/native/CreatePipelineAsyncTask.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d/ShaderUtils.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/native/d3d11/Forward.h"
#include "dawn/native/d3d11/PipelineLayoutD3D11.h"
#include "dawn/native/d3d11/ShaderModuleD3D11.h"
#include "dawn/native/d3d11/UtilsD3D11.h"

namespace dawn::native::d3d11 {
namespace {

DXGI_FORMAT VertexFormatType(wgpu::VertexFormat format) {
    switch (format) {
        case wgpu::VertexFormat::Uint8x2:
            return DXGI_FORMAT_R8G8_UINT;
        case wgpu::VertexFormat::Uint8x4:
            return DXGI_FORMAT_R8G8B8A8_UINT;
        case wgpu::VertexFormat::Sint8x2:
            return DXGI_FORMAT_R8G8_SINT;
        case wgpu::VertexFormat::Sint8x4:
            return DXGI_FORMAT_R8G8B8A8_SINT;
        case wgpu::VertexFormat::Unorm8x2:
            return DXGI_FORMAT_R8G8_UNORM;
        case wgpu::VertexFormat::Unorm8x4:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case wgpu::VertexFormat::Snorm8x2:
            return DXGI_FORMAT_R8G8_SNORM;
        case wgpu::VertexFormat::Snorm8x4:
            return DXGI_FORMAT_R8G8B8A8_SNORM;
        case wgpu::VertexFormat::Uint16x2:
            return DXGI_FORMAT_R16G16_UINT;
        case wgpu::VertexFormat::Uint16x4:
            return DXGI_FORMAT_R16G16B16A16_UINT;
        case wgpu::VertexFormat::Sint16x2:
            return DXGI_FORMAT_R16G16_SINT;
        case wgpu::VertexFormat::Sint16x4:
            return DXGI_FORMAT_R16G16B16A16_SINT;
        case wgpu::VertexFormat::Unorm16x2:
            return DXGI_FORMAT_R16G16_UNORM;
        case wgpu::VertexFormat::Unorm16x4:
            return DXGI_FORMAT_R16G16B16A16_UNORM;
        case wgpu::VertexFormat::Snorm16x2:
            return DXGI_FORMAT_R16G16_SNORM;
        case wgpu::VertexFormat::Snorm16x4:
            return DXGI_FORMAT_R16G16B16A16_SNORM;
        case wgpu::VertexFormat::Float16x2:
            return DXGI_FORMAT_R16G16_FLOAT;
        case wgpu::VertexFormat::Float16x4:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case wgpu::VertexFormat::Float32:
            return DXGI_FORMAT_R32_FLOAT;
        case wgpu::VertexFormat::Float32x2:
            return DXGI_FORMAT_R32G32_FLOAT;
        case wgpu::VertexFormat::Float32x3:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case wgpu::VertexFormat::Float32x4:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case wgpu::VertexFormat::Uint32:
            return DXGI_FORMAT_R32_UINT;
        case wgpu::VertexFormat::Uint32x2:
            return DXGI_FORMAT_R32G32_UINT;
        case wgpu::VertexFormat::Uint32x3:
            return DXGI_FORMAT_R32G32B32_UINT;
        case wgpu::VertexFormat::Uint32x4:
            return DXGI_FORMAT_R32G32B32A32_UINT;
        case wgpu::VertexFormat::Sint32:
            return DXGI_FORMAT_R32_SINT;
        case wgpu::VertexFormat::Sint32x2:
            return DXGI_FORMAT_R32G32_SINT;
        case wgpu::VertexFormat::Sint32x3:
            return DXGI_FORMAT_R32G32B32_SINT;
        case wgpu::VertexFormat::Sint32x4:
            return DXGI_FORMAT_R32G32B32A32_SINT;
        default:
            UNREACHABLE();
    }
}

D3D11_INPUT_CLASSIFICATION VertexStepModeFunction(wgpu::VertexStepMode mode) {
    switch (mode) {
        case wgpu::VertexStepMode::Vertex:
            return D3D11_INPUT_PER_VERTEX_DATA;
        case wgpu::VertexStepMode::Instance:
            return D3D11_INPUT_PER_INSTANCE_DATA;
        case wgpu::VertexStepMode::VertexBufferNotUsed:
            UNREACHABLE();
    }
}

D3D_PRIMITIVE_TOPOLOGY D3DPrimitiveTopology(wgpu::PrimitiveTopology topology) {
    switch (topology) {
        case wgpu::PrimitiveTopology::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case wgpu::PrimitiveTopology::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case wgpu::PrimitiveTopology::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case wgpu::PrimitiveTopology::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case wgpu::PrimitiveTopology::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default:
            UNREACHABLE();
    }
}

D3D11_CULL_MODE D3DCullMode(wgpu::CullMode cullMode) {
    switch (cullMode) {
        case wgpu::CullMode::None:
            return D3D11_CULL_NONE;
        case wgpu::CullMode::Front:
            return D3D11_CULL_FRONT;
        case wgpu::CullMode::Back:
            return D3D11_CULL_BACK;
        default:
            UNREACHABLE();
    }
}

}  // namespace

// static
Ref<RenderPipeline> RenderPipeline::CreateUninitialized(
    Device* device,
    const RenderPipelineDescriptor* descriptor) {
    return AcquireRef(new RenderPipeline(device, descriptor));
}

RenderPipeline::RenderPipeline(Device* device, const RenderPipelineDescriptor* descriptor)
    : RenderPipelineBase(device, descriptor),
      mD3DPrimitiveTopology(D3DPrimitiveTopology(GetPrimitiveTopology())) {}

MaybeError RenderPipeline::Initialize() {
    DAWN_TRY(InitializeRasterizerState());
    DAWN_TRY(InitializeShaders());
    return {};
}

RenderPipeline::~RenderPipeline() = default;

MaybeError RenderPipeline::ApplyNow(CommandRecordingContext* commandRecordingContext) {
    ID3D11DeviceContext1* d3dDeviceContext1 = commandRecordingContext->GetD3D11DeviceContext1();
    d3dDeviceContext1->IASetPrimitiveTopology(mD3DPrimitiveTopology);
    d3dDeviceContext1->IASetInputLayout(mInputLayout.Get());
    d3dDeviceContext1->RSSetState(mRasterizerState.Get());
    d3dDeviceContext1->VSSetShader(mVertexShader.Get(), nullptr, 0);
    d3dDeviceContext1->PSSetShader(mPixelShader.Get(), nullptr, 0);
    return {};
}

void RenderPipeline::DestroyImpl() {
    RenderPipelineBase::DestroyImpl();
}

MaybeError RenderPipeline::InitializeRasterizerState() {
    Device* device = ToBackend(GetDevice());

    D3D11_RASTERIZER_DESC rasterizerDesc;
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3DCullMode(GetCullMode());
    rasterizerDesc.FrontCounterClockwise = (GetFrontFace() == wgpu::FrontFace::CCW) ? TRUE : FALSE;
    rasterizerDesc.DepthBias = GetDepthBias();
    rasterizerDesc.DepthBiasClamp = GetDepthBiasClamp();
    rasterizerDesc.SlopeScaledDepthBias = GetDepthBiasSlopeScale();
    // TODO(dawn:1705): set below fields.
    rasterizerDesc.DepthClipEnable = !HasUnclippedDepth();
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.MultisampleEnable = (GetSampleCount() > 1) ? TRUE : FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;

    DAWN_TRY(CheckHRESULT(
        device->GetD3D11Device()->CreateRasterizerState(&rasterizerDesc, &mRasterizerState),
        "ID3D11Device::CreateRasterizerState"));

    return {};
}

MaybeError RenderPipeline::InitializeInputLayout(const Blob& vertexShader) {
    std::array<D3D11_INPUT_ELEMENT_DESC, kMaxVertexAttributes> inputElementDescriptors;
    if (GetAttributeLocationsUsed().any()) {
        UINT count = ComputeInputLayout(&inputElementDescriptors);
        Device* device = ToBackend(GetDevice());
        DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateInputLayout(
                                  inputElementDescriptors.data(), count, vertexShader.Data(),
                                  vertexShader.Size(), &mInputLayout),
                              "ID3D11Device::CreateInputLayout"));
    }
    return {};
}

MaybeError RenderPipeline::InitializeShaders() {
    Device* device = ToBackend(GetDevice());
    uint32_t compileFlags = 0;

    if (!device->IsToggleEnabled(Toggle::UseDXC) &&
        !device->IsToggleEnabled(Toggle::FxcOptimizations)) {
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
    }

    if (device->IsToggleEnabled(Toggle::EmitHLSLDebugSymbols)) {
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    }

    // SPRIV-cross does matrix multiplication expecting row major matrices
    compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

    // FXC can miscompile code that depends on special float values (NaN, INF, etc) when IEEE
    // strictness is not enabled. See crbug.com/tint/976.
    compileFlags |= D3DCOMPILE_IEEE_STRICTNESS;

    PerStage<d3d::CompiledShader> compiledShader;

    std::bitset<kMaxInterStageShaderVariables>* usedInterstageVariables = nullptr;
    dawn::native::EntryPointMetadata fragmentEntryPoint;
    if (GetStageMask() & wgpu::ShaderStage::Fragment) {
        // Now that only fragment shader can have inter-stage inputs.
        const ProgrammableStage& programmableStage = GetStage(SingleShaderStage::Fragment);
        fragmentEntryPoint = programmableStage.module->GetEntryPoint(programmableStage.entryPoint);
        usedInterstageVariables = &fragmentEntryPoint.usedInterStageVariables;
    }

    if (GetStageMask() & wgpu::ShaderStage::Vertex) {
        const ProgrammableStage& programmableStage = GetStage(SingleShaderStage::Vertex);
        DAWN_TRY_ASSIGN(
            compiledShader[SingleShaderStage::Vertex],
            ToBackend(programmableStage.module)
                ->Compile(programmableStage, SingleShaderStage::Vertex, ToBackend(GetLayout()),
                          compileFlags, usedInterstageVariables));
        const Blob& shaderBlob = compiledShader[SingleShaderStage::Vertex].shaderBlob;
        DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreateVertexShader(
                                  shaderBlob.Data(), shaderBlob.Size(), nullptr, &mVertexShader),
                              "D3D11 create vertex shader"));
        DAWN_TRY(InitializeInputLayout(shaderBlob));
    }

    if (GetStageMask() & wgpu::ShaderStage::Fragment) {
        const ProgrammableStage& programmableStage = GetStage(SingleShaderStage::Fragment);
        DAWN_TRY_ASSIGN(
            compiledShader[SingleShaderStage::Fragment],
            ToBackend(programmableStage.module)
                ->Compile(programmableStage, SingleShaderStage::Fragment, ToBackend(GetLayout()),
                          compileFlags, usedInterstageVariables));
        DAWN_TRY(CheckHRESULT(device->GetD3D11Device()->CreatePixelShader(
                                  compiledShader[SingleShaderStage::Fragment].shaderBlob.Data(),
                                  compiledShader[SingleShaderStage::Fragment].shaderBlob.Size(),
                                  nullptr, &mPixelShader),
                              "D3D11 create pixel shader"));
    }

    return {};
}

void RenderPipeline::InitializeAsync(Ref<RenderPipelineBase> renderPipeline,
                                     WGPUCreateRenderPipelineAsyncCallback callback,
                                     void* userdata) {
    std::unique_ptr<CreateRenderPipelineAsyncTask> asyncTask =
        std::make_unique<CreateRenderPipelineAsyncTask>(std::move(renderPipeline), callback,
                                                        userdata);
    CreateRenderPipelineAsyncTask::RunAsync(std::move(asyncTask));
}

UINT RenderPipeline::ComputeInputLayout(
    std::array<D3D11_INPUT_ELEMENT_DESC, kMaxVertexAttributes>* inputElementDescriptors) {
    unsigned int count = 0;
    for (VertexAttributeLocation loc : IterateBitSet(GetAttributeLocationsUsed())) {
        D3D11_INPUT_ELEMENT_DESC& inputElementDescriptor = (*inputElementDescriptors)[count++];

        const VertexAttributeInfo& attribute = GetAttribute(loc);

        // If the HLSL semantic is TEXCOORDN the SemanticName should be "TEXCOORD" and the
        // SemanticIndex N
        inputElementDescriptor.SemanticName = "TEXCOORD";
        inputElementDescriptor.SemanticIndex = static_cast<uint8_t>(loc);
        inputElementDescriptor.Format = VertexFormatType(attribute.format);
        inputElementDescriptor.InputSlot = static_cast<uint8_t>(attribute.vertexBufferSlot);

        const VertexBufferInfo& input = GetVertexBuffer(attribute.vertexBufferSlot);

        inputElementDescriptor.AlignedByteOffset = attribute.offset;
        inputElementDescriptor.InputSlotClass = VertexStepModeFunction(input.stepMode);
        if (inputElementDescriptor.InputSlotClass == D3D11_INPUT_PER_VERTEX_DATA) {
            inputElementDescriptor.InstanceDataStepRate = 0;
        } else {
            inputElementDescriptor.InstanceDataStepRate = 1;
        }
    }

    return count;
}

}  // namespace dawn::native::d3d11
