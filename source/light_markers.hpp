#pragma once
#include "cone_mesh.hpp"
#include "scene.hpp"

namespace application {
struct LightMarkers {
    std::vector<ConeVertex> triangles;
    std::vector<ConeVertex> lines;
};
inline void addLightCube(LightMarkers& markers, const glm::vec3& position, const glm::vec3& color) {
    const glm::vec3 corners[] = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
        {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}
    };
    const int indices[] = {
        0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4,
        3,7,6, 3,6,2, 0,4,7, 0,7,3, 1,2,6, 1,6,5
    };
    for (int i : indices)
        markers.triangles.push_back({position + 0.08f * corners[i], color, glm::vec3(0)});
}
inline void addLine(LightMarkers& markers, const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
    markers.lines.push_back({a, color, glm::vec3(0)});
    markers.lines.push_back({b, color, glm::vec3(0)});
}
inline void addSpotCone(LightMarkers& markers, const SpotLight& light, float angle, const glm::vec3& color) {
    const glm::vec3 direction = normalizedOr(light.direction, glm::vec3(0, -1, 0));
    const glm::vec3 helper = std::abs(direction.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(direction, helper));
    const glm::vec3 up = glm::cross(right, direction);
    constexpr float length = 1.4f;
    const glm::vec3 center = light.position + length * direction;
    const float radius = length * std::tan(glm::radians(angle));
    constexpr int segments = 32;
    for (int i = 0; i < segments; ++i) {
        const float a = 2 * std::numbers::pi_v<float> * float(i) / segments;
        const float b = 2 * std::numbers::pi_v<float> * float(i + 1) / segments;
        const glm::vec3 p = center + radius * (std::cos(a) * right + std::sin(a) * up);
        const glm::vec3 q = center + radius * (std::cos(b) * right + std::sin(b) * up);
        addLine(markers, p, q, color);
        if (i % 8 == 0) addLine(markers, light.position, p, color);
    }
    addLine(markers, light.position, center, color);
}
inline LightMarkers makeLightMarkers(const Lighting& lighting) {
    LightMarkers result;
    if (!lighting.show_markers) return result;
    for (const PointLight& light : lighting.points) addLightCube(result, light.position, light.color);
    for (const SpotLight& light : lighting.spots) {
        addLightCube(result, light.position, light.color);
        if (lighting.show_cones) {
            addSpotCone(result, light, light.outer_angle, glm::vec3(1));
            addSpotCone(result, light, light.inner_angle, glm::vec3(0.45f));
        }
    }
    return result;
}
} // namespace application

