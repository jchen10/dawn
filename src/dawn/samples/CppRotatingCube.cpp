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

#include <cmath>
#include <vector>

#include "dawn/samples/SampleUtils.h"
#include "dawn/utils/ComboRenderPipelineDescriptor.h"
#include "dawn/utils/ScopedAutoreleasePool.h"
#include "dawn/utils/SystemUtils.h"
#include "dawn/utils/WGPUHelpers.h"

#include "third_party/glm/glm/ext/matrix_clip_space.hpp"  // glm::perspective
#include "third_party/glm/glm/ext/matrix_transform.hpp"   // glm::translate, glm::rotate, glm::scale
#include "third_party/glm/glm/ext/scalar_constants.hpp"   // glm::pi
#include "third_party/glm/glm/mat4x4.hpp"                 // glm::mat4
#include "third_party/glm/glm/vec3.hpp"                   // glm::vec3
#include "third_party/glm/glm/vec4.hpp"                   // glm::vec4

wgpu::Device device;

wgpu::Buffer vertexBuffer;
wgpu::Buffer uniformBuffer;

wgpu::Texture texture;
wgpu::Sampler sampler;

wgpu::Queue queue;
wgpu::SwapChain swapchain;
wgpu::TextureView depthStencilView;
wgpu::RenderPipeline pipeline;
wgpu::BindGroup bindGroup;

void initBuffers() {
    static const float vertexData[] = {
        // clang-format off
        // float4 position, float4 color, float2 uv,
        1,  -1, 1,  1,      1, 0, 1, 1,   1, 1,
        -1, -1, 1,  1,      0, 0, 1, 1,   0, 1,
        -1, -1, -1, 1,      0, 0, 0, 1,   0, 0,
        1,  -1, -1, 1,      1, 0, 0, 1,   1, 0,
        1,  -1, 1,  1,      1, 0, 1, 1,   1, 1,
        -1, -1, -1, 1,      0, 0, 0, 1,   0, 0,

        1,  1,  1,  1,      1, 1, 1, 1,   1, 1,
        1,  -1, 1,  1,      1, 0, 1, 1,   0, 1,
        1,  -1, -1, 1,      1, 0, 0, 1,   0, 0,
        1,  1,  -1, 1,      1, 1, 0, 1,   1, 0,
        1,  1,  1,  1,      1, 1, 1, 1,   1, 1,
        1,  -1, -1, 1,      1, 0, 0, 1,   0, 0,

        -1, 1,  1,  1,      0, 1, 1, 1,   1, 1,
        1,  1,  1,  1,      1, 1, 1, 1,   0, 1,
        1,  1,  -1, 1,      1, 1, 0, 1,   0, 0,
        -1, 1,  -1, 1,      0, 1, 0, 1,   1, 0,
        -1, 1,  1,  1,      0, 1, 1, 1,   1, 1,
        1,  1,  -1, 1,      1, 1, 0, 1,   0, 0,

        -1, -1, 1,  1,      0, 0, 1, 1,   1, 1,
        -1, 1,  1,  1,      0, 1, 1, 1,   0, 1,
        -1, 1,  -1, 1,      0, 1, 0, 1,   0, 0,
        -1, -1, -1, 1,      0, 0, 0, 1,   1, 0,
        -1, -1, 1,  1,      0, 0, 1, 1,   1, 1,
        -1, 1,  -1, 1,      0, 1, 0, 1,   0, 0,

        1,  1,  1,  1,      1, 1, 1, 1,   1, 1,
        -1, 1,  1,  1,      0, 1, 1, 1,   0, 1,
        -1, -1, 1,  1,      0, 0, 1, 1,   0, 0,
        -1, -1, 1,  1,      0, 0, 1, 1,   0, 0,
        1,  -1, 1,  1,      1, 0, 1, 1,   1, 0,
        1,  1,  1,  1,      1, 1, 1, 1,   1, 1,

        1,  -1, -1, 1,      1, 0, 0, 1,   1, 1,
        -1, -1, -1, 1,      0, 0, 0, 1,   0, 1,
        -1, 1,  -1, 1,      0, 1, 0, 1,   0, 0,
        1,  1,  -1, 1,      1, 1, 0, 1,   1, 0,
        1,  -1, -1, 1,      1, 0, 0, 1,   1, 1,
        -1, 1,  -1, 1,      0, 1, 0, 1,   0, 0,
        // clang-format on
    };

    vertexBuffer = utils::CreateBufferFromData(device, vertexData, sizeof(vertexData),
                                               wgpu::BufferUsage::Vertex);

    wgpu::BufferDescriptor descriptor;
    descriptor.size = sizeof(float) * 16;
    descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    uniformBuffer = device.CreateBuffer(&descriptor);
}

void initTextures() {
    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size.width = 1024;
    descriptor.size.height = 1024;
    descriptor.size.depthOrArrayLayers = 1;
    descriptor.sampleCount = 1;
    descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding;
    texture = device.CreateTexture(&descriptor);

    sampler = device.CreateSampler();

    // Initialize the texture with arbitrary data until we can load images
    std::vector<uint8_t> data(4 * 1024 * 1024, 0);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i % 253);
    }

    wgpu::Buffer stagingBuffer = utils::CreateBufferFromData(
        device, data.data(), static_cast<uint32_t>(data.size()), wgpu::BufferUsage::CopySrc);
    wgpu::ImageCopyBuffer imageCopyBuffer =
        utils::CreateImageCopyBuffer(stagingBuffer, 0, 4 * 1024);
    wgpu::ImageCopyTexture imageCopyTexture = utils::CreateImageCopyTexture(texture, 0, {0, 0, 0});
    wgpu::Extent3D copySize = {1024, 1024, 1};

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    encoder.CopyBufferToTexture(&imageCopyBuffer, &imageCopyTexture, &copySize);

    wgpu::CommandBuffer copy = encoder.Finish();
    queue.Submit(1, &copy);
}

