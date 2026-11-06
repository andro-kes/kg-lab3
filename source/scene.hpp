#pragma once
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

namespace application {
struct Camera {
    glm::vec3 position{3.2f, 2.2f, 4.4f};
    float yaw = -126.0f, pitch = -22.0f, fov = 45.0f, ortho_height = 2.4f;
    int projection = 0;
};
struct Material {
    glm::vec3 albedo{0.65f, 0.7f, 0.8f};
    glm::vec3 specular{0.8f};
    float shininess = 48.0f;
    int textures[2]{1, 0};
    bool effect = true;
};
struct PointLight {
    glm::vec3 position{2.0f, 1.5f, 1.5f};
    glm::vec3 color{1.0f, 0.55f, 0.25f};
    float intensity = 8.0f;
};
struct SpotLight {
    glm::vec3 position{-2.0f, 2.0f, 2.0f};
    glm::vec3 direction{2.0f, -2.0f, -2.0f};
    glm::vec3 color{0.3f, 0.6f, 1.0f};
    float intensity = 18.0f;
    float inner_angle = 15.0f, outer_angle = 25.0f;
};
struct Lighting {
    glm::vec3 ambient_color{1.0f};
    float ambient_intensity = 0.08f;
    glm::vec3 direction{-0.5f, -1.0f, -0.4f};
    glm::vec3 directional_color{1.0f};
    float directional_intensity = 0.4f;
    std::vector<PointLight> points{PointLight{}};
    std::vector<SpotLight> spots{SpotLight{}};
    bool show_markers = true, show_cones = true;
};
inline glm::vec3 normalizedOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float d = glm::dot(v, v);
    return d > 0.000001f ? v / std::sqrt(d) : fallback;
}
inline glm::vec3 cameraForward(const Camera& camera) {
    const float yaw = glm::radians(camera.yaw), pitch = glm::radians(camera.pitch);
    return {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
}
} // namespace application
