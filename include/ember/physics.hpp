#pragma once
#include "math.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ember {

// Advanced rigid-body physics is an optional service. These public types never
// expose Jolt headers, IDs or layouts, so games can keep the same source API
// when the backend is replaced or omitted.
enum class PhysicsPlane { Full3D, XY, XZ, YZ };
enum class BodyMotion { Static, Kinematic, Dynamic };
enum class BodyShapeType { Box, Sphere, Capsule };

struct PhysicsTransform {
    Vec3 position{};
    Quat rotation{};
};

struct BodyShape {
    BodyShapeType type = BodyShapeType::Box;
    Vec3 half_extents{.5f, .5f, .5f};
    float radius = .5f;
    float half_height = .5f;

    static BodyShape box(Vec3 half_extents);
    static BodyShape sphere(float radius);
    // half_height is the cylindrical half-height, excluding the two caps.
    static BodyShape capsule(float half_height, float radius);
};

struct BodyDesc {
    BodyShape shape{};
    BodyMotion motion = BodyMotion::Static;
    PhysicsTransform transform{};
    float mass = 1.f;
    float friction = .5f;
    float restitution = 0.f;
    float linear_damping = .05f;
    float angular_damping = .05f;
    float gravity_factor = 1.f;
    bool continuous = false;
    bool allow_rotation = true;
    bool sensor = false;
};

struct BodyRef {
    size_t index = 0;
    uint64_t generation = 0;
    bool operator==(BodyRef other) const {
        return index == other.index && generation == other.generation;
    }
};

struct BodyState {
    PhysicsTransform transform{};
    Vec3 linear_velocity{}, angular_velocity{};
    bool sleeping = false;
};

struct PhysicsWorldSettings {
    PhysicsPlane plane = PhysicsPlane::Full3D;
    Vec3 gravity{0, -9.81f, 0};
    int solver_quality = 40; // 1..100, mapped to bounded solver iterations.
    int worker_threads = -1; // -1 = hardware minus one, 0 = calling thread.
    uint32_t max_bodies = 4096;
    uint32_t max_body_pairs = 4096;
    uint32_t max_contacts = 2048;
    size_t temporary_bytes = 4 * 1024 * 1024;
};

bool jolt_physics_available() noexcept;
PhysicsPlane parse_physics_plane(const char *name);
const char *physics_plane_name(PhysicsPlane plane) noexcept;

class PhysicsWorld {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit PhysicsWorld(const PhysicsWorldSettings &settings = {});
    ~PhysicsWorld();
    PhysicsWorld(PhysicsWorld &&) noexcept;
    PhysicsWorld &operator=(PhysicsWorld &&) noexcept;
    PhysicsWorld(const PhysicsWorld &) = delete;
    PhysicsWorld &operator=(const PhysicsWorld &) = delete;

    BodyRef create_body(const BodyDesc &);
    void destroy_body(BodyRef);
    bool alive(BodyRef) const noexcept;
    BodyState state(BodyRef) const;
    void set_transform(BodyRef, PhysicsTransform, bool activate = true);
    void move_kinematic(BodyRef, PhysicsTransform target, float delta);
    void set_linear_velocity(BodyRef, Vec3);
    Vec3 linear_velocity(BodyRef) const;
    void add_force(BodyRef, Vec3);
    void add_impulse(BodyRef, Vec3);
    void step(float delta);
    void optimize_static_broadphase();
};

} // namespace ember
