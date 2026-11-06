#include "graphics.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <memory>
#include <climits>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace graphics {
namespace {

using application::ConeVertex;

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    VkDeviceSize capacity = 0;
};

Buffer vertex_buffer, index_buffer, scene_buffer, model_buffer;
Buffer point_buffer, spot_buffer, marker_buffer, line_buffer;
Buffer marker_staging, line_staging;
uint32_t index_count = 0;
VkDeviceSize max_storage_range = 0;
VkDescriptorSetLayout descriptor_layouts[2]{};
VkDescriptorSet descriptor_sets[5]{}; // scene and four materials
VkDeviceSize model_stride;
struct Texture {
    VkImage image{};
    VmaAllocation allocation{};
    VkImageView view{};
};
Texture textures[7];
VkSampler sampler{};
VkCommandPool upload_pool{};
struct Effect { int32_t enabled; float time; };
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE, marker_pipeline = VK_NULL_HANDLE, line_pipeline = VK_NULL_HANDLE;

bool check(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed (VkResult " << result << ")\n";
    return false;
}

std::filesystem::path shaderDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (length == 0 || length >= executable.size()) {
        throw std::runtime_error("Cannot determine the executable directory");
    }
    return std::filesystem::path(std::wstring(executable.data(), length)).parent_path() / L"shaders";
#else
    return std::filesystem::current_path() / "shaders";
#endif
}

bool loadShader(const std::filesystem::path& path, VkShaderModule& module) {
    const auto utf8 = path.u8string();
    const std::string printable(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "Cannot open shader: " << printable << ". Run build.cmd to compile shaders.\n";
        return false;
    }
    const auto bytes = input.tellg();
    if (bytes < 20 || bytes % 4 != 0 || bytes > 4 * 1024 * 1024) {
        std::cerr << "Invalid SPIR-V shader size: " << printable << '\n';
        return false;
    }
    std::vector<uint32_t> code(size_t(bytes) / sizeof(uint32_t));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(code.data()), std::streamsize(bytes)) || code[0] != 0x07230203u) {
        std::cerr << "Cannot read valid SPIR-V shader: " << printable << '\n';
        return false;
    }
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data(),
    };
    return check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module),
                 "Create shader module");
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, Buffer& buffer) {
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo allocation_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo result{};
    const auto allocator = graphics::internal::context.allocator;
    if (!check(vmaCreateBuffer(allocator, &info, &allocation_info, &buffer.handle, &buffer.allocation, &result),
               "Create mapped buffer")) {
        return false;
    }
    buffer.mapped = result.pMappedData;
    buffer.capacity = size;
    if (!buffer.mapped) {
        std::cerr << "Mapped buffer has no host address\n";
        return false;
    }
    if (data) {
        std::memcpy(buffer.mapped, data, size_t(size));
        // VMA handles atom-size alignment; this is a no-op on coherent memory.
        return check(vmaFlushAllocation(allocator, buffer.allocation, 0, size), "Flush buffer");
    }
    return true;
}

void destroyBuffer(Buffer& buffer) {
    if (buffer.handle != VK_NULL_HANDLE)
        vmaDestroyBuffer(internal::context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

bool deviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer.capacity = size;
    return check(vmaCreateBuffer(internal::context.allocator, &info, &alloc, &buffer.handle,
                                 &buffer.allocation, nullptr), "Allocate GPU buffer");
}

template<class Record>
bool upload(const void* data, VkDeviceSize size, Record record) {
    const auto& ctx = internal::context;
    Buffer staging;
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = upload_pool;
    alloc.commandBufferCount = 1;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool ok = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data, staging)
        && check(vkAllocateCommandBuffers(ctx.device, &alloc, &cmd), "Allocate upload command")
        && check(vkBeginCommandBuffer(cmd, &begin), "Begin upload");
    if (ok) {
        record(cmd, staging.handle);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        ok = check(vkEndCommandBuffer(cmd), "End upload")
            && check(vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE), "Submit upload")
            && check(vkQueueWaitIdle(ctx.graphics_queue), "Wait for upload");
    }
    if (cmd) vkFreeCommandBuffers(ctx.device, upload_pool, 1, &cmd);
    destroyBuffer(staging);
    return ok;
}

