// Copyright 2017 The NXT Authors
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

#ifndef BACKEND_VULKAN_VULKANINFO_H_
#define BACKEND_VULKAN_VULKANINFO_H_

#include "backend/vulkan/vulkan_platform.h"

#include <vector>

namespace backend {
namespace vulkan {

    class Device;

    extern const char kLayerNameLunargStandardValidation[];

    extern const char kExtensionNameExtDebugReport[];

    struct KnownGlobalVulkanExtensions {
        // Layers
        bool standardValidation = false;

        // Extensions
        bool debugReport = false;
    };

    // Stores the information about the Vulkan system that are required to use Vulkan.
    // Also does the querying of the information.
    struct VulkanInfo {
        // Global information - gathered before the instance is created
        struct : KnownGlobalVulkanExtensions {
            std::vector<VkLayerProperties> layers;
            std::vector<VkExtensionProperties> extensions;
            // TODO(cwallez@chromium.org): layer instance extensions
        } global;

        bool GatherGlobalInfo(const Device& device);
        void SetUsedGlobals(const KnownGlobalVulkanExtensions& usedGlobals);
    };

}
}

#endif // BACKEND_VULKAN_VULKANINFO_H_
