#include "application.hpp"
#include "animation.hpp"
#include "graphics.hpp"
#include "scene.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <imgui.h>

namespace application {
namespace {
struct Controls {
    Camera camera;
    Material material;
    Lighting lighting;
    glm::vec3 position{0}, rotation{0}, scale{1};
    Animation animation;
};
Controls controls;
double previous_time = -1;

glm::mat4 makeModel(int cone) {
    const AnimationPose pose = animationPose(controls.animation);
    const glm::vec3 offset(pose.position[0], pose.position[1], pose.position[2]);
    const glm::vec3 angles = controls.rotation + glm::vec3(pose.rotation[0], pose.rotation[1], pose.rotation[2]);
    glm::mat4 model = glm::translate(glm::mat4(1), controls.position + offset + glm::vec3(cone == 0 ? -1.1f : 1.1f, 0, 0));
    model = glm::rotate(model, glm::radians(angles.x), glm::vec3(1,0,0));
    model = glm::rotate(model, glm::radians(angles.y), glm::vec3(0,1,0));
    model = glm::rotate(model, glm::radians(angles.z), glm::vec3(0,0,1));
    return glm::scale(model, controls.scale);
}

glm::mat4 makeViewProjection(VkExtent2D size) {
    const Camera& camera = controls.camera;
    const glm::mat4 view = glm::lookAtRH(camera.position, camera.position + cameraForward(camera), glm::vec3(0,1,0));
    const float aspect = float(size.width) / float(size.height);
    glm::mat4 projection;
    if (camera.projection == 0)
        projection = glm::perspectiveRH_ZO(glm::radians(camera.fov), aspect, 0.1f, 100.0f);
    else
        projection = glm::orthoRH_ZO(-camera.ortho_height * aspect, camera.ortho_height * aspect,
                                   -camera.ortho_height, camera.ortho_height, 0.1f, 100.0f);
    projection[1][1] *= -1;
    return projection * view;
}

void cameraUi() {
    Camera& camera = controls.camera;
    ImGui::DragFloat3("Camera position", &camera.position.x, 0.05f, -30, 30, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Yaw", &camera.yaw, -180, 180, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Pitch", &camera.pitch, -89, 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Combo("Projection", &camera.projection, "Perspective\0Orthographic\0");
    if (camera.projection == 0)
        ImGui::SliderFloat("Field of view", &camera.fov, 15, 100, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    else
        ImGui::SliderFloat("Ortho half height", &camera.ortho_height, 0.5f, 15, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset camera")) camera = {};
}

void modelUi() {
    ImGui::DragFloat3("Position", &controls.position.x, 0.02f, -10, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat3("Rotation", &controls.rotation.x, -180, 180, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat3("Scale", &controls.scale.x, 0.1f, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    Material& material = controls.material;
    ImGui::ColorEdit3("Albedo", &material.albedo.x);
    ImGui::ColorEdit3("Specular color", &material.specular.x);
    ImGui::SliderFloat("Shininess", &material.shininess, 1, 256, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    const char* materials[] = {"Mona Lisa", "Spiral", "Shark", "Doge"};
    ImGui::Combo("First cone", &material.textures[0], materials, IM_ARRAYSIZE(materials));
    ImGui::Combo("Second cone", &material.textures[1], materials, IM_ARRAYSIZE(materials));
    ImGui::Checkbox("Animate first texture", &material.effect);
    if (ImGui::TreeNode("Animation")) {
        Animation& animation = controls.animation;
        if (ImGui::Button(animation.playing ? "Pause" : "Play")) animation.playing = !animation.playing;
        ImGui::SameLine();
        if (ImGui::Button("Reset motion")) { animation.phase = 0; animation.playing = false; }
        ImGui::SliderFloat("Speed", &animation.speed, 0.1f, 3, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Path radius", &animation.radius, 0, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Vertical range", &animation.height, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Depth range", &animation.depth, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TreePop();
    }
}

void globalLightUi() {
    Lighting& lighting = controls.lighting;
    ImGui::ColorEdit3("Ambient color", &lighting.ambient_color.x);
    ImGui::SliderFloat("Ambient intensity", &lighting.ambient_intensity, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat3("Direction", &lighting.direction.x, 0.02f, -10, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Directional color", &lighting.directional_color.x);
    ImGui::SliderFloat("Directional intensity", &lighting.directional_intensity, 0, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled("Direction: from light into scene.");
    ImGui::TextDisabled("Zero direction falls back to (0, -1, 0).");
}

void pointLightsUi() {
    auto& lights = controls.lighting.points;
    if (ImGui::Button("Add point")) {
        PointLight light;
        light.position.x += 0.3f * float(lights.size() % 6);
        lights.push_back(light);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove all points")) lights.clear();
    size_t remove_index = lights.size();
    for (size_t i = 0; i < lights.size(); ++i) {
        ImGui::PushID(int(i));
        bool remove = false;
        if (ImGui::TreeNode("point", "Point %zu", i + 1)) {
            PointLight& light = lights[i];
            ImGui::DragFloat3("Position", &light.position.x, 0.03f, -20, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::SliderFloat("Intensity", &light.intensity, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            remove = ImGui::Button("Remove point");
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove) remove_index = i;
    }
    // Keep indices unchanged while submitting widgets: each ID is used once per frame.
    if (remove_index < lights.size()) lights.erase(lights.begin() + remove_index);
}

void spotLightsUi() {
    auto& lights = controls.lighting.spots;
    if (ImGui::Button("Add spot")) {
        SpotLight light;
        light.position.x -= 0.3f * float(lights.size() % 6);
        light.direction = -light.position;
        lights.push_back(light);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove all spots")) lights.clear();
    size_t remove_index = lights.size();
    for (size_t i = 0; i < lights.size(); ++i) {
        ImGui::PushID(int(i));
        bool remove = false;
        if (ImGui::TreeNode("spot", "Spot %zu", i + 1)) {
            SpotLight& light = lights[i];
            ImGui::DragFloat3("Position", &light.position.x, 0.03f, -20, 20, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat3("Direction", &light.direction.x, 0.02f, -10, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::SliderFloat("Intensity", &light.intensity, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Inner angle", &light.inner_angle, 0, light.outer_angle, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Outer angle", &light.outer_angle, light.inner_angle, 80, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextDisabled("Angles are measured from the cone axis.");
            if (ImGui::Button("Aim at first cone")) light.direction = glm::vec3(makeModel(0)[3]) - light.position;
            if (ImGui::Button("Aim at second cone")) light.direction = glm::vec3(makeModel(1)[3]) - light.position;
            ImGui::SameLine();
            remove = ImGui::Button("Remove spot");
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove) remove_index = i;
    }
    if (remove_index < lights.size()) lights.erase(lights.begin() + remove_index);
}
} // namespace

bool initialize() {
    try {
        return graphics::initializeCone(makeConeMesh());
    } catch (const std::exception& error) {
        std::cerr << "Application initialization failed: " << error.what() << '\n';
        return false;
    }
}
void shutdown() { graphics::shutdownCone(); }

void update(double time) {
    const double elapsed = previous_time < 0 ? 0 : time - previous_time;
    previous_time = time;
    advanceAnimation(controls.animation, elapsed);
    ImGui::SetNextWindowPos(ImVec2(16,16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(365, std::max(250.0f, ImGui::GetIO().DisplaySize.y - 32)), ImGuiCond_Always);
    ImGui::Begin("Lab 3 - Texture mapping");
    ImGui::Text("Cone | %u segments | Blinn-Phong", cone_segments);
    ImGui::Text("Point lights: %zu  Spot lights: %zu", controls.lighting.points.size(), controls.lighting.spots.size());
    ImGui::Checkbox("Show light markers", &controls.lighting.show_markers);
    ImGui::Checkbox("Show spotlight cones", &controls.lighting.show_cones);
    if (ImGui::Button("Reset all")) controls = {};
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera")) cameraUi();
    if (ImGui::CollapsingHeader("Object and material", ImGuiTreeNodeFlags_DefaultOpen)) modelUi();
    if (ImGui::CollapsingHeader("Ambient and directional light")) globalLightUi();
    if (ImGui::CollapsingHeader("Point lights")) pointLightsUi();
    if (ImGui::CollapsingHeader("Spotlights")) spotLightsUi();
    ImGui::End();
}

bool render(const graphics::internal::FrameData& frame) {
    const VkExtent2D size = graphics::internal::context.swapchain_extent;
    if (size.width == 0 || size.height == 0) return false;
    const Lighting& lighting = controls.lighting;
    graphics::SceneUniform scene;
    scene.view_projection = makeViewProjection(size);
    scene.camera_position = glm::vec4(controls.camera.position, controls.camera.projection == 0 ? 1.0f : 0.0f);
    scene.view_direction = glm::vec4(cameraForward(controls.camera), 0);
    scene.ambient = glm::vec4(lighting.ambient_color * lighting.ambient_intensity, 0);
    scene.light_direction = glm::vec4(normalizedOr(lighting.direction, glm::vec3(0,-1,0)), 0);
    scene.light_color = glm::vec4(lighting.directional_color * lighting.directional_intensity, 0);
    scene.light_counts = glm::uvec4(lighting.points.size(), lighting.spots.size(), 0, 0);

    graphics::ModelUniform models[2];
    for (int i = 0; i < 2; ++i) {
        models[i].model = makeModel(i);
        models[i].normal_matrix = glm::transpose(glm::inverse(models[i].model));
        models[i].albedo = glm::vec4(controls.material.albedo, 1.0f);
        models[i].specular = glm::vec4(controls.material.specular, controls.material.shininess);
    }
    std::vector<graphics::PointLightGpu> points;
    for (const PointLight& light : lighting.points)
        points.push_back({glm::vec4(light.position, 1), glm::vec4(light.color, light.intensity)});
    std::vector<graphics::SpotLightGpu> spots;
    for (const SpotLight& light : lighting.spots) {
        spots.push_back({
            glm::vec4(light.position, 1),
            glm::vec4(normalizedOr(light.direction, glm::vec3(0,-1,0)), 0),
            glm::vec4(light.color, light.intensity),
            glm::vec4(std::cos(glm::radians(light.inner_angle)), std::cos(glm::radians(light.outer_angle)), 0, 0)
        });
    }
    return graphics::renderScene(frame, scene, models, controls.material.textures,
                                 controls.material.effect, float(ImGui::GetTime()), points, spots, makeLightMarkers(lighting));
}
} // namespace application

