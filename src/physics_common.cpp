#include "ember/physics.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace ember {
namespace {
void require_positive(float value, const char *name) {
    if (!std::isfinite(value) || value <= 0)
        throw std::runtime_error(std::string(name) + " must be finite and positive");
}
} // namespace

BodyShape BodyShape::box(Vec3 half_extents) {
    if (!std::isfinite(half_extents.x) || !std::isfinite(half_extents.y) || !std::isfinite(half_extents.z) ||
        half_extents.x <= 0 || half_extents.y <= 0 || half_extents.z <= 0)
        throw std::runtime_error("Physics box half extents must be finite and positive");
    BodyShape out;
    out.type = BodyShapeType::Box;
    out.half_extents = half_extents;
    return out;
}
BodyShape BodyShape::sphere(float radius) {
    require_positive(radius, "Physics sphere radius");
    BodyShape out;
    out.type = BodyShapeType::Sphere;
    out.radius = radius;
    return out;
}
BodyShape BodyShape::capsule(float half_height, float radius) {
    require_positive(half_height, "Physics capsule half-height");
    require_positive(radius, "Physics capsule radius");
    BodyShape out;
    out.type = BodyShapeType::Capsule;
    out.half_height = half_height;
    out.radius = radius;
    return out;
}
PhysicsPlane parse_physics_plane(const char *name) {
    const std::string value = name ? name : "";
    if (value == "3d") return PhysicsPlane::Full3D;
    if (value == "xy") return PhysicsPlane::XY;
    if (value == "xz") return PhysicsPlane::XZ;
    if (value == "yz") return PhysicsPlane::YZ;
    throw std::runtime_error("Unknown physics plane: " + value);
}
const char *physics_plane_name(PhysicsPlane plane) noexcept {
    switch (plane) {
    case PhysicsPlane::XY: return "xy";
    case PhysicsPlane::XZ: return "xz";
    case PhysicsPlane::YZ: return "yz";
    default: return "3d";
    }
}
} // namespace ember
