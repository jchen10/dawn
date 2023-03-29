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

#include "dawn/native/d3d11/BufferD3D11.h"

#include <algorithm>
#include <vector>

#include "dawn/common/Assert.h"
#include "dawn/common/Constants.h"
#include "dawn/common/Math.h"
#include "dawn/native/CommandBuffer.h"
#include "dawn/native/DynamicUploader.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d11/CommandRecordingContextD3D11.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/platform/DawnPlatform.h"
#include "dawn/platform/tracing/TraceEvent.h"

namespace dawn::native::d3d11 {

namespace {

D3D11_USAGE D3D11BufferUsage(wgpu::BufferUsage usage) {
    if (usage & wgpu::BufferUsage::MapRead) {
        return D3D11_USAGE_STAGING;
    } else if (usage & wgpu::BufferUsage::MapWrite) {
        return D3D11_USAGE_DYNAMIC;
    } else {
        return D3D11_USAGE_DEFAULT;
    }
}

UINT D3D11BufferBindFlags(wgpu::BufferUsage usage) {
    UINT bindFlags = 0;

    if (usage & (wgpu::BufferUsage::Vertex)) {
        bindFlags |= D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
    }
    if (usage & wgpu::BufferUsage::Index) {
        bindFlags |= D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
    }
    if (usage & (wgpu::BufferUsage::Uniform)) {
        bindFlags |= D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER;
    }
    if (usage & (wgpu::BufferUsage::Storage | kInternalStorageBuffer)) {
        bindFlags |= D3D11_BIND_FLAG::D3D11_BIND_UNORDERED_ACCESS;
    }
    if (usage & kReadOnlyStorageBuffer) {
        bindFlags |= D3D11_BIND_FLAG::D3D11_BIND_SHADER_RESOURCE;
    }

    return bindFlags;
}

UINT D3D11BufferCPUAccessFlags(wgpu::BufferUsage usage) {
    UINT cpuAccessFlags = 0;
    return cpuAccessFlags;
}

UINT D3D11BufferMiscFlags(wgpu::BufferUsage usage) {
    // TODO(dawn:1705): figure out the flags for staging buffers
    UINT miscFlags = 0;
    return miscFlags;
}

UINT D3D11BufferStructureByteStride(wgpu::BufferUsage usage) {
    // TODO(dawn:1705): figure out the stride for staging buffers
    return 0;
}

size_t D3D11BufferSizeAlignment(wgpu::BufferUsage usage) {
    if (usage & wgpu::BufferUsage::Uniform) {
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1
        // Each number of constants must be a multiple of 16 shader constants(sizeof(float) * 4 *
        // 16).
        return sizeof(float) * 4 * 16;
    }

    if (usage & (wgpu::BufferUsage::Storage | kInternalStorageBuffer)) {
        // Structured buffers must be 4-byte aligned.
        return sizeof(uint32_t);
    }
    return 1;
}

bool IsGPUUsage(wgpu::BufferUsage usage) {
    return usage & (wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index |
                    wgpu::BufferUsage::Uniform | wgpu::BufferUsage::Storage);
}

MaybeError ValidationUsage(wgpu::BufferUsage usage) {
    if (usage & wgpu::BufferUsage::Uniform && usage & wgpu::BufferUsage::Storage) {
        return DAWN_VALIDATION_ERROR("Buffer usage can't be both uniform and storage with D3D11");
    }
    return {};
}

}  // namespace

// static
ResultOrError<Ref<Buffer>> Buffer::Create(Device* device, const BufferDescriptor* descriptor) {
    Ref<Buffer> buffer = AcquireRef(new Buffer(device, descriptor));
    DAWN_TRY(buffer->Initialize(descriptor->mappedAtCreation));
    return buffer;
}

Buffer::Buffer(Device* device, const BufferDescriptor* descriptor)
    : BufferBase(device, descriptor) {}

MaybeError Buffer::Initialize(bool mappedAtCreation) {
    DAWN_TRY(ValidationUsage(GetUsage()));

    // Allocate at least 4 bytes so clamped accesses are always in bounds.
    uint64_t size = std::max(GetSize(), uint64_t(4u));
    size_t alignment = D3D11BufferSizeAlignment(GetUsage());
    if (size > std::numeric_limits<uint64_t>::max() - alignment) {
        // Alignment would overlow.
        return DAWN_OUT_OF_MEMORY_ERROR("Buffer allocation is too large");
    }
    mAllocatedSize = Align(size, alignment);

    if (!mappedAtCreation && IsGPUUsage(GetUsage())) {
        // Create mD3d11Buffer
        D3D11_BUFFER_DESC bufferDescriptor;
        bufferDescriptor.ByteWidth = mAllocatedSize;
        bufferDescriptor.Usage = D3D11BufferUsage(GetUsage());
        bufferDescriptor.BindFlags = D3D11BufferBindFlags(GetUsage());
        bufferDescriptor.CPUAccessFlags = D3D11BufferCPUAccessFlags(GetUsage());
        bufferDescriptor.MiscFlags = D3D11BufferMiscFlags(GetUsage());
        bufferDescriptor.StructureByteStride = D3D11BufferStructureByteStride(GetUsage());

        DAWN_TRY(CheckHRESULT(ToBackend(GetDevice())
                                  ->GetD3D11Device()
                                  ->CreateBuffer(&bufferDescriptor, nullptr, &mD3d11Buffer),
                              "ID3D11Device::CreateBuffer"));
    } else {
        // Create mStagingBuffer, which is used for both staging and mappedAtCreation.
        // For mappedAtCreation, the creation of the mD3d1Buffer will be deferred to UnmapImpl()
        // when we can provide initial data.
        mStagingBuffer.reset(reinterpret_cast<uint8_t*>(malloc(GetAllocatedSize())));
        if (!mStagingBuffer) {
            return DAWN_OUT_OF_MEMORY_ERROR("Buffer allocation failed");
        }
    }

    SetLabelImpl();
    return {};
}

Buffer::~Buffer() = default;

bool Buffer::IsCPUWritableAtCreation() const {
    // All buffers can be initialized with data at creation, and we will allocate a staging buffer
    // in system memory for it.
    return true;
}

MaybeError Buffer::MapInternal(bool isWrite, size_t offset, size_t size, const char* contextInfo) {
    ASSERT(!mMappedData);
    ASSERT(!mD3d11Buffer);

    mMappedData = mStagingBuffer.get();
    return {};
}

MaybeError Buffer::MapAtCreationImpl() {
    ASSERT(!mD3d11Buffer);

    // The buffers with mappedAtCreation == true will be initialized in
    // BufferBase::MapAtCreation().
    DAWN_TRY(MapInternal(true, 0, size_t(GetAllocatedSize()), "D3D11 map at creation"));

    return {};
}

MaybeError Buffer::MapAsyncImpl(wgpu::MapMode mode, size_t offset, size_t size) {
    ASSERT(!IsGPUUsage(GetUsage()));

    DAWN_TRY(EnsureDataInitialized(nullptr));

    DAWN_TRY(MapInternal(mode & wgpu::MapMode::Write, offset, size, "D3D11 map async"));

    return {};
}

void Buffer::UnmapImpl() {
    ASSERT(mMappedData);
    mMappedData = nullptr;

    if (!IsGPUUsage(GetUsage())) {
        return;
    }

    ASSERT(!mD3d11Buffer);
    // Create mD3d11Buffer and copy the data from mStagingBuffer to mD3d11Buffer.
    D3D11_BUFFER_DESC bufferDescriptor;
    bufferDescriptor.ByteWidth = mAllocatedSize;
    bufferDescriptor.Usage = D3D11BufferUsage(GetUsage());
    bufferDescriptor.BindFlags = D3D11BufferBindFlags(GetUsage());
    bufferDescriptor.CPUAccessFlags = D3D11BufferCPUAccessFlags(GetUsage());
    bufferDescriptor.MiscFlags = D3D11BufferMiscFlags(GetUsage());
    bufferDescriptor.StructureByteStride = D3D11BufferStructureByteStride(GetUsage());

    D3D11_SUBRESOURCE_DATA subresourceData;
    subresourceData.pSysMem = mStagingBuffer.get();
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;

    HRESULT hr = ToBackend(GetDevice())
                     ->GetD3D11Device()
                     ->CreateBuffer(&bufferDescriptor, &subresourceData, &mD3d11Buffer);
    ASSERT(SUCCEEDED(hr));
    mStagingBuffer.reset();
}

void* Buffer::GetMappedPointer() {
    // The frontend asks that the pointer returned is from the start of the resource
    // irrespective of the offset passed in MapAsyncImpl, which is what mMappedData is.
    return mMappedData;
}

void Buffer::DestroyImpl() {
    BufferBase::DestroyImpl();
    mD3d11Buffer = nullptr;
}

MaybeError Buffer::EnsureDataInitialized(CommandRecordingContext* commandContext) {
    if (!NeedsInitialization()) {
        return {};
    }

    DAWN_TRY(InitializeToZero(commandContext));
    return {};
}

ResultOrError<bool> Buffer::EnsureDataInitializedAsDestination(
    CommandRecordingContext* commandContext,
    uint64_t offset,
    uint64_t size) {
    if (!NeedsInitialization()) {
        return {false};
    }

    if (IsFullBufferRange(offset, size)) {
        SetIsDataInitialized();
        return {false};
    }

    DAWN_TRY(InitializeToZero(commandContext));
    return {true};
}

MaybeError Buffer::EnsureDataInitializedAsDestination(CommandRecordingContext* commandContext,
                                                      const CopyTextureToBufferCmd* copy) {
    if (!NeedsInitialization()) {
        return {};
    }

    if (IsFullBufferOverwrittenInTextureToBufferCopy(copy)) {
        SetIsDataInitialized();
    } else {
        DAWN_TRY(InitializeToZero(commandContext));
    }

    return {};
}

void Buffer::SetLabelImpl() {
    if (mD3d11Buffer) {
        mD3d11Buffer->SetPrivateData(WKPDID_D3DDebugObjectName, GetLabel().size(),
                                     GetLabel().c_str());
    }
}

MaybeError Buffer::InitializeToZero(CommandRecordingContext* commandContext) {
    ASSERT(NeedsInitialization());

    // TODO(crbug.com/dawn/484): skip initializing the buffer when it is created on a heap
    // that has already been zero initialized.
    DAWN_TRY(ClearBufferInternal(commandContext, uint8_t(0u)));
    SetIsDataInitialized();
    GetDevice()->IncrementLazyClearCountForTesting();

    return {};
}

MaybeError Buffer::ClearBuffer(CommandRecordingContext* commandContext,
                               uint8_t clearValue,
                               uint64_t offset,
                               uint64_t size) {
    if (size == 0) {
        return {};
    }
    // For non-staging buffers, we can use UpdateSubresource to write the data.
    DAWN_TRY_ASSIGN(std::ignore,
                    this->EnsureDataInitializedAsDestination(commandContext, offset, size));
    return this->ClearBufferInternal(commandContext, clearValue, offset, size);
}

MaybeError Buffer::ClearBufferInternal(CommandRecordingContext* commandContext,
                                       uint8_t clearValue,
                                       uint64_t offset,
                                       uint64_t size) {
    if (size <= 0) {
        DAWN_ASSERT(offset == 0);
        size = GetAllocatedSize();
    }

    if (!mD3d11Buffer) {
        memset(mStagingBuffer.get() + offset, clearValue, size);
        return {};
    }

    std::vector<uint8_t> clearData(size, clearValue);
    return this->WriteBufferInternal(commandContext, offset, clearData.data(), size);
}

MaybeError Buffer::WriteBuffer(CommandRecordingContext* commandContext,
                               uint64_t offset,
                               const void* data,
                               size_t size) {
    if (size == 0) {
        return {};
    }

    // For non-staging buffers, we can use UpdateSubresource to write the data.
    DAWN_TRY_ASSIGN(std::ignore,
                    this->EnsureDataInitializedAsDestination(commandContext, offset, size));
    return this->WriteBufferInternal(commandContext, offset, data, size);
}

MaybeError Buffer::WriteBufferInternal(CommandRecordingContext* commandContext,
                                       uint64_t offset,
                                       const void* data,
                                       size_t size) {
    if (size == 0) {
        return {};
    }

    if (mD3d11Buffer) {
        // For non-staging buffers, we can use UpdateSubresource to write the data.
        ID3D11DeviceContext1* d3d11DeviceContext1 = commandContext->GetD3D11DeviceContext1();
        D3D11_BOX dstBox;
        D3D11_BOX* pDstBox = nullptr;
        if (this->GetUsage() & wgpu::BufferUsage::Uniform) {
            if (offset != 0 || size != this->GetSize()) {
                return DAWN_VALIDATION_ERROR(
                    "Partial updates to uniform buffers are not allowed with D3D11");
            }
        } else {
            dstBox.left = static_cast<UINT>(offset);
            dstBox.right = static_cast<UINT>(offset + size);
            dstBox.top = 0;
            dstBox.bottom = 1;
            dstBox.front = 0;
            dstBox.back = 1;
            pDstBox = &dstBox;
        }
        d3d11DeviceContext1->UpdateSubresource(this->GetD3D11Buffer(), 0, pDstBox, data, 0, 0);
        return {};
    }

    memcpy(this->GetStagingBufferPointer() + offset, data, size);
    return {};
}

MaybeError Buffer::CopyFromBuffer(CommandRecordingContext* commandContext,
                                  uint64_t offset,
                                  size_t size,
                                  Buffer* source,
                                  uint64_t sourceOffset) {
    if (size == 0) {
        // Skip no-op copies.
        return {};
    }

    DAWN_TRY(source->EnsureDataInitialized(commandContext));
    DAWN_TRY_ASSIGN(std::ignore,
                    this->EnsureDataInitializedAsDestination(commandContext, offset, size));

    if (source->GetD3D11Buffer() && this->GetD3D11Buffer()) {
        // Both buffers are GPU buffers.
        D3D11_BOX srcBox;
        srcBox.left = sourceOffset;
        srcBox.right = sourceOffset + size;
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;
        commandContext->GetD3D11DeviceContext()->CopySubresourceRegion(
            this->GetD3D11Buffer(), 0, offset, 0, 0, source->GetD3D11Buffer(), 0, &srcBox);
        return {};
    }

    if (source->GetD3D11Buffer()) {
        // Source buffer is a GPU buffer, destination buffer is a staging buffer (in system memory).
        D3D11_BUFFER_DESC stagingDesc;
        stagingDesc.ByteWidth = size;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.StructureByteStride = 0;

        ComPtr<ID3D11Buffer> stagingBuffer;
        DAWN_TRY(CheckHRESULT(
            commandContext->GetD3D11Device()->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer),
            "ID3D11Device::CreateBuffer"));

        D3D11_BOX srcBox;
        srcBox.left = sourceOffset;
        srcBox.right = sourceOffset + size;
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;
        commandContext->GetD3D11DeviceContext()->CopySubresourceRegion(
            stagingBuffer.Get(), 0, 0, 0, 0, source->GetD3D11Buffer(), 0, &srcBox);

        // Map the staging buffer
        // The map call will block until the GPU is done with the resource.
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        DAWN_TRY(CheckHRESULT(commandContext->GetD3D11DeviceContext()->Map(
                                  stagingBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedResource),
                              "ID3D11DeviceContext::Map"));

        memcpy(this->GetStagingBufferPointer() + offset, mappedResource.pData, size);

        // Unmap the staging buffer
        commandContext->GetD3D11DeviceContext()->Unmap(stagingBuffer.Get(), 0);
        return {};
    }

    // If source buffer is a staging buffer (in system memory).
    return this->WriteBufferInternal(commandContext, offset,
                                     source->GetStagingBufferPointer() + sourceOffset, size);
}

}  // namespace dawn::native::d3d11