bool uploadMesh(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, Buffer& buffer) {
    if (!deviceBuffer(size, usage, buffer)) return false;
    return upload(data, size, [&](VkCommandBuffer cmd, VkBuffer src) {
        VkBufferCopy copy{0,0,size};
        vkCmdCopyBuffer(cmd, src, buffer.handle, 1, &copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    });
}

bool loadTexture(const char* name, VkFormat format, Texture& texture) {
    const auto path = shaderDirectory().parent_path() / "textures" / name;
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.empty() || bytes.size() > INT_MAX) {
        std::cerr << "Invalid texture file: " << name << '\n';
        return false;
    }
    int w, h, channels;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4), stbi_image_free);
    if (!pixels) { std::cerr << "Cannot load " << name << '\n'; return false; }
    const auto& ctx = internal::context;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {uint32_t(w), uint32_t(h), 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!check(vmaCreateImage(ctx.allocator, &info, &alloc, &texture.image, &texture.allocation, nullptr),
               "Create texture")) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = texture.image;
    view.format = format;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(ctx.device, &view, nullptr, &texture.view), "Create texture view")) return false;
    return upload(pixels.get(), VkDeviceSize(w)*h*4, [&](VkCommandBuffer cmd, VkBuffer src) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange = view.subresourceRange;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = info.extent;
        vkCmdCopyBufferToImage(cmd, src, texture.image, barrier.newLayout, 1, &copy);
        barrier.oldLayout = barrier.newLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
}

bool createTextures() {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.minFilter = info.magFilter = VK_FILTER_LINEAR;
    if (!check(vkCreateSampler(internal::context.device, &info, nullptr, &sampler), "Create sampler")) return false;
    const char* files[] = {"mona-lisa.png","spiral.png","shark.png","specular.png","emissive.png","black.png","doge.png"};
    for (int i = 0; i < 7; ++i) {
        if ((i == 3 || i == 4) && !std::filesystem::exists(shaderDirectory().parent_path() / "textures" / files[i]))
            continue;
        VkFormat format = i == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        if (!loadTexture(files[i], format, textures[i])) return false;
    }
    return true;
}

bool writeBuffer(Buffer& buffer, const void* data, VkDeviceSize size) {
    if (size == 0) return true;
    std::memcpy(buffer.mapped, data, size_t(size));
    return check(vmaFlushAllocation(internal::context.allocator, buffer.allocation, 0, size), "Flush buffer");
}

// вызывается после ожидания fence предыдущего кадра в internal::prepare().
bool ensureBufferSize(Buffer& buffer, VkDeviceSize needed, VkBufferUsageFlags usage) {
    if (needed <= buffer.capacity) return true;
    VkDeviceSize capacity = std::max(needed, buffer.capacity * 2);
    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        if (needed > max_storage_range) {
            std::cerr << "Light buffer exceeds maxStorageBufferRange\n";
            return false;
        }
        capacity = std::min(capacity, max_storage_range);
    }
    Buffer replacement;
    if (!createBuffer(capacity, usage, nullptr, replacement)) {
        destroyBuffer(replacement);
        return false;
    }
    destroyBuffer(buffer);
    buffer = replacement;
    return true;
}

// маркеры меняются каждый кадр. Обновляем staging после ожидания fence кадра
bool prepareMarkers(Buffer& gpu, Buffer& staging, const void* vertices, VkDeviceSize bytes) {
    if (bytes > gpu.capacity) {
        destroyBuffer(gpu);
        if (!deviceBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu)) return false;
    }
    return ensureBufferSize(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        && writeBuffer(staging, vertices, bytes);
}

void copyMarkers(VkCommandBuffer cmd, const Buffer& staging, const Buffer& gpu, VkDeviceSize size) {
    if (size == 0) return;
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.handle, gpu.handle, 1, &copy);
}

void updateDescriptors() {
    const VkDescriptorBufferInfo infos[] = {
        {scene_buffer.handle, 0, sizeof(SceneUniform)},
        {point_buffer.handle, 0, point_buffer.capacity},
        {spot_buffer.handle, 0, spot_buffer.capacity}
    };
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_sets[0];
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i == 1 || i == 2) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(internal::context.device, 3, writes, 0, nullptr);
}

