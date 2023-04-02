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

#include "dawn/native/d3d11/CommandBufferD3D11.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "dawn/native/BindGroup.h"
#include "dawn/native/BindGroupTracker.h"
#include "dawn/native/CommandEncoder.h"
#include "dawn/native/Commands.h"
#include "dawn/native/ExternalTexture.h"
#include "dawn/native/RenderBundle.h"
#include "dawn/native/VertexFormat.h"
#include "dawn/native/d3d/D3DError.h"
#include "dawn/native/d3d11/BufferD3D11.h"
#include "dawn/native/d3d11/ComputePipelineD3D11.h"
#include "dawn/native/d3d11/DeviceD3D11.h"
#include "dawn/native/d3d11/Forward.h"
#include "dawn/native/d3d11/PipelineLayoutD3D11.h"
#include "dawn/native/d3d11/RenderPipelineD3D11.h"
#include "dawn/native/d3d11/SamplerD3D11.h"
#include "dawn/native/d3d11/TextureD3D11.h"
#include "dawn/native/d3d11/UtilsD3D11.h"

namespace dawn::native::d3d11 {
namespace {

DXGI_FORMAT DXGIIndexFormat(wgpu::IndexFormat format) {
    switch (format) {
        case wgpu::IndexFormat::Uint16:
            return DXGI_FORMAT_R16_UINT;
        case wgpu::IndexFormat::Uint32:
            return DXGI_FORMAT_R32_UINT;
        default:
            UNREACHABLE();
    }
}

class BindGroupTracker : public BindGroupTrackerBase<false, uint64_t> {
  public:
    MaybeError Apply(CommandRecordingContext* commandRecordingContext) {
        BeforeApply();
        for (BindGroupIndex index : IterateBitSet(mDirtyBindGroupsObjectChangedOrIsDynamic)) {
            DAWN_TRY(ApplyBindGroup(commandRecordingContext, index, mBindGroups[index],
                                    mDynamicOffsets[index]));
        }
        AfterApply();
        return {};
    }

    void AfterDispatch(CommandRecordingContext* commandRecordingContext) {
        // Clear the UAVs after the dispatch.
        for (UINT uav : mUnorderedAccessViews) {
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            commandRecordingContext->GetD3D11DeviceContext1()->CSSetUnorderedAccessViews(
                uav, 1, &nullUAV, nullptr);
        }
        mUnorderedAccessViews.clear();
    }

