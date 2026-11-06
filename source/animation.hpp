#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace application {

struct Animation {
    bool playing = false;
    double phase = 0.0;
    float speed = 1.0f;
    float radius = 0.9f;
    float height = 0.35f;
    float depth = 0.45f;
};

struct AnimationPose {
    std::array<float, 3> position;
    std::array<float, 3> rotation;
};

inline void advanceAnimation(Animation& animation, double elapsed) {
    if (!animation.playing || !std::isfinite(elapsed) || elapsed <= 0.0) {
        return;
    }

    // После долгой задержки ограничиваем шаг, чтобы избежать скачка
    if (elapsed > 0.1) {
        elapsed = 0.1;
    }

    animation.phase += elapsed * animation.speed;

    // После полного оборота фаза снова начинается с нуля
    const double full_turn = 2.0 * std::numbers::pi;
    animation.phase = std::fmod(animation.phase, full_turn);
}

inline AnimationPose animationPose(const Animation& animation) {
    const double t = animation.phase;
    AnimationPose pose{};

    // Смещение по трём осям: восьмёрка в XY и колебание по Z.
    pose.position[0] = animation.radius * float(std::sin(t));
    pose.position[1] = animation.height * float(std::sin(2.0 * t));
    pose.position[2] = animation.depth * float(std::sin(3.0 * t));

    // Углы поворота вокруг X, Y и Z в градусах.
    pose.rotation[0] = 25.0f * float(std::sin(2.0 * t));
    pose.rotation[1] = float(t * 180.0 / std::numbers::pi);
    pose.rotation[2] = 15.0f * float(std::sin(3.0 * t));

    return pose;
}

} // namespace application
