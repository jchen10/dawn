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

#include "dawn/native/d3d11/BackendD3D11.h"

#include <memory>
#include <utility>

#include "dawn/common/Log.h"
#include "dawn/native/D3D11Backend.h"
#include "dawn/native/Instance.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d11/AdapterD3D11.h"
#include "dawn/native/d3d11/PlatformFunctionsD3D11.h"
#include "dawn/native/d3d11/UtilsD3D11.h"

namespace dawn::native::d3d11 {
namespace {

ResultOrError<Ref<AdapterBase>> CreateAdapterFromIDXGIAdapter(Backend* backend,
                                                              ComPtr<IDXGIAdapter> dxgiAdapter,
                                                              const TogglesState& adapterToggles) {
    ComPtr<IDXGIAdapter3> dxgiAdapter3;
    DAWN_TRY(CheckHRESULT(dxgiAdapter.As(&dxgiAdapter3), "DXGIAdapter retrieval"));
    Ref<Adapter> adapter =
        AcquireRef(new Adapter(backend, std::move(dxgiAdapter3), adapterToggles));
    DAWN_TRY(adapter->Initialize());

    return {std::move(adapter)};
}

}  // anonymous namespace

Backend::Backend(InstanceBase* instance) : Base(instance, wgpu::BackendType::D3D11) {}

MaybeError Backend::Initialize() {
    auto functions = std::make_unique<PlatformFunctions>();
    DAWN_TRY(functions->LoadFunctions());

    DAWN_TRY(Base::Initialize(std::move(functions)));

    return {};
}

const PlatformFunctions* Backend::GetFunctions() const {
    return static_cast<const PlatformFunctions*>(Base::GetFunctions());
}

std::vector<Ref<AdapterBase>> Backend::DiscoverDefaultAdapters(const TogglesState& adapterToggles) {
    AdapterDiscoveryOptions options;
    auto result = DiscoverAdapters(&options, adapterToggles);
    if (result.IsError()) {
        GetInstance()->ConsumedError(result.AcquireError());
        return {};
    }
    return result.AcquireSuccess();
}

ResultOrError<std::vector<Ref<AdapterBase>>> Backend::DiscoverAdapters(
    const AdapterDiscoveryOptionsBase* optionsBase,
    const TogglesState& adapterToggles) {
    ASSERT(optionsBase->backendType == WGPUBackendType_D3D11);
    const AdapterDiscoveryOptions* options =
        static_cast<const AdapterDiscoveryOptions*>(optionsBase);

    std::vector<Ref<AdapterBase>> adapters;
    if (options->dxgiAdapter != nullptr) {
        // |dxgiAdapter| was provided. Discover just that adapter.
        Ref<AdapterBase> adapter;
        DAWN_TRY_ASSIGN(adapter,
                        CreateAdapterFromIDXGIAdapter(this, options->dxgiAdapter, adapterToggles));
        adapters.push_back(std::move(adapter));
        return std::move(adapters);
    }

    // Enumerate and discover all available adapters.
    for (uint32_t adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> dxgiAdapter = nullptr;
        if (GetFactory()->EnumAdapters1(adapterIndex, &dxgiAdapter) == DXGI_ERROR_NOT_FOUND) {
            break;  // No more adapters to enumerate.
        }

        ASSERT(dxgiAdapter != nullptr);
        ResultOrError<Ref<AdapterBase>> adapter =
            CreateAdapterFromIDXGIAdapter(this, dxgiAdapter, adapterToggles);
        if (adapter.IsError()) {
            GetInstance()->ConsumedError(adapter.AcquireError());
            continue;
        }

        adapters.push_back(adapter.AcquireSuccess());
    }

    return adapters;
}

BackendConnection* Connect(InstanceBase* instance) {
    Backend* backend = new Backend(instance);

    if (instance->ConsumedError(backend->Initialize())) {
        delete backend;
        return nullptr;
    }

    return backend;
}

}  // namespace dawn::native::d3d11