  private:
    MaybeError ApplyBindGroup(CommandRecordingContext* commandRecordingContext,
                              BindGroupIndex index,
                              BindGroupBase* group,
                              const ityp::vector<BindingIndex, uint64_t>& dynamicOffsets) {
        const auto& indices = ToBackend(mPipelineLayout)->GetBindingIndexInfo()[index];
        for (BindingIndex bindingIndex{0}; bindingIndex < group->GetLayout()->GetBindingCount();
             ++bindingIndex) {
            const BindingInfo& bindingInfo = group->GetLayout()->GetBindingInfo(bindingIndex);
            const uint32_t bindingSlot = indices[bindingIndex];

            switch (bindingInfo.bindingType) {
                case BindingInfoType::Buffer: {
                    BufferBinding binding = group->GetBindingAsBufferBinding(bindingIndex);
                    ID3D11Buffer* d3d11Buffer = ToBackend(binding.buffer)->GetD3D11Buffer();
                    auto offset = binding.offset;
                    if (bindingInfo.buffer.hasDynamicOffset) {
                        // Dynamic buffers are packed at the front of BindingIndices.
                        offset += dynamicOffsets[bindingIndex];
                    }

                    auto* deviceContext = commandRecordingContext->GetD3D11DeviceContext1();

                    switch (bindingInfo.buffer.type) {
                        case wgpu::BufferBindingType::Uniform: {
                            // https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1
                            // Offset and size are measured in shader constants, which are 16 bytes
                            // (4*32-bit components).
                            DAWN_ASSERT(offset % 16 == 0);
                            UINT firstConstant = static_cast<UINT>(offset / 16);
                            // Each number of constants must be a multiple of 16 constants.
                            auto size = Align(binding.size, 256);
                            UINT numConstants = static_cast<UINT>(size / 16);

                            if (bindingInfo.visibility & wgpu::ShaderStage::Vertex) {
                                deviceContext->VSSetConstantBuffers1(bindingSlot, 1, &d3d11Buffer,
                                                                     &firstConstant, &numConstants);
                            }
                            if (bindingInfo.visibility & wgpu::ShaderStage::Fragment) {
                                deviceContext->PSSetConstantBuffers1(bindingSlot, 1, &d3d11Buffer,
                                                                     &firstConstant, &numConstants);
                            }
                            if (bindingInfo.visibility & wgpu::ShaderStage::Compute) {
                                deviceContext->CSSetConstantBuffers1(bindingSlot, 1, &d3d11Buffer,
                                                                     &firstConstant, &numConstants);
                            }
                            break;
                        }
                        case wgpu::BufferBindingType::Storage: {
                            ID3D11UnorderedAccessView* d3d11UAV;
                            DAWN_TRY_ASSIGN(
                                d3d11UAV,
                                ToBackend(binding.buffer)->GetD3D11UnorderedAccessView1());
                            UINT firstElement = offset / 4;
                            if (bindingInfo.visibility & wgpu::ShaderStage::Compute) {
                                deviceContext->CSSetUnorderedAccessViews(bindingSlot, 1, &d3d11UAV,
                                                                         &firstElement);
                                // Record the bounded UAVs so that we can clear them after the
                                // dispatch.
                                mUnorderedAccessViews.emplace_back(bindingSlot);
                            } else {
                                return DAWN_VALIDATION_ERROR(
                                    "Storage buffers are only supported in compute shaders");
                            }
                            break;
                        }
                        case wgpu::BufferBindingType::ReadOnlyStorage: {
                            ID3D11ShaderResourceView* d3d11SRV;
                            DAWN_TRY_ASSIGN(d3d11SRV, ToBackend(binding.buffer)
                                                          ->GetD3D11ShaderResourceView(
                                                              binding.offset, binding.size));
                            if (bindingInfo.visibility & wgpu::ShaderStage::Vertex) {
                                deviceContext->VSSetShaderResources(bindingSlot, 1, &d3d11SRV);
                            }
                            if (bindingInfo.visibility & wgpu::ShaderStage::Fragment) {
                                deviceContext->PSSetShaderResources(bindingSlot, 1, &d3d11SRV);
                            }
                            if (bindingInfo.visibility & wgpu::ShaderStage::Compute) {
                                deviceContext->CSSetShaderResources(bindingSlot, 1, &d3d11SRV);
                            }
                            break;
                        }
                        case wgpu::BufferBindingType::Undefined:
                            UNREACHABLE();
                    }
                    break;
                }

                case BindingInfoType::Sampler: {
                    Sampler* sampler = ToBackend(group->GetBindingAsSampler(bindingIndex));
                    ID3D11SamplerState* d3d11SamplerState = sampler->GetD3D11SamplerState();
                    if (bindingInfo.visibility & wgpu::ShaderStage::Vertex) {
                        commandRecordingContext->GetD3D11DeviceContext1()->VSSetSamplers(
                            bindingSlot, 1, &d3d11SamplerState);
                    }
                    if (bindingInfo.visibility & wgpu::ShaderStage::Fragment) {
                        commandRecordingContext->GetD3D11DeviceContext1()->PSSetSamplers(
                            bindingSlot, 1, &d3d11SamplerState);
                    }
                    if (bindingInfo.visibility & wgpu::ShaderStage::Compute) {
                        commandRecordingContext->GetD3D11DeviceContext1()->CSSetSamplers(
                            bindingSlot, 1, &d3d11SamplerState);
                    }
                    break;
                }

                case BindingInfoType::Texture: {
                    TextureView* view = ToBackend(group->GetBindingAsTextureView(bindingIndex));
                    ID3D11ShaderResourceView* srv;
                    DAWN_TRY_ASSIGN(srv, view->GetD3D11ShaderResourceView());
                    commandRecordingContext->GetD3D11DeviceContext1()->PSSetShaderResources(
                        bindingSlot, 1, &srv);
                    break;
                }

                case BindingInfoType::StorageTexture: {
                    return DAWN_UNIMPLEMENTED_ERROR("Storage textures are not supported");
                }

                case BindingInfoType::ExternalTexture: {
                    return DAWN_UNIMPLEMENTED_ERROR("External textures are not supported");
                    break;
                }
            }
        }
        return {};
    }

