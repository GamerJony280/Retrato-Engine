#pragma once
#include "vfx.hpp"

// Optional editable recipes, not particle types or engine policies. Returning
// a plain Burst lets any game change every parameter before emission.
namespace ember::vfx::recipes {
inline Vec3 axis(Vec3 v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        throw std::invalid_argument("Invalid VFX recipe axis");
    double n = std::sqrt(double(v.x)*v.x + double(v.y)*v.y + double(v.z)*v.z);
    if (n < 1e-8) throw std::invalid_argument("VFX recipe axis cannot be zero");
    return {float(v.x/n), float(v.y/n), float(v.z/n)};
}

inline Burst planar_sparks(Vec3 color, float speed = 4, Vec3 plane = {0,0,1}, Vec3 up = {0,1,0}) {
    Burst b;
    b.pattern = Pattern::Ring;
    b.direction = axis(plane);
    up = up - b.direction * dot(up, b.direction);
    b.up = axis(up);
    b.arc_max = 3.1415927f; // recipe chooses the upward half of a general ring
    b.speed = speed;
    b.speed_min_factor = .4f;
    b.particle.color = color;
    b.particle.acceleration = b.up * -20;
    b.particle.plane_normal = b.direction;
    return b;
}

inline Burst running_dust(Vec3 surface_normal, Vec3 motion, Vec3 color = {.8f,.75f,.7f}) {
    Burst b;
    b.count = 2;
    b.pattern = Pattern::Cone;
    b.direction = axis(surface_normal);
    b.cone_angle = 1;
    b.speed = 1.2f;
    b.speed_min_factor = .4f;
    b.particle.color = color;
    b.particle.velocity = motion * -.08f;
    b.particle.acceleration = b.direction * -20;
    b.life_min = .5f; b.life_max = .9f;
    return b;
}
} // namespace ember::vfx::recipes
