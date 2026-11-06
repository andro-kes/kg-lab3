#pragma once
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>
#include <glm/glm.hpp>

namespace application {
struct ConeVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv{};
};
struct ConeMesh {
    std::vector<ConeVertex> vertices;
    std::vector<uint32_t> indices;
};
inline constexpr uint32_t cone_segments = 64;
inline constexpr float cone_radius = 1.0f, cone_height = 2.0f;

inline ConeMesh makeConeMesh() {
    ConeMesh mesh;
    const glm::vec3 top(0, cone_height / 2, 0), center(0, -cone_height / 2, 0), down(0, -1, 0);
    for (uint32_t i = 0; i < cone_segments; ++i) {
        const float a = 2 * std::numbers::pi_v<float> * float(i) / float(cone_segments);
        const float b = 2 * std::numbers::pi_v<float> * float(i + 1) / float(cone_segments);
        const glm::vec3 p(cone_radius * std::cos(a), center.y, cone_radius * std::sin(a));
        const glm::vec3 q(cone_radius * std::cos(b), center.y, cone_radius * std::sin(b));
        const glm::vec3 np = glm::normalize(glm::vec3(std::cos(a), cone_radius / cone_height, std::sin(a)));
        const glm::vec3 nq = glm::normalize(glm::vec3(std::cos(b), cone_radius / cone_height, std::sin(b)));
        const glm::vec3 nt = glm::normalize(np + nq);
        const uint32_t first = uint32_t(mesh.vertices.size());
        const float u = float(i) / cone_segments, v = float(i + 1) / cone_segments;
        auto diskUV = [](glm::vec3 pos) { return glm::vec2(0.5f) + glm::vec2(pos.x,pos.z) / (2 * cone_radius); };
        mesh.vertices.insert(mesh.vertices.end(), {
            {top, {}, nt, {(u+v)/2, 0}}, {q, {}, nq, {v, 1}}, {p, {}, np, {u, 1}},
            {center, {}, down, diskUV(center)}, {p, {}, down, diskUV(p)}, {q, {}, down, diskUV(q)}
        });
        for (uint32_t j = 0; j < 6; ++j) mesh.indices.push_back(first + j);
    }
    return mesh;
}
} // namespace application

