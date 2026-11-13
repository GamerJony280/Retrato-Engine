#include "ember/physics.hpp"
#include <stdexcept>

namespace ember {
struct PhysicsWorld::Impl {};
bool jolt_physics_available() noexcept { return false; }
PhysicsWorld::PhysicsWorld(const PhysicsWorldSettings &) {
    throw std::runtime_error("Jolt physics was not compiled; configure Ember with EMBER_JOLT=ON");
}
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld &&) noexcept = default;
PhysicsWorld &PhysicsWorld::operator=(PhysicsWorld &&) noexcept = default;
BodyRef PhysicsWorld::create_body(const BodyDesc &) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::destroy_body(BodyRef) { throw std::runtime_error("Jolt physics unavailable"); }
bool PhysicsWorld::alive(BodyRef) const noexcept { return false; }
BodyState PhysicsWorld::state(BodyRef) const { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::set_transform(BodyRef, PhysicsTransform, bool) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::move_kinematic(BodyRef, PhysicsTransform, float) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::set_linear_velocity(BodyRef, Vec3) { throw std::runtime_error("Jolt physics unavailable"); }
Vec3 PhysicsWorld::linear_velocity(BodyRef) const { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::add_force(BodyRef, Vec3) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::add_impulse(BodyRef, Vec3) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::step(float) { throw std::runtime_error("Jolt physics unavailable"); }
void PhysicsWorld::optimize_static_broadphase() { throw std::runtime_error("Jolt physics unavailable"); }
} // namespace ember