void init() {
    device = CreateCppDawnDevice();

    queue = device.GetQueue();
    swapchain = GetSwapChain();

    initBuffers();
    initTextures();

    const char* vs = R"(
        struct Uniforms {
            modelViewProjectionMatrix : mat4x4<f32>,
        }
        @binding(0) @group(0) var<uniform> uniforms : Uniforms;

        struct VertexOutput {
            @builtin(position) Position : vec4<f32>,
            @location(0) fragUV : vec2<f32>,
            @location(1) fragPosition: vec4<f32>,
            @location(2) fragColor: vec4<f32>,
        }

        @vertex
        fn main(
            @location(0) position : vec4<f32>,
            @location(1) color : vec4<f32>,
            @location(2) uv : vec2<f32>
        ) -> VertexOutput {
            var output : VertexOutput;
            output.Position = uniforms.modelViewProjectionMatrix * position;
            output.fragUV = uv;
            output.fragPosition = 0.5 * (position + vec4(1.0, 1.0, 1.0, 1.0));
            output.fragColor = color;
            return output;
        })";
    const char* fs = R"(
        @fragment
        fn main(
            @location(0) fragUV: vec2<f32>,
            @location(1) fragPosition: vec4<f32>,
            @location(2) fragColor: vec4<f32>
        ) -> @location(0) vec4<f32> {
            return fragColor;
        })";

    wgpu::ShaderModule vsModule = utils::CreateShaderModule(device, vs);

    wgpu::ShaderModule fsModule = utils::CreateShaderModule(device, fs);

    auto bgl = utils::MakeBindGroupLayout(
        device, {{0, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::Uniform}});

    wgpu::PipelineLayout pl = utils::MakeBasicPipelineLayout(device, &bgl);

    depthStencilView = CreateDefaultDepthStencilView(device);

    utils::ComboRenderPipelineDescriptor descriptor;
    descriptor.layout = utils::MakeBasicPipelineLayout(device, &bgl);
    descriptor.vertex.module = vsModule;
    descriptor.vertex.bufferCount = 1;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.primitive.cullMode = wgpu::CullMode::Back;

    descriptor.cBuffers[0].arrayStride = 10 * sizeof(float);
    descriptor.cBuffers[0].attributeCount = 3;
    descriptor.cAttributes[0].shaderLocation = 0;
    descriptor.cAttributes[0].format = wgpu::VertexFormat::Float32x4;
    descriptor.cAttributes[0].offset = 0;
    descriptor.cAttributes[1].shaderLocation = 1;
    descriptor.cAttributes[1].format = wgpu::VertexFormat::Float32x4;
    descriptor.cAttributes[1].offset = 4 * sizeof(float);
    descriptor.cAttributes[2].shaderLocation = 2;
    descriptor.cAttributes[2].format = wgpu::VertexFormat::Float32x2;
    descriptor.cAttributes[2].offset = 8 * sizeof(float);

    descriptor.cFragment.module = fsModule;
    descriptor.cTargets[0].format = GetPreferredSwapChainTextureFormat();
    descriptor.EnableDepthStencil(wgpu::TextureFormat::Depth24PlusStencil8);

    pipeline = device.CreateRenderPipeline(&descriptor);

    wgpu::TextureView view = texture.CreateView();

    bindGroup = utils::MakeBindGroup(device, bgl,
                                     {
                                         {0, uniformBuffer, 0, sizeof(float) * 16},
                                     });
}

glm::mat4 camera(float Translate, glm::vec2 const& Rotate) {
    glm::mat4 Projection = glm::perspective(glm::pi<float>() * 0.25f, 4.0f / 3.0f, 0.1f, 100.f);
    glm::mat4 View = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -Translate));
    View = glm::rotate(View, Rotate.y, glm::vec3(-1.0f, 0.0f, 0.0f));
    View = glm::rotate(View, Rotate.x, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 Model = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    return Projection * View * Model;
}

void frame() {
    wgpu::TextureView backbufferView = swapchain.GetCurrentTextureView();
    utils::ComboRenderPassDescriptor renderPass({backbufferView}, depthStencilView);
    renderPass.cColorAttachments[0].loadOp = wgpu::LoadOp::Clear;
    renderPass.cColorAttachments[0].clearValue = {0.5f, 0.5f, 0.5f, 1.0f};

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    static float frame = 0.0f;
    frame += 1.0f;
    glm::mat4 matrix = camera(
        3.0f, glm::vec2(sin(glm::pi<float>() / 180 * frame), cos(glm::pi<float>() / 90 * frame)));
    encoder.WriteBuffer(uniformBuffer, 0, reinterpret_cast<const uint8_t*>(&matrix[0][0]),
                        sizeof(float) * 16);
    {
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass);
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroup);
        pass.SetVertexBuffer(0, vertexBuffer);
        pass.Draw(36, 1, 0, 0);
        pass.End();
    }

    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
    swapchain.Present();
    DoFlush();
}

int main(int argc, const char* argv[]) {
    if (!InitSample(argc, argv)) {
        return 1;
    }
    init();

    while (!ShouldQuit()) {
        utils::ScopedAutoreleasePool pool;
        frame();
        utils::USleep(16000);
    }
}
