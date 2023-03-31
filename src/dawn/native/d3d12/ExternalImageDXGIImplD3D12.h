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

#ifndef SRC_DAWN_NATIVE_D3D12_EXTERNALIMAGEDXGIIMPLD3D12_H_
#define SRC_DAWN_NATIVE_D3D12_EXTERNALIMAGEDXGIIMPLD3D12_H_

#include <wrl/client.h>

#include <memory>
#include <vector>

#include "dawn/common/LinkedList.h"
#include "dawn/dawn_wsi.h"
#include "dawn/native/D3D12Backend.h"
#include "dawn/native/Error.h"
#include "dawn/native/Forward.h"
#include "dawn/native/IntegerTypes.h"
#include "dawn/native/d3d/ExternalImageDXGIImpl.h"
#include "dawn/native/d3d12/FenceD3D12.h"
#include "dawn/webgpu_cpp.h"

struct ID3D12Resource;

namespace dawn::native::d3d12 {

class D3D11on12ResourceCache;
class Device;

class ExternalImageDXGIImpl : public d3d::ExternalImageDXGIImpl {
  public:
    ExternalImageDXGIImpl(Device* backendDevice,
                          Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource,
                          const TextureDescriptor* textureDescriptor,
                          bool useFenceSynchronization);
    ~ExternalImageDXGIImpl() override;

    ExternalImageDXGIImpl(const ExternalImageDXGIImpl&) = delete;
    ExternalImageDXGIImpl& operator=(const ExternalImageDXGIImpl&) = delete;

    void Destroy() override;
    WGPUTexture BeginAccess(const ExternalImageDXGIBeginAccessDescriptor* descriptor) override;
    void EndAccess(WGPUTexture texture, ExternalImageDXGIFenceDescriptor* signalFence) override;

  private:
    using Base = d3d::ExternalImageDXGIImpl;

    Microsoft::WRL::ComPtr<ID3D12Resource> mD3D12Resource;
    std::unique_ptr<D3D11on12ResourceCache> mD3D11on12ResourceCache;
};

}  // namespace dawn::native::d3d12

#endif  // SRC_DAWN_NATIVE_D3D12_EXTERNALIMAGEDXGIIMPLD3D12_H_