bool createDescriptors() {
    const auto device = internal::context.device;
    const VkDescriptorSetLayoutBinding scene_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    const VkDescriptorSetLayoutBinding model_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    for (int i = 0; i < 2; ++i) {
        const VkDescriptorSetLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = i == 0 ? 3u : 4u,
            .pBindings = i == 0 ? scene_bindings : model_bindings
        };
        if (!check(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptor_layouts[i]), "Create descriptor layout"))
            return false;
    }
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 5, .poolSizeCount = 4, .pPoolSizes = sizes
    };
    if (!check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool), "Create descriptor pool")) return false;
    VkDescriptorSetLayout layouts[]{descriptor_layouts[0], descriptor_layouts[1], descriptor_layouts[1], descriptor_layouts[1], descriptor_layouts[1]};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool, .descriptorSetCount = 5, .pSetLayouts = layouts
    };
    if (!check(vkAllocateDescriptorSets(device, &allocate, descriptor_sets), "Allocate descriptors")) return false;
    updateDescriptors();
    const int maps[4][3] = {{0,5,5},{1,3,4},{2,5,5},{6,5,5}};
    VkDescriptorBufferInfo models{model_buffer.handle, 0, sizeof(ModelUniform)};
    for (int i = 0; i < 4; ++i) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor_sets[i+1];
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &models;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        for (int j = 0; j < 3; ++j) {
            VkImageView view = textures[maps[i][j]].view;
            VkDescriptorImageInfo image{sampler, view ? view : textures[5].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            write.dstBinding = j+1;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Effect)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = descriptor_layouts,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &range
    };
    return check(vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout), "Create pipeline layout");
}

bool createPipeline(const char* shader, bool marker, VkPrimitiveTopology topology, VkPipeline& output) {
    const auto& context = graphics::internal::context;
    const auto shader_directory = shaderDirectory();
    struct Shader {
        VkShaderModule handle = VK_NULL_HANDLE;
        ~Shader() {
            vkDestroyShaderModule(graphics::internal::context.device, handle, nullptr);
        }
    };
    Shader vertex;
    Shader fragment;
    if (!loadShader(shader_directory / (std::string(shader) + ".vert.spv"), vertex.handle)) {
        return false;
    }
    if (!loadShader(shader_directory / (std::string(shader) + ".frag.spv"), fragment.handle)) {
        return false;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex.handle, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment.handle, .pName = "main"},
    };
    const VkVertexInputBindingDescription vertex_binding{0, sizeof(ConeVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, position)},
        {marker ? 1u : 3u, 0, marker ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32_SFLOAT, marker ? uint32_t(offsetof(ConeVertex, color)) : uint32_t(offsetof(ConeVertex, uv))},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, normal)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = marker ? 2u : 3u, .pVertexAttributeDescriptions = attributes,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = marker ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineColorBlendAttachmentState color_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &color_attachment,
    };
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport, .pRasterizationState = &rasterization,
        .pMultisampleState = &multisampling, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .pDynamicState = &dynamic,
        .layout = pipeline_layout, .renderPass = context.render_pass,
    };
    return check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &output),
                 "Create graphics pipeline");
}

} // namespace

