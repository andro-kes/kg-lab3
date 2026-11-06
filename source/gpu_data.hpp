#pragma once
#include <cstddef>
#include <glm/glm.hpp>

namespace graphics {
// vec4 упрощает одинаковое расположение полей в C++ и GLSL.
struct alignas(16) SceneUniform {
    glm::mat4 view_projection{1.0f};
    glm::vec4 camera_position{0.0f}; // w: 1 для перспективы, 0 для ортографии
    glm::vec4 view_direction{0.0f};
    glm::vec4 ambient{0.0f};
    glm::vec4 light_direction{0.0f};
    glm::vec4 light_color{0.0f};
    glm::uvec4 light_counts{0};
};
struct alignas(16) ModelUniform {
    glm::mat4 model{1.0f};
    glm::mat4 normal_matrix{1.0f};
    glm::vec4 albedo{1.0f}; // Множитель цвета текстуры
    glm::vec4 specular{1.0f}; // w: показатель блеска
};
struct alignas(16) PointLightGpu {
    glm::vec4 position{0.0f};
    glm::vec4 color_intensity{0.0f};
};
struct alignas(16) SpotLightGpu {
    glm::vec4 position{0.0f};
    glm::vec4 direction{0.0f};
    glm::vec4 color_intensity{0.0f};
    glm::vec4 cone{0.0f}; // cos внутреннего и внешнего полууглов
};
static_assert(sizeof(SceneUniform) == 160);
static_assert(offsetof(SceneUniform, camera_position) == 64);
static_assert(offsetof(SceneUniform, view_direction) == 80);
static_assert(offsetof(SceneUniform, ambient) == 96);
static_assert(offsetof(SceneUniform, light_direction) == 112);
static_assert(offsetof(SceneUniform, light_color) == 128);
static_assert(offsetof(SceneUniform, light_counts) == 144);
static_assert(sizeof(ModelUniform) == 160);
static_assert(offsetof(ModelUniform, normal_matrix) == 64);
static_assert(offsetof(ModelUniform, albedo) == 128);
static_assert(offsetof(ModelUniform, specular) == 144);
static_assert(sizeof(PointLightGpu) == 32);
static_assert(offsetof(PointLightGpu, color_intensity) == 16);
static_assert(sizeof(SpotLightGpu) == 64);
static_assert(offsetof(SpotLightGpu, direction) == 16);
static_assert(offsetof(SpotLightGpu, color_intensity) == 32);
static_assert(offsetof(SpotLightGpu, cone) == 48);
} // namespace graphics
