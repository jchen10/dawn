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

#ifndef SRC_DAWN_NATIVE_D3D11_PIPELINELAYOUTD3D11_H_
#define SRC_DAWN_NATIVE_D3D11_PIPELINELAYOUTD3D11_H_

#include "dawn/native/PipelineLayout.h"

#include "dawn/common/ityp_array.h"
#include "dawn/common/ityp_vector.h"
#include "dawn/native/BindingInfo.h"
#include "dawn/native/d3d/d3d_platform.h"

namespace dawn::native::d3d11 {

class Device;

class PipelineLayout final : public PipelineLayoutBase {
  public:
    enum {
        kReservedConstantBufferSlot = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1,
        kFirstIndexOffsetConstantBufferSlot = kReservedConstantBufferSlot,
        kNumWorkgroupsConstantBufferSlot = kReservedConstantBufferSlot,
    };

    static ResultOrError<Ref<PipelineLayout>> Create(Device* device,
                                                     const PipelineLayoutDescriptor* descriptor);

    using BindingIndexInfo =
        ityp::array<BindGroupIndex, ityp::vector<BindingIndex, uint32_t>, kMaxBindGroups>;
    const BindingIndexInfo& GetBindingIndexInfo() const;

    size_t GetNumSamplers() const;
    size_t GetNumSampledTextures() const;

  private:
    using PipelineLayoutBase::PipelineLayoutBase;

    ~PipelineLayout() override = default;

    MaybeError Initialize();

    BindingIndexInfo mIndexInfo;
    size_t mNumSamplers;
    size_t mNumSampledTextures;
};

}  // namespace dawn::native::d3d11

#endif  // SRC_DAWN_NATIVE_D3D11_PIPELINELAYOUTD3D11_H_