    std::vector<UINT> mUnorderedAccessViews;
};

}  // namespace

// Create CommandBuffer
Ref<CommandBuffer> CommandBuffer::Create(CommandEncoder* encoder,
                                         const CommandBufferDescriptor* descriptor) {
    Ref<CommandBuffer> commandBuffer = AcquireRef(new CommandBuffer(encoder, descriptor));
    return commandBuffer;
}

CommandBuffer::CommandBuffer(CommandEncoder* encoder, const CommandBufferDescriptor* descriptor)
    : CommandBufferBase(encoder, descriptor) {}

MaybeError CommandBuffer::Execute() {
    CommandRecordingContext* commandRecordingContext = nullptr;
    DAWN_TRY_ASSIGN(commandRecordingContext, ToBackend(GetDevice())->GetPendingCommandContext());

    ID3D11DeviceContext1* d3d11DeviceContext1 = commandRecordingContext->GetD3D11DeviceContext1();

    auto LazyClearSyncScope = [](const SyncScopeResourceUsage& scope) {
        // TODO(dawn:1705): clear resources.
        // for (size_t i = 0; i < scope.textures.size(); i++) {
        //     Texture* texture = ToBackend(scope.textures[i]);

        //     // Clear subresources that are not render attachments. Render attachments will be
        //     // cleared in RecordBeginRenderPass by setting the loadop to clear when the texture
        //     // subresource has not been initialized before the render pass.
        //     scope.textureUsages[i].Iterate(
        //         [&](const SubresourceRange& range, wgpu::TextureUsage usage) {
        //             if (usage & ~wgpu::TextureUsage::RenderAttachment) {
        //                 // texture->EnsureSubresourceContentInitialized(range);
        //             }
        //         });
        // }

        // for (BufferBase* bufferBase : scope.buffers) {
        //     ToBackend(bufferBase)->EnsureDataInitialized();
        // }
    };

    size_t nextComputePassNumber = 0;
    size_t nextRenderPassNumber = 0;

    Command type;
    while (mCommands.NextCommandId(&type)) {
        switch (type) {
            case Command::BeginComputePass: {
                mCommands.NextCommand<BeginComputePassCmd>();
                for (const SyncScopeResourceUsage& scope :
                     GetResourceUsages().computePasses[nextComputePassNumber].dispatchUsages) {
                    LazyClearSyncScope(scope);
                }
                DAWN_TRY(ExecuteComputePass(commandRecordingContext));

                nextComputePassNumber++;
                break;
            }

            case Command::BeginRenderPass: {
                auto* cmd = mCommands.NextCommand<BeginRenderPassCmd>();
                LazyClearSyncScope(GetResourceUsages().renderPasses[nextRenderPassNumber]);
                LazyClearRenderPassAttachments(cmd);
                DAWN_TRY(ExecuteRenderPass(cmd, commandRecordingContext));

                nextRenderPassNumber++;
                break;
            }

            case Command::CopyBufferToBuffer: {
                CopyBufferToBufferCmd* copy = mCommands.NextCommand<CopyBufferToBufferCmd>();
                if (copy->size == 0) {
                    // Skip no-op copies.
                    break;
                }

                Buffer* source = ToBackend(copy->source.Get());
                Buffer* destination = ToBackend(copy->destination.Get());

                DAWN_TRY(destination->CopyFromBuffer(commandRecordingContext,
                                                     copy->destinationOffset, copy->size, source,
                                                     copy->sourceOffset));
                break;
            }

            case Command::CopyBufferToTexture: {
                CopyBufferToTextureCmd* copy = mCommands.NextCommand<CopyBufferToTextureCmd>();
                if (copy->copySize.width == 0 || copy->copySize.height == 0 ||
                    copy->copySize.depthOrArrayLayers == 0) {
                    // Skip no-op copies.
                    continue;
                }

                auto& src = copy->source;
                auto& dst = copy->destination;
                Buffer* buffer = ToBackend(src.buffer.Get());

                uint32_t subresource =
                    dst.texture->GetSubresourceIndex(dst.mipLevel, dst.origin.z, dst.aspect);

                D3D11_BOX dstBox;
                dstBox.left = dst.origin.x;
                dstBox.right = dst.origin.x + copy->copySize.width;
                dstBox.top = dst.origin.y;
                dstBox.bottom = dst.origin.y + copy->copySize.height;
                dstBox.front = 0;
                dstBox.back = copy->copySize.depthOrArrayLayers;

                const uint8_t* pSrcData = buffer->GetStagingBufferPointer() + src.offset;

                d3d11DeviceContext1->UpdateSubresource(
                    ToBackend(dst.texture.Get())->GetD3D11Resource(), subresource, &dstBox,
                    pSrcData, src.bytesPerRow, src.rowsPerImage * src.bytesPerRow);
                break;
            }

            case Command::CopyTextureToBuffer: {
                CopyTextureToBufferCmd* copy = mCommands.NextCommand<CopyTextureToBufferCmd>();
                if (copy->copySize.width == 0 || copy->copySize.height == 0 ||
                    copy->copySize.depthOrArrayLayers == 0) {
                    // Skip no-op copies.
                    continue;
                }

                auto& src = copy->source;
                auto& dst = copy->destination;

                // Create a staging texture.
                D3D11_TEXTURE2D_DESC stagingTextureDesc;
                stagingTextureDesc.Width = copy->copySize.width;
                stagingTextureDesc.Height = copy->copySize.height;
                stagingTextureDesc.MipLevels = 1;
                stagingTextureDesc.ArraySize = copy->copySize.depthOrArrayLayers;
                stagingTextureDesc.Format = ToBackend(src.texture)->GetD3D11Format();
                stagingTextureDesc.SampleDesc.Count = 1;
                stagingTextureDesc.SampleDesc.Quality = 0;
                stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
                stagingTextureDesc.BindFlags = 0;
                stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stagingTextureDesc.MiscFlags = 0;

                ComPtr<ID3D11Texture2D> stagingTexture;
                DAWN_TRY(CheckHRESULT(commandRecordingContext->GetD3D11Device()->CreateTexture2D(
                                          &stagingTextureDesc, nullptr, &stagingTexture),
                                      "D3D11 create staging texture"));

                uint32_t subresource =
                    src.texture->GetSubresourceIndex(src.mipLevel, src.origin.z, src.aspect);

                // Copy the texture to the staging texture.
                D3D11_BOX srcBox;
                srcBox.left = src.origin.x;
                srcBox.right = src.origin.x + copy->copySize.width;
                srcBox.top = src.origin.y;
                srcBox.bottom = src.origin.y + copy->copySize.height;
                srcBox.front = 0;
                srcBox.back = copy->copySize.depthOrArrayLayers;

                d3d11DeviceContext1->CopySubresourceRegion(
                    stagingTexture.Get(), 0, 0, 0, 0, ToBackend(src.texture)->GetD3D11Resource(),
                    subresource, &srcBox);

                // Copy the staging texture to the buffer.
                // The Map() will block until the GPU is done with the texture.
                // TODO(dawn:1705): avoid blocking the CPU.
                D3D11_MAPPED_SUBRESOURCE mappedResource;
                DAWN_TRY(CheckHRESULT(d3d11DeviceContext1->Map(stagingTexture.Get(), 0,
                                                               D3D11_MAP_READ, 0, &mappedResource),
                                      "D3D11 map staging texture"));

                uint8_t* pDstData = ToBackend(dst.buffer)->GetStagingBufferPointer() + dst.offset;
                uint8_t* pSrcData = reinterpret_cast<uint8_t*>(mappedResource.pData);

                // TODO(dawn:1705): figure out the memcpy size.
                uint32_t memcpySize = std::min(dst.bytesPerRow, mappedResource.RowPitch);
                for (uint32_t y = 0; y < copy->copySize.height; ++y) {
                    memcpy(pDstData, pSrcData, memcpySize);
                    pDstData += dst.bytesPerRow;
                    pSrcData += mappedResource.RowPitch;
                }

                d3d11DeviceContext1->Unmap(stagingTexture.Get(), 0);

                break;
            }

            case Command::CopyTextureToTexture: {
                CopyTextureToTextureCmd* copy = mCommands.NextCommand<CopyTextureToTextureCmd>();
                if (copy->copySize.width == 0 || copy->copySize.height == 0 ||
                    copy->copySize.depthOrArrayLayers == 0) {
                    // Skip no-op copies.
                    continue;
                }

                auto& src = copy->source;
                auto& dst = copy->destination;

                // TODO(dawn:1705): Implement data initialization for textures.
                // ToBackend(src.texture)->EnsureSubresourceContentInitialized(srcRange);

                D3D11_BOX srcBox;
                srcBox.left = src.origin.x;
                srcBox.right = src.origin.x + copy->copySize.width;
                srcBox.top = src.origin.y;
                srcBox.bottom = src.origin.y + copy->copySize.height;
                srcBox.front = 0;
                srcBox.back = 1;

                uint32_t subresource =
                    src.texture->GetSubresourceIndex(src.mipLevel, src.origin.z, src.aspect);

                commandRecordingContext->GetD3D11DeviceContext()->CopySubresourceRegion(
                    ToBackend(dst.texture)->GetD3D11Resource(), dst.mipLevel, dst.origin.x,
                    dst.origin.y, dst.origin.z, ToBackend(src.texture)->GetD3D11Resource(),
                    subresource, &srcBox);

                // TODO(dawn:1705): Implement tracking of texture usage.
                // ToBackend(dst.texture)->Touch();
                break;
            }

            case Command::ClearBuffer: {
                ClearBufferCmd* cmd = mCommands.NextCommand<ClearBufferCmd>();
                if (cmd->size == 0) {
                    // Skip no-op fills.
                    break;
                }
                Buffer* buffer = ToBackend(cmd->buffer.Get());
                DAWN_TRY(buffer->ClearBuffer(commandRecordingContext, 0, cmd->offset, cmd->size));
                break;
            }

            case Command::ResolveQuerySet: {
                // TODO(crbug.com/dawn/434): Resolve non-precise occlusion query.
                SkipCommand(&mCommands, type);
                return DAWN_UNIMPLEMENTED_ERROR("ResolveQuerySet unimplemented");
                break;
            }

            case Command::WriteTimestamp: {
                return DAWN_UNIMPLEMENTED_ERROR("WriteTimestamp unimplemented");
            }

            case Command::InsertDebugMarker:
            case Command::PopDebugGroup:
            case Command::PushDebugGroup: {
                // TODO(dawn:1705): Implement debug markers.
                SkipCommand(&mCommands, type);
                break;
            }

            case Command::WriteBuffer: {
                WriteBufferCmd* cmd = mCommands.NextCommand<WriteBufferCmd>();
                if (cmd->size == 0) {
                    // Skip no-op writes.
                    continue;
                }

                Buffer* dstBuffer = ToBackend(cmd->buffer.Get());
                uint8_t* data = mCommands.NextData<uint8_t>(cmd->size);
                DAWN_TRY(
                    dstBuffer->WriteBuffer(commandRecordingContext, cmd->offset, data, cmd->size));

                break;
            }

            default:
                return DAWN_FORMAT_INTERNAL_ERROR("Unknown command type: %d", type);
        }
    }

    return {};
}

MaybeError CommandBuffer::ExecuteComputePass(CommandRecordingContext* commandRecordingContext) {
    ComputePipeline* lastPipeline = nullptr;
    BindGroupTracker bindGroupTracker = {};

    Command type;
    while (mCommands.NextCommandId(&type)) {
        switch (type) {
            case Command::EndComputePass: {
                mCommands.NextCommand<EndComputePassCmd>();
                return {};
            }

            case Command::Dispatch: {
                DispatchCmd* dispatch = mCommands.NextCommand<DispatchCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));

                DAWN_TRY(RecordNumWorkgroupsForDispatch(lastPipeline, commandRecordingContext,
                                                        dispatch));
                commandRecordingContext->GetD3D11DeviceContext()->Dispatch(dispatch->x, dispatch->y,
                                                                           dispatch->z);
                bindGroupTracker.AfterDispatch(commandRecordingContext);

                break;
            }

            case Command::DispatchIndirect: {
                DispatchIndirectCmd* dispatch = mCommands.NextCommand<DispatchIndirectCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));

