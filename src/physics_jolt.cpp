#include "ember/physics.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ember {
namespace {
bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool finite(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
JPH::Vec3 jvec(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::RVec3 jrvec(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::Quat jquat(Quat q) {
    const float length_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(length_sq) || length_sq < 1e-8f)
        throw std::runtime_error("Rigid body rotation cannot be a zero quaternion");
    const float inverse = 1.f / std::sqrt(length_sq);
    return {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
}
template <class V> Vec3 evec(const V &v) { return {float(v.GetX()), float(v.GetY()), float(v.GetZ())}; }
Quat equat(JPH::QuatArg q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

struct JoltRuntime {
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        if (JPH::Factory::sInstance)
            throw std::runtime_error("Jolt Factory already owned by another integration");
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRuntime() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
JoltRuntime &jolt_runtime() {
    static JoltRuntime runtime;
    return runtime;
}

namespace Layers {
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
} // namespace Layers
namespace BroadLayers {
const JPH::BroadPhaseLayer Static(0);
const JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
} // namespace BroadLayers

class BroadPhase final : public JPH::BroadPhaseLayerInterface {
    JPH::BroadPhaseLayer map_[Layers::Count]{BroadLayers::Static, BroadLayers::Moving};
  public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadLayers::Count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer < Layers::Count ? map_[layer] : BroadLayers::Static;
    }
};
class ObjectVsBroad final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        return object == Layers::Moving || broad == BroadLayers::Moving;
    }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

JPH::EMotionType motion(BodyMotion value) {
    switch (value) {
    case BodyMotion::Dynamic: return JPH::EMotionType::Dynamic;
    case BodyMotion::Kinematic: return JPH::EMotionType::Kinematic;
    default: return JPH::EMotionType::Static;
    }
}
JPH::EAllowedDOFs allowed_dofs(PhysicsPlane plane, bool rotation) {
    using D = JPH::EAllowedDOFs;
    switch (plane) {
    case PhysicsPlane::XY:
        return D::TranslationX | D::TranslationY | (rotation ? D::RotationZ : D::None);
    case PhysicsPlane::XZ:
        return D::TranslationX | D::TranslationZ | (rotation ? D::RotationY : D::None);
    case PhysicsPlane::YZ:
        return D::TranslationY | D::TranslationZ | (rotation ? D::RotationX : D::None);
    default:
        return rotation ? D::All : D::TranslationX | D::TranslationY | D::TranslationZ;
    }
}
void validate_settings(const PhysicsWorldSettings &s) {
    if (!finite(s.gravity) || s.solver_quality < 1 || s.solver_quality > 100 || s.worker_threads < -1 ||
        s.worker_threads > 64 || s.max_bodies == 0 || s.max_body_pairs == 0 || s.max_contacts == 0 ||
        s.temporary_bytes < 64 * 1024 || s.temporary_bytes > size_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("Invalid physics world settings");
}
void validate_desc(const BodyDesc &d) {
    if (!finite(d.transform.position) || !finite(d.transform.rotation) || !std::isfinite(d.mass) ||
        !std::isfinite(d.friction) || !std::isfinite(d.restitution) || !std::isfinite(d.linear_damping) ||
        !std::isfinite(d.angular_damping) || !std::isfinite(d.gravity_factor) || d.mass <= 0 ||
        d.friction < 0 || d.restitution < 0 || d.restitution > 1 || d.linear_damping < 0 ||
        d.linear_damping > 1 || d.angular_damping < 0 || d.angular_damping > 1)
        throw std::runtime_error("Invalid rigid body description");
    const float q2 = d.transform.rotation.x * d.transform.rotation.x + d.transform.rotation.y * d.transform.rotation.y +
                     d.transform.rotation.z * d.transform.rotation.z + d.transform.rotation.w * d.transform.rotation.w;
    if (q2 < 1e-8f)
        throw std::runtime_error("Rigid body rotation cannot be a zero quaternion");
    switch (d.shape.type) {
    case BodyShapeType::Box: BodyShape::box(d.shape.half_extents); break;
    case BodyShapeType::Sphere: BodyShape::sphere(d.shape.radius); break;
    case BodyShapeType::Capsule: BodyShape::capsule(d.shape.half_height, d.shape.radius); break;
    }
}
} // namespace

struct PhysicsWorld::Impl {
    struct Slot {
        JPH::BodyID id{};
        uint64_t generation = 0;
        BodyMotion motion = BodyMotion::Static;
    };
    JoltRuntime *runtime = nullptr;
    PhysicsWorldSettings settings;
    BroadPhase broad_phase;
    ObjectVsBroad object_vs_broad;
    ObjectPairs object_pairs;
    JPH::TempAllocatorImpl temporary;
    JPH::JobSystemThreadPool jobs;
    JPH::PhysicsSystem system;
    std::vector<Slot> slots;
    uint64_t next_generation = 1;

    static int threads(int requested) {
        if (requested >= 0) return requested;
        const auto hardware = std::thread::hardware_concurrency();
        return hardware > 1 ? int(hardware - 1) : 0;
    }
    explicit Impl(const PhysicsWorldSettings &value)
        : runtime(&jolt_runtime()), settings(value), temporary(int(value.temporary_bytes)),
          jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads(value.worker_threads)) {
        system.Init(value.max_bodies, 0, value.max_body_pairs, value.max_contacts, broad_phase,
                    object_vs_broad, object_pairs);
        system.SetGravity(jvec(value.gravity));
        auto solver = system.GetPhysicsSettings();
        solver.mNumVelocitySteps = JPH::uint(2 + (value.solver_quality - 1) * 8 / 99);
        solver.mNumPositionSteps = JPH::uint(1 + (value.solver_quality - 1) * 2 / 99);
        system.SetPhysicsSettings(solver);
    }
    ~Impl() {
        auto &bodies = system.GetBodyInterface();
        for (auto &slot : slots)
            if (slot.generation) {
                bodies.RemoveBody(slot.id);
                bodies.DestroyBody(slot.id);
            }
    }
    Slot &slot(BodyRef ref) {
        if (!ref.generation || ref.index >= slots.size() || slots[ref.index].generation != ref.generation)
            throw std::runtime_error("Invalid or expired rigid body reference");
        return slots[ref.index];
    }
    const Slot &slot(BodyRef ref) const {
        if (!ref.generation || ref.index >= slots.size() || slots[ref.index].generation != ref.generation)
            throw std::runtime_error("Invalid or expired rigid body reference");
        return slots[ref.index];
    }
};

bool jolt_physics_available() noexcept { return true; }
PhysicsWorld::PhysicsWorld(const PhysicsWorldSettings &settings) {
    validate_settings(settings);
    impl_ = std::make_unique<Impl>(settings);
}
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld &&) noexcept = default;
PhysicsWorld &PhysicsWorld::operator=(PhysicsWorld &&) noexcept = default;

BodyRef PhysicsWorld::create_body(const BodyDesc &desc) {
    validate_desc(desc);
    JPH::RefConst<JPH::Shape> shape;
    switch (desc.shape.type) {
    case BodyShapeType::Box: shape = new JPH::BoxShape(jvec(desc.shape.half_extents), 0.f); break;
    case BodyShapeType::Sphere: shape = new JPH::SphereShape(desc.shape.radius); break;
    case BodyShapeType::Capsule: shape = new JPH::CapsuleShape(desc.shape.half_height, desc.shape.radius); break;
    }
    const auto layer = desc.motion == BodyMotion::Static ? Layers::Static : Layers::Moving;
    JPH::BodyCreationSettings settings(shape, jrvec(desc.transform.position), jquat(desc.transform.rotation),
                                       motion(desc.motion), layer);
    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mLinearDamping = desc.linear_damping;
    settings.mAngularDamping = desc.angular_damping;
    settings.mGravityFactor = desc.gravity_factor;
    settings.mIsSensor = desc.sensor;
    settings.mAllowedDOFs = allowed_dofs(impl_->settings.plane, desc.allow_rotation);
    settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    if (desc.motion == BodyMotion::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }
    const auto activation = desc.motion == BodyMotion::Dynamic ? JPH::EActivation::Activate
                                                               : JPH::EActivation::DontActivate;
    auto id = impl_->system.GetBodyInterface().CreateAndAddBody(settings, activation);
    if (id.IsInvalid())
        throw std::runtime_error("Jolt rigid body capacity exhausted");
    if (impl_->next_generation == std::numeric_limits<uint64_t>::max()) {
        auto &bodies = impl_->system.GetBodyInterface();
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
        throw std::runtime_error("Rigid body generation space exhausted");
    }
    const auto generation = impl_->next_generation++;
    for (size_t i = 0; i < impl_->slots.size(); ++i)
        if (!impl_->slots[i].generation) {
            impl_->slots[i] = {id, generation, desc.motion};
            return {i, generation};
        }
    try {
        impl_->slots.push_back({id, generation, desc.motion});
    } catch (...) {
        auto &bodies = impl_->system.GetBodyInterface();
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
        throw;
    }
    return {impl_->slots.size() - 1, generation};
}
void PhysicsWorld::destroy_body(BodyRef ref) {
    auto &slot = impl_->slot(ref);
    auto &bodies = impl_->system.GetBodyInterface();
    bodies.RemoveBody(slot.id);
    bodies.DestroyBody(slot.id);
    slot = {};
}
bool PhysicsWorld::alive(BodyRef ref) const noexcept {
    return impl_ && ref.generation && ref.index < impl_->slots.size() &&
           impl_->slots[ref.index].generation == ref.generation;
}
BodyState PhysicsWorld::state(BodyRef ref) const {
    const auto &slot = impl_->slot(ref);
    const auto &bodies = impl_->system.GetBodyInterface();
    JPH::RVec3 position;
    JPH::Quat rotation;
    bodies.GetPositionAndRotation(slot.id, position, rotation);
    Vec3 linear{}, angular{};
    if (slot.motion != BodyMotion::Static) {
        linear = evec(bodies.GetLinearVelocity(slot.id));
        angular = evec(bodies.GetAngularVelocity(slot.id));
    }
    return {{evec(position), equat(rotation)}, linear, angular,
            slot.motion == BodyMotion::Static || !bodies.IsActive(slot.id)};
}
void PhysicsWorld::set_transform(BodyRef ref, PhysicsTransform transform, bool activate) {
    if (!finite(transform.position) || !finite(transform.rotation))
        throw std::runtime_error("Non-finite rigid body transform");
    const auto &slot = impl_->slot(ref);
    impl_->system.GetBodyInterface().SetPositionAndRotation(
        slot.id, jrvec(transform.position), jquat(transform.rotation),
        activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}
void PhysicsWorld::move_kinematic(BodyRef ref, PhysicsTransform target, float delta) {
    if (!std::isfinite(delta) || delta <= 0 || !finite(target.position) || !finite(target.rotation))
        throw std::runtime_error("Invalid kinematic movement");
    const auto &slot = impl_->slot(ref);
    if (slot.motion != BodyMotion::Kinematic)
        throw std::runtime_error("move_kinematic requires a kinematic body");
    impl_->system.GetBodyInterface().MoveKinematic(slot.id, jrvec(target.position), jquat(target.rotation), delta);
}
void PhysicsWorld::set_linear_velocity(BodyRef ref, Vec3 velocity) {
    if (!finite(velocity)) throw std::runtime_error("Non-finite rigid body velocity");
    const auto &slot = impl_->slot(ref);
    if (slot.motion == BodyMotion::Static)
        throw std::runtime_error("Static bodies have no velocity");
    impl_->system.GetBodyInterface().SetLinearVelocity(slot.id, jvec(velocity));
}
Vec3 PhysicsWorld::linear_velocity(BodyRef ref) const {
    const auto &slot = impl_->slot(ref);
    if (slot.motion == BodyMotion::Static)
        return {};
    return evec(impl_->system.GetBodyInterface().GetLinearVelocity(slot.id));
}
void PhysicsWorld::add_force(BodyRef ref, Vec3 force) {
    if (!finite(force)) throw std::runtime_error("Non-finite rigid body force");
    const auto &slot = impl_->slot(ref);
    if (slot.motion != BodyMotion::Dynamic)
        throw std::runtime_error("Forces require a dynamic body");
    impl_->system.GetBodyInterface().AddForce(slot.id, jvec(force));
}
void PhysicsWorld::add_impulse(BodyRef ref, Vec3 impulse) {
    if (!finite(impulse)) throw std::runtime_error("Non-finite rigid body impulse");
    const auto &slot = impl_->slot(ref);
    if (slot.motion != BodyMotion::Dynamic)
        throw std::runtime_error("Impulses require a dynamic body");
    impl_->system.GetBodyInterface().AddImpulse(slot.id, jvec(impulse));
}
void PhysicsWorld::step(float delta) {
    if (!std::isfinite(delta) || delta <= 0 || delta > .25f)
        throw std::runtime_error("Invalid rigid body timestep");
    const auto error = impl_->system.Update(delta, 1, &impl_->temporary, &impl_->jobs);
    if (error != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt physics update exhausted a configured capacity");
}
void PhysicsWorld::optimize_static_broadphase() { impl_->system.OptimizeBroadPhase(); }
} // namespace ember
