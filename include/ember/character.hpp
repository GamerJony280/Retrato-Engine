#pragma once

#include "collision.hpp"
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ember {

struct CharacterStep {
    bool grounded = false;
    bool hit_wall = false;
    bool hit_head = false;
    Vec3 ground_normal{0, 1, 0};
    Vec3 wall_normal{};
};

// Lightweight arcade character controller over static AABBs. Horizontal
// motion (X/Z) is solved before vertical motion so walls cannot be mistaken
// for floors. Games retain policy such as acceleration, jump counts and
// animation state; this type owns only movement and contact state.
struct CharacterBody {
    Vec3 position{};
    Vec3 velocity{};
    float radius = 0.35f;
    float coyote_left = 0;
    float jump_buffer = 0;
    bool grounded = false;
    Vec3 ground_normal{0, 1, 0};
    bool hit_wall = false;
    Vec3 last_wall_normal{};

    void queue_jump(float buffer_time) {
        if (!std::isfinite(buffer_time) || buffer_time < 0)
            throw std::invalid_argument("Jump buffer must be finite and non-negative");
        jump_buffer = std::max(jump_buffer, buffer_time);
    }

    void cut_jump(float max_rise) {
        if (!std::isfinite(max_rise) || max_rise < 0)
            throw std::invalid_argument("Jump cut speed must be finite and non-negative");
        velocity.y = std::min(velocity.y, max_rise);
    }

    void air_jump(float speed) {
        if (!std::isfinite(speed))
            throw std::invalid_argument("Air-jump speed must be finite");
        velocity.y = speed;
    }

    bool update(const std::vector<Aabb> &solids, float dt, float gravity, float jump_speed,
                float coyote_time, float max_fall = 20) {
        if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(gravity) || gravity < 0 ||
            !std::isfinite(jump_speed) || !std::isfinite(coyote_time) || coyote_time < 0 ||
            !std::isfinite(max_fall) || max_fall < 0)
            throw std::invalid_argument("Invalid character step parameters");
        validate_body();

        bool jumped = false;
        if (jump_buffer > 0 && (grounded || coyote_left > 0)) {
            velocity.y = jump_speed;
            jump_buffer = 0;
            coyote_left = 0;
            grounded = false;
            jumped = true;
        }
        jump_buffer = std::max(0.f, jump_buffer - dt);
        coyote_left = std::max(0.f, coyote_left - dt);
        velocity.y = std::max(-max_fall, velocity.y - gravity * dt);

        CharacterStep step = move_collide(solids, velocity * dt);
        grounded = step.grounded;
        hit_wall = step.hit_wall;
        last_wall_normal = step.wall_normal;
        if (grounded) {
            ground_normal = step.ground_normal;
            coyote_left = coyote_time;
            velocity.y = 0;
        }
        return jumped;
    }

    CharacterStep move_collide(const std::vector<Aabb> &solids, Vec3 delta, int iterations = 4) {
        validate_body();
        if (!finite(delta) || iterations < 1 || iterations > 16)
            throw std::invalid_argument("Invalid character movement");

        CharacterStep out;
        auto slide = slide_sphere_boxes(position, {delta.x, 0, delta.z}, radius, solids, iterations);
        position = slide.position;
        if (slide.contacts > 0 && std::fabs(slide.normal.y) < 0.75f) {
            out.hit_wall = true;
            out.wall_normal = slide.normal;
            const float inward = dot(velocity, slide.normal);
            if (inward < 0)
                velocity = velocity - slide.normal * inward;
        }

        slide = slide_sphere_boxes(position, {0, delta.y, 0}, radius, solids, iterations);
        position = slide.position;
        if (slide.contacts > 0 && slide.normal.y > 0.5f) {
            out.grounded = true;
            out.ground_normal = slide.normal;
        } else if (slide.contacts > 0 && slide.normal.y < -0.5f) {
            out.hit_head = true;
            velocity.y = std::min(velocity.y, 0.f);
        }
        return out;
    }

  private:
    static bool finite(Vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    void validate_body() const {
        if (!finite(position) || !finite(velocity) || !std::isfinite(radius) || radius <= 0 ||
            !std::isfinite(coyote_left) || coyote_left < 0 || !std::isfinite(jump_buffer) || jump_buffer < 0)
            throw std::invalid_argument("Invalid character body state");
    }
};

} // namespace ember
