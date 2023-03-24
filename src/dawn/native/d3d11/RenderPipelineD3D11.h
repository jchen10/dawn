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

#ifndef SRC_DAWN_NATIVE_D3D11_RENDERPIPELINED3D11_H_
#define SRC_DAWN_NATIVE_D3D11_RENDERPIPELINED3D11_H_

#include <vector>

#include "dawn/native/RenderPipeline.h"

#include "dawn/native/d3d/d3d_platform.h"

namespace dawn::native::d3d11 {

class CommandRecordingContext;
class Device;
class PersistentPipelineState;

class RenderPipeline final : public RenderPipelineBase {
  public:
    static Ref<RenderPipeline> CreateUninitialized(Device* device,
                                                   const RenderPipelineDescriptor* descriptor);

    static void InitializeAsync(Ref<RenderPipelineBase> renderPipeline,
                                WGPUCreateRenderPipelineAsyncCallback callback,
                                void* userdata);

    MaybeError ApplyNow(CommandRecordingContext* commandRecordingContext);

  private:
    RenderPipeline(Device* device, const RenderPipelineDescriptor* descriptor);
    ~RenderPipeline() override;

    MaybeError Initialize() override;
    void DestroyImpl() override;

    MaybeError InitializeRasterizerState();
    MaybeError InitializeInputLayout(const Blob& vertexShader);
    MaybeError InitializeShaders();

    UINT ComputeInputLayout(
        std::array<D3D11_INPUT_ELEMENT_DESC, kMaxVertexAttributes>* inputElementDescriptors);

    const D3D_PRIMITIVE_TOPOLOGY mD3DPrimitiveTopology;
    ComPtr<ID3D11RasterizerState> mRasterizerState;
    ComPtr<ID3D11InputLayout> mInputLayout;
    ComPtr<ID3D11VertexShader> mVertexShader;
    ComPtr<ID3D11PixelShader> mPixelShader;
};

}  // namespace dawn::native::d3d11

#endif  // SRC_DAWN_NATIVE_D3D11_RENDERPIPELINED3D11_H_