                uint64_t indirectBufferOffset = dispatch->indirectOffset;
                Buffer* indirectBuffer = ToBackend(dispatch->indirectBuffer.Get());

                commandRecordingContext->GetD3D11DeviceContext()->DispatchIndirect(
                    indirectBuffer->GetD3D11Buffer(), indirectBufferOffset);

                bindGroupTracker.AfterDispatch(commandRecordingContext);

                break;
            }

            case Command::SetComputePipeline: {
                SetComputePipelineCmd* cmd = mCommands.NextCommand<SetComputePipelineCmd>();
                lastPipeline = ToBackend(cmd->pipeline).Get();
                lastPipeline->ApplyNow(commandRecordingContext);
                bindGroupTracker.OnSetPipeline(lastPipeline);
                break;
            }

            case Command::SetBindGroup: {
                SetBindGroupCmd* cmd = mCommands.NextCommand<SetBindGroupCmd>();

                uint32_t* dynamicOffsets = nullptr;
                if (cmd->dynamicOffsetCount > 0) {
                    dynamicOffsets = mCommands.NextData<uint32_t>(cmd->dynamicOffsetCount);
                }

                bindGroupTracker.OnSetBindGroup(cmd->index, cmd->group.Get(),
                                                cmd->dynamicOffsetCount, dynamicOffsets);

                break;
            }

            case Command::InsertDebugMarker:
            case Command::PopDebugGroup:
            case Command::PushDebugGroup: {
                SkipCommand(&mCommands, type);
                break;
            }

            case Command::WriteTimestamp: {
                return DAWN_UNIMPLEMENTED_ERROR("WriteTimestamp unimplemented");
            }

            default:
                UNREACHABLE();
        }
    }

    // EndComputePass should have been called
    UNREACHABLE();
}

