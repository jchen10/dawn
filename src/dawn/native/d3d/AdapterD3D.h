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

#ifndef SRC_DAWN_NATIVE_D3D_ADAPTERD3D_H_
#define SRC_DAWN_NATIVE_D3D_ADAPTERD3D_H_

#include "dawn/native/Adapter.h"

#include "dawn/native/d3d/d3d_platform.h"

namespace dawn::native::d3d {

class Backend;

class Adapter : public AdapterBase {
  public:
    Adapter(Backend* backend,
            ComPtr<IDXGIAdapter3> hardwareAdapter,
            wgpu::BackendType backendType,
            const TogglesState& adapterToggles);
    ~Adapter() override;

    IDXGIAdapter3* GetHardwareAdapter() const;
    Backend* GetBackend() const;

  private:
    ComPtr<IDXGIAdapter3> mHardwareAdapter;
    Backend* mBackend;
};

}  // namespace dawn::native::d3d

#endif  // SRC_DAWN_NATIVE_D3D_ADAPTERD3D_H_
