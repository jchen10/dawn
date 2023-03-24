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

#include "dawn/native/d3d11/QueueD3D11.h"

#include "dawn/native/d3d11/BufferD3D11.h"
#include "dawn/native/d3d11/CommandBufferD3D11.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/native/d3d11/TextureD3D11.h"
#include "dawn/platform/DawnPlatform.h"
#include "dawn/platform/tracing/TraceEvent.h"

namespace dawn::native::d3d11 {

Ref<Queue> Queue::Create(Device* device, const QueueDescriptor* descriptor) {
    Ref<Queue> queue = AcquireRef(new Queue(device, descriptor));
    return queue;
}

MaybeError Queue::SubmitImpl(uint32_t commandCount, CommandBufferBase* const* commands) {
    Device* device = ToBackend(GetDevice());

    TRACE_EVENT_BEGIN0(GetDevice()->GetPlatform(), Recording, "CommandBufferGL::Execute");
    for (uint32_t i = 0; i < commandCount; ++i) {
        DAWN_TRY(ToBackend(commands[i])->Execute());
    }
    TRACE_EVENT_END0(GetDevice()->GetPlatform(), Recording, "CommandBufferGL::Execute");

    DAWN_TRY(device->NextSerial());

    return {};
}

MaybeError Queue::WriteBufferImpl(BufferBase* buffer,
                                  uint64_t bufferOffset,
                                  const void* data,
                                  size_t size) {
    if (size == 0) {
        return {};
    }

    ID3D11Buffer* d3d11Buffer = ToBackend(buffer)->GetD3D11Buffer();
    if (d3d11Buffer) {
        // For non-staging buffers, we can use UpdateSubresource to write the data.
        CommandRecordingContext* commandRecordingContext;
        DAWN_TRY_ASSIGN(commandRecordingContext,
                        ToBackend(GetDevice())->GetPendingCommandContext());

        DAWN_TRY_ASSIGN(std::ignore, ToBackend(buffer)->EnsureDataInitializedAsDestination(
                                         commandRecordingContext, bufferOffset, size));

        ID3D11DeviceContext1* d3d11DeviceContext1 =
            commandRecordingContext->GetD3D11DeviceContext1();
        D3D11_BOX dstBox;
        D3D11_BOX* pDstBox = nullptr;
        if (buffer->GetUsage() & wgpu::BufferUsage::Uniform) {
            if (bufferOffset != 0 || size != buffer->GetSize()) {
                return DAWN_VALIDATION_ERROR(
                    "Partial updates to uniform buffers are not allowed with D3D11");
            }
        } else {
            dstBox.left = static_cast<UINT>(bufferOffset);
            dstBox.right = static_cast<UINT>(bufferOffset + size);
            dstBox.top = 0;
            dstBox.bottom = 1;
            dstBox.front = 0;
            dstBox.back = 1;
            pDstBox = &dstBox;
        }
        d3d11DeviceContext1->UpdateSubresource(ToBackend(buffer)->GetD3D11Buffer(), 0, pDstBox,
                                               data, 0, 0);
    } else {
        // For staging buffers, we can just memcpy the data into the buffer.
        DAWN_TRY_ASSIGN(std::ignore, ToBackend(buffer)->EnsureDataInitializedAsDestination(
                                         nullptr, bufferOffset, size));
        memcpy(ToBackend(buffer)->GetStagingBufferPointer() + bufferOffset, data, size);
    }

    return {};
}

MaybeError Queue::WriteTextureImpl(const ImageCopyTexture& destination,
                                   const void* data,
                                   const TextureDataLayout& dataLayout,
                                   const Extent3D& writeSizePixel) {
    if (writeSizePixel.width == 0 || writeSizePixel.height == 0 ||
        writeSizePixel.depthOrArrayLayers == 0) {
        return {};
    }

    CommandRecordingContext* commandRecordingContext;
    DAWN_TRY_ASSIGN(commandRecordingContext, ToBackend(GetDevice())->GetPendingCommandContext());

    TextureCopy textureCopy;
    textureCopy.texture = destination.texture;
    textureCopy.mipLevel = destination.mipLevel;
    textureCopy.origin = destination.origin;
    textureCopy.aspect = SelectFormatAspects(destination.texture->GetFormat(), destination.aspect);

    SubresourceRange range = GetSubresourcesAffectedByCopy(textureCopy, writeSizePixel);
    if (IsCompleteSubresourceCopiedTo(destination.texture, writeSizePixel, destination.mipLevel)) {
        destination.texture->SetIsSubresourceContentInitialized(true, range);
    } else {
        DAWN_TRY(ToBackend(destination.texture)
                     ->EnsureSubresourceContentInitialized(commandRecordingContext, range));
    }

    uint32_t subresource = destination.texture->GetSubresourceIndex(
        destination.mipLevel, destination.origin.z, textureCopy.aspect);

    D3D11_BOX dstBox;
    dstBox.left = destination.origin.x;
    dstBox.right = destination.origin.x + writeSizePixel.width;
    dstBox.top = destination.origin.y;
    dstBox.bottom = destination.origin.y + writeSizePixel.height;
    dstBox.front = 0;
    dstBox.back = writeSizePixel.depthOrArrayLayers;

    commandRecordingContext->GetD3D11DeviceContext1()->UpdateSubresource(
        ToBackend(destination.texture)->GetD3D11Resource(), subresource, &dstBox, data,
        dataLayout.bytesPerRow, dataLayout.rowsPerImage * dataLayout.bytesPerRow);

    return {};
}

}  // namespace dawn::native::d3d11