MaybeError CommandBuffer::ExecuteRenderPass(BeginRenderPassCmd* renderPass,
                                            CommandRecordingContext* commandRecordingContext) {
    ID3D11DeviceContext1* d3d11DeviceContext1 = commandRecordingContext->GetD3D11DeviceContext1();

    ityp::array<ColorAttachmentIndex, ID3D11RenderTargetView*, kMaxColorAttachments>
        d3d11RenderTargetViews = {};
    ColorAttachmentIndex attachmentCount(uint8_t(0));
    for (ColorAttachmentIndex i :
         IterateBitSet(renderPass->attachmentState->GetColorAttachmentsMask())) {
        TextureView* colorTextureView = ToBackend(renderPass->colorAttachments[i].view.Get());
        DAWN_TRY_ASSIGN(d3d11RenderTargetViews[i], colorTextureView->GetD3D11RenderTargetView());
        if (renderPass->colorAttachments[i].loadOp == wgpu::LoadOp::Clear) {
            d3d11DeviceContext1->ClearRenderTargetView(
                d3d11RenderTargetViews[i],
                ConvertToFloatColor(renderPass->colorAttachments[i].clearColor).data());
        }
        attachmentCount = i;
        attachmentCount++;
    }

    ID3D11DepthStencilView* d3d11DepthStencilView = nullptr;
    if (renderPass->attachmentState->HasDepthStencilAttachment()) {
        auto* attachmentInfo = &renderPass->depthStencilAttachment;
        const Format& attachmentFormat = attachmentInfo->view->GetTexture()->GetFormat();

        TextureView* depthStencilTextureView =
            ToBackend(renderPass->depthStencilAttachment.view.Get());
        DAWN_TRY_ASSIGN(d3d11DepthStencilView,
                        depthStencilTextureView->GetD3D11DepthStencilView(false, false));
        UINT clearFlags = 0;
        if (attachmentFormat.HasDepth() &&
            renderPass->depthStencilAttachment.depthLoadOp == wgpu::LoadOp::Clear) {
            clearFlags |= D3D11_CLEAR_DEPTH;
        }

        if (attachmentFormat.HasStencil() &&
            renderPass->depthStencilAttachment.stencilLoadOp == wgpu::LoadOp::Clear) {
            clearFlags |= D3D11_CLEAR_STENCIL;
        }

        d3d11DeviceContext1->ClearDepthStencilView(d3d11DepthStencilView, clearFlags,
                                                   attachmentInfo->clearDepth,
                                                   attachmentInfo->clearStencil);
    }

    d3d11DeviceContext1->OMSetRenderTargets(static_cast<uint8_t>(attachmentCount),
                                            d3d11RenderTargetViews.data(), d3d11DepthStencilView);

    // Set viewport
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = renderPass->width;
    viewport.Height = renderPass->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    d3d11DeviceContext1->RSSetViewports(1, &viewport);

    // Set scissor
    D3D11_RECT scissor;
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = renderPass->width;
    scissor.bottom = renderPass->height;
    d3d11DeviceContext1->RSSetScissorRects(1, &scissor);

    RenderPipeline* lastPipeline = nullptr;
    BindGroupTracker bindGroupTracker = {};
    std::array<float, 4> blendColor = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t stencilReference = 0;

    auto DoRenderBundleCommand = [&](CommandIterator* iter, Command type) -> MaybeError {
        switch (type) {
            case Command::Draw: {
                DrawCmd* draw = iter->NextCommand<DrawCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));
                DAWN_TRY(RecordFirstIndexOffset(lastPipeline, commandRecordingContext,
                                                draw->firstVertex, draw->firstInstance));
                commandRecordingContext->GetD3D11DeviceContext()->DrawInstanced(
                    draw->vertexCount, draw->instanceCount, draw->firstVertex, draw->firstInstance);

                break;
            }

            case Command::DrawIndexed: {
                DrawIndexedCmd* draw = iter->NextCommand<DrawIndexedCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));
                DAWN_TRY(RecordFirstIndexOffset(lastPipeline, commandRecordingContext,
                                                draw->baseVertex, draw->firstInstance));
                commandRecordingContext->GetD3D11DeviceContext()->DrawIndexedInstanced(
                    draw->indexCount, draw->instanceCount, draw->firstIndex, draw->baseVertex,
                    draw->firstInstance);

                break;
            }

            case Command::DrawIndirect: {
                DrawIndirectCmd* draw = iter->NextCommand<DrawIndirectCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));
                uint64_t indirectBufferOffset = draw->indirectOffset;
                Buffer* indirectBuffer = ToBackend(draw->indirectBuffer.Get());
                ASSERT(indirectBuffer != nullptr);

                commandRecordingContext->GetD3D11DeviceContext()->DrawInstancedIndirect(
                    indirectBuffer->GetD3D11Buffer(), indirectBufferOffset);

                break;
            }

            case Command::DrawIndexedIndirect: {
                DrawIndexedIndirectCmd* draw = iter->NextCommand<DrawIndexedIndirectCmd>();

                DAWN_TRY(bindGroupTracker.Apply(commandRecordingContext));

                Buffer* indirectBuffer = ToBackend(draw->indirectBuffer.Get());
                ASSERT(indirectBuffer != nullptr);

                commandRecordingContext->GetD3D11DeviceContext()->DrawIndexedInstancedIndirect(
                    indirectBuffer->GetD3D11Buffer(), draw->indirectOffset);

                break;
            }

            case Command::InsertDebugMarker:
            case Command::PopDebugGroup:
            case Command::PushDebugGroup: {
                SkipCommand(iter, type);
                break;
            }

            case Command::SetRenderPipeline: {
                SetRenderPipelineCmd* cmd = iter->NextCommand<SetRenderPipelineCmd>();

                lastPipeline = ToBackend(cmd->pipeline).Get();
                DAWN_TRY(
                    lastPipeline->ApplyNow(commandRecordingContext, blendColor, stencilReference));
                bindGroupTracker.OnSetPipeline(lastPipeline);

                break;
            }

            case Command::SetBindGroup: {
                SetBindGroupCmd* cmd = iter->NextCommand<SetBindGroupCmd>();

                uint32_t* dynamicOffsets = nullptr;
                if (cmd->dynamicOffsetCount > 0) {
                    dynamicOffsets = iter->NextData<uint32_t>(cmd->dynamicOffsetCount);
                }
                bindGroupTracker.OnSetBindGroup(cmd->index, cmd->group.Get(),
                                                cmd->dynamicOffsetCount, dynamicOffsets);

                break;
            }

            case Command::SetIndexBuffer: {
                SetIndexBufferCmd* cmd = iter->NextCommand<SetIndexBufferCmd>();

                UINT indexBufferBaseOffset = cmd->offset;
                DXGI_FORMAT indexBufferFormat = DXGIIndexFormat(cmd->format);

                commandRecordingContext->GetD3D11DeviceContext()->IASetIndexBuffer(
                    ToBackend(cmd->buffer)->GetD3D11Buffer(), indexBufferFormat,
                    indexBufferBaseOffset);

                break;
            }

            case Command::SetVertexBuffer: {
                SetVertexBufferCmd* cmd = iter->NextCommand<SetVertexBufferCmd>();
                ASSERT(lastPipeline);
                const VertexBufferInfo& info = lastPipeline->GetVertexBuffer(cmd->slot);

                // TODO(dawn:1705): track all vertex buffers.
                UINT slot = static_cast<uint8_t>(cmd->slot);
                ID3D11Buffer* buffer = ToBackend(cmd->buffer)->GetD3D11Buffer();
                UINT arrayStride = info.arrayStride;
                UINT offset = cmd->offset;
                commandRecordingContext->GetD3D11DeviceContext()->IASetVertexBuffers(
                    slot, 1, &buffer, &arrayStride, &offset);

                break;
            }

            default:
                UNREACHABLE();
                break;
        }

        return {};
    };

    Command type;
    while (mCommands.NextCommandId(&type)) {
        switch (type) {
            case Command::EndRenderPass: {
                mCommands.NextCommand<EndRenderPassCmd>();
                // TODO(dawn:1705): resolve MSAA
                return {};
            }

            case Command::SetStencilReference: {
                SetStencilReferenceCmd* cmd = mCommands.NextCommand<SetStencilReferenceCmd>();
                stencilReference = cmd->reference;
                return {};
            }

            case Command::SetViewport: {
                SetViewportCmd* cmd = mCommands.NextCommand<SetViewportCmd>();

                D3D11_VIEWPORT viewport;
                viewport.TopLeftX = cmd->x;
                viewport.TopLeftY = cmd->y;
                viewport.Width = cmd->width;
                viewport.Height = cmd->height;
                viewport.MinDepth = cmd->minDepth;
                viewport.MaxDepth = cmd->maxDepth;
                commandRecordingContext->GetD3D11DeviceContext()->RSSetViewports(1, &viewport);
                break;
            }

            case Command::SetScissorRect: {
                SetScissorRectCmd* cmd = mCommands.NextCommand<SetScissorRectCmd>();

                D3D11_RECT scissorRect = {static_cast<LONG>(cmd->x), static_cast<LONG>(cmd->y),
                                          static_cast<LONG>(cmd->x + cmd->width),
                                          static_cast<LONG>(cmd->y + cmd->height)};
                commandRecordingContext->GetD3D11DeviceContext()->RSSetScissorRects(1,
                                                                                    &scissorRect);
                break;
            }

            case Command::SetBlendConstant: {
                SetBlendConstantCmd* cmd = mCommands.NextCommand<SetBlendConstantCmd>();
                blendColor = ConvertToFloatColor(cmd->color);
                break;
            }

            case Command::ExecuteBundles: {
                ExecuteBundlesCmd* cmd = mCommands.NextCommand<ExecuteBundlesCmd>();
                auto bundles = mCommands.NextData<Ref<RenderBundleBase>>(cmd->count);
                for (uint32_t i = 0; i < cmd->count; ++i) {
                    CommandIterator* iter = bundles[i]->GetCommands();
                    iter->Reset();
                    while (iter->NextCommandId(&type)) {
                        DAWN_TRY(DoRenderBundleCommand(iter, type));
                    }
                }
                break;
            }

            case Command::BeginOcclusionQuery: {
                return DAWN_UNIMPLEMENTED_ERROR("BeginOcclusionQuery unimplemented.");
            }

            case Command::EndOcclusionQuery: {
                return DAWN_UNIMPLEMENTED_ERROR("EndOcclusionQuery unimplemented.");
            }

            case Command::WriteTimestamp:
                return DAWN_UNIMPLEMENTED_ERROR("WriteTimestamp unimplemented");

            default: {
                DAWN_TRY(DoRenderBundleCommand(&mCommands, type));
            }
        }
    }

    // EndRenderPass should have been called
    UNREACHABLE();
}

