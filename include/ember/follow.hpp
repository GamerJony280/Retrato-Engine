#pragma once

#include "math.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ember {

struct FollowCam {
    Vec3 current{};
    Vec3 bounds_lo{-1e30f, -1e30f, -1e30f};
    Vec3 bounds_hi{1e30f, 1e30f, 1e30f};
    float smooth = 8;
    float trauma = 0;
    float trauma_decay = 2.5f;

    void snap(Vec3 focus) {
        require_finite(focus, "Camera focus must be finite");
        current = focus;
    }

    void add_shake(float amount) {
        if (!std::isfinite(amount) || amount < 0)
            throw std::invalid_argument("Shake amount must be finite and non-negative");
        trauma = std::clamp(trauma + amount, 0.f, 1.f);
    }

    Vec3 update(Vec3 focus, float dt) {
        require_finite(focus, "Camera focus must be finite");
        if (!std::isfinite(dt) || dt < 0 || !finite(current) || !finite(bounds_lo) || !finite(bounds_hi) ||
            !std::isfinite(smooth) || smooth < 0 || !std::isfinite(trauma) || trauma < 0 || trauma > 1 ||
            !std::isfinite(trauma_decay) || trauma_decay < 0 || bounds_lo.x > bounds_hi.x ||
            bounds_lo.y > bounds_hi.y || bounds_lo.z > bounds_hi.z)
            throw std::invalid_argument("Invalid follow-camera configuration");
        focus.x = std::clamp(focus.x, bounds_lo.x, bounds_hi.x);
        focus.y = std::clamp(focus.y, bounds_lo.y, bounds_hi.y);
        focus.z = std::clamp(focus.z, bounds_lo.z, bounds_hi.z);
        const float t = 1 - std::exp(-smooth * dt);
        current = current + (focus - current) * t;
        trauma = std::max(0.f, trauma - trauma_decay * dt);
        return current;
    }

    Vec3 shake_offset(float time, float magnitude = 0.35f) const {
        if (!std::isfinite(time) || !std::isfinite(magnitude) || magnitude < 0 || !std::isfinite(trauma) ||
            trauma < 0 || trauma > 1)
            throw std::invalid_argument("Invalid camera-shake parameters");
        const float energy = trauma * trauma * magnitude;
        if (energy <= 0)
            return {};
        return {std::sin(time * 61.7f) * energy, std::sin(time * 53.3f + 1.3f) * energy, 0};
    }

    Camera camera(Vec3 offset, float time) const {
        require_finite(offset, "Camera offset must be finite");
        Camera camera;
        const Vec3 shake = shake_offset(time);
        camera.target = current + shake;
        camera.position = current + offset + shake;
        return camera;
    }

  private:
    static void require_finite(Vec3 value, const char *message) {
        if (!finite(value))
            throw std::invalid_argument(message);
    }
    static bool finite(Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
};

} // namespace ember