bool initializeCone(const application::ConeMesh& mesh) {
    try {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(internal::context.physical_device, &properties);
        max_storage_range = properties.limits.maxStorageBufferRange;
        index_count = uint32_t(mesh.indices.size());
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = internal::context.graphics_queue_index;
        const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
        model_stride = (sizeof(ModelUniform) + alignment - 1) / alignment * alignment;
        if (check(vkCreateCommandPool(internal::context.device,&pool,nullptr,&upload_pool), "Create upload pool") &&
            createTextures() && uploadMesh(mesh.vertices.size() * sizeof(ConeVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices.data(), vertex_buffer) &&
            uploadMesh(mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices.data(), index_buffer) &&
            createBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, scene_buffer) &&
            createBuffer(model_stride * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, model_buffer) &&
            createBuffer(sizeof(PointLightGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr, point_buffer) &&
            createBuffer(sizeof(SpotLightGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr, spot_buffer) &&
            createDescriptors() &&
            createPipeline("cone", false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pipeline) &&
            createPipeline("marker", true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, marker_pipeline) &&
            createPipeline("marker", true, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, line_pipeline)) return true;
    } catch (const std::exception& error) {
        std::cerr << "Resource initialization failed: " << error.what() << '\n';
    }
    shutdownCone();
    return false;
}

void shutdownCone() {
    const auto& context = internal::context;
    if (context.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(context.device);
    for (VkPipeline* p : {&pipeline, &marker_pipeline, &line_pipeline}) {
        vkDestroyPipeline(context.device, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    for (int i = 0; i < 2; ++i) {
        vkDestroyDescriptorSetLayout(context.device, descriptor_layouts[i], nullptr);
        descriptor_layouts[i] = VK_NULL_HANDLE;
        descriptor_sets[i] = VK_NULL_HANDLE;
    }
    for (Buffer* b : {&vertex_buffer, &index_buffer, &scene_buffer, &model_buffer,
                      &point_buffer, &spot_buffer, &marker_buffer, &line_buffer, &marker_staging, &line_staging}) destroyBuffer(*b);
    for (Texture& tex : textures) {
        vkDestroyImageView(context.device, tex.view, nullptr);
        if (tex.image) vmaDestroyImage(context.allocator, tex.image, tex.allocation);
        tex = {};
    }
    vkDestroySampler(context.device, sampler, nullptr);
    vkDestroyCommandPool(context.device, upload_pool, nullptr);
    sampler = VK_NULL_HANDLE;
    upload_pool = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    descriptor_pool = VK_NULL_HANDLE;
    index_count = 0;
}

bool renderScene(const internal::FrameData& fd, const SceneUniform& scene, const ModelUniform (&models)[2], const int (&materials)[2], bool animated, float time,
                 const std::vector<PointLightGpu>& points, const std::vector<SpotLightGpu>& spots,
                 const application::LightMarkers& markers) {
    const auto& context = internal::context;
    if (fd.command_buffer == VK_NULL_HANDLE || fd.framebuffer == VK_NULL_HANDLE ||
        context.swapchain_extent.width == 0 || context.swapchain_extent.height == 0) return false;
    if (!check(vkResetCommandBuffer(fd.command_buffer, 0), "Reset command buffer")) return false;

    // при пустом массиве буфер всё ещё содержит место для одного элемента, а счётчик равен нулю.
    const VkDeviceSize point_bytes = points.size() * sizeof(PointLightGpu);
    const VkDeviceSize spot_bytes = spots.size() * sizeof(SpotLightGpu);
    const VkDeviceSize marker_bytes = markers.triangles.size() * sizeof(ConeVertex);
    const VkDeviceSize line_bytes = markers.lines.size() * sizeof(ConeVertex);
    const bool lights_resized = point_bytes > point_buffer.capacity || spot_bytes > spot_buffer.capacity;
    if (!ensureBufferSize(point_buffer, point_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !ensureBufferSize(spot_buffer, spot_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !prepareMarkers(marker_buffer, marker_staging, markers.triangles.data(), marker_bytes) ||
        !prepareMarkers(line_buffer, line_staging, markers.lines.data(), line_bytes)) return false;
    if (lights_resized) updateDescriptors();
    for (int i = 0; i < 2; ++i)
        std::memcpy(static_cast<char*>(model_buffer.mapped) + i * model_stride, &models[i], sizeof(ModelUniform));
    if (!check(vmaFlushAllocation(context.allocator, model_buffer.allocation, 0, model_buffer.capacity), "Flush models")) return false;
    if (!writeBuffer(scene_buffer, &scene, sizeof(scene)) ||
        !writeBuffer(point_buffer, points.data(), point_bytes) || !writeBuffer(spot_buffer, spots.data(), spot_bytes)) return false;

    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (!check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin command buffer")) return false;
    if (marker_bytes || line_bytes) {
        copyMarkers(fd.command_buffer, marker_staging, marker_buffer, marker_bytes);
        copyMarkers(fd.command_buffer, line_staging, line_buffer, line_bytes);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(fd.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    const VkClearValue clear[] = {
        {.color = {{0.025f, 0.035f, 0.055f, 1.0f}}}, {.depthStencil = {1.0f, 0}}
    };
    const VkRenderPassBeginInfo render_pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass, .framebuffer = fd.framebuffer,
        .renderArea = {.extent = context.swapchain_extent}, .clearValueCount = 2, .pClearValues = clear
    };
    vkCmdBeginRenderPass(fd.command_buffer, &render_pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0, 0, float(context.swapchain_extent.width), float(context.swapchain_extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, context.swapchain_extent};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                            0, 1, descriptor_sets, 0, nullptr);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
    for (int i = 0; i < 2; ++i) {
        uint32_t dynamic_offset = uint32_t(i * model_stride);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                1, 1, &descriptor_sets[1 + materials[i]], 1, &dynamic_offset);
        Effect effect{i == 0 && animated, time};
        vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(effect), &effect);
        vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
    }

    if (marker_bytes != 0) {
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marker_pipeline);
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &marker_buffer.handle, &offset);
        vkCmdDraw(fd.command_buffer, uint32_t(markers.triangles.size()), 1, 0, 0);
    }
    if (line_bytes != 0) {
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline);
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &line_buffer.handle, &offset);
        vkCmdDraw(fd.command_buffer, uint32_t(markers.lines.size()), 1, 0, 0);
    }
    vkCmdEndRenderPass(fd.command_buffer);
    return check(vkEndCommandBuffer(fd.command_buffer), "End command buffer");
}
} // namespace graphics