MaybeError CommandBuffer::RecordFirstIndexOffset(RenderPipeline* renderPipeline,
                                                 CommandRecordingContext* commandRecordingContext,
                                                 uint32_t firstVertex,
                                                 uint32_t firstInstance) {
    if (!renderPipeline->GetUsesVertexOrInstanceIndex()) {
        // Vertex and instance index are not used in shader, so we don't need to update the uniform
        // buffer. The original value in the uniform buffer will not be used, so we don't need to
        // clear it.
        return {};
    }

    std::array<uint32_t, 256 / sizeof(uint32_t)> offsets = {
        firstVertex,
        firstInstance,
    };
    DAWN_TRY(commandRecordingContext->GetUniformBuffer()->WriteBuffer(
        commandRecordingContext, 0, offsets.data(), sizeof(uint32_t) * offsets.size()));
    return {};
}

MaybeError CommandBuffer::RecordNumWorkgroupsForDispatch(
    ComputePipeline* computePipeline,
    CommandRecordingContext* commandRecordingContext,
    DispatchCmd* dispatchCmd) {
    std::array<uint32_t, 256 / sizeof(uint32_t)> dispatch = {
        dispatchCmd->x,
        dispatchCmd->y,
        dispatchCmd->z,
    };
    DAWN_TRY(commandRecordingContext->GetUniformBuffer()->WriteBuffer(
        commandRecordingContext, 0, dispatch.data(), sizeof(uint32_t) * dispatch.size()));
    return {};
}

}  // namespace dawn::native::d3d11
