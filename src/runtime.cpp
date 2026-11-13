#include "ember/runtime.hpp"
#include "detail/listeners.hpp"
#include "ember/asset.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
namespace ember {
namespace {
Quat yaw_quaternion(float yaw) {
    return {0, std::sin(yaw * .5f), 0, std::cos(yaw * .5f)};
}
float quaternion_yaw(Quat q) {
    return std::atan2(2.f * (q.w * q.y + q.x * q.z), 1.f - 2.f * (q.y * q.y + q.z * q.z));
}
} // namespace
struct Runtime::EventListeners : detail::Listeners<const Event &> { uint64_t next = 1; };
Runtime::Runtime(Config c) : events_(std::make_shared<EventListeners>()), config(std::move(c)) {
    config.validate();
    auto settings = config.physics_settings();
    if (config.physics_backend == "jolt")
        physics_ = std::make_unique<PhysicsWorld>(settings);
}
Runtime::~Runtime() = default;
ObjectRef Runtime::add_object(Object object) {
    object.active = true;
    while (!free_objects_.empty()) {
        const auto slot = free_objects_.back();
        free_objects_.pop_back();
        if (slot.index < scene.objects.size() && !scene.objects[slot.index].active &&
            scene.objects[slot.index].id == slot.id) {
            scene.objects[slot.index] = std::move(object);
            return object_ref(slot.index);
        }
    }
    scene.objects.push_back(std::move(object));
    return object_ref(scene.objects.size() - 1);
}
size_t Runtime::add_model(const std::string &path, Vec3 position) {
    auto a = asset_cache[path].lock();
    if (!a) {
        a = import_asset(path);
        asset_cache[path] = a;
    }
    return add_object(instantiate(a, position)).index;
}
ObjectRef Runtime::object_ref(size_t index) const {
    if (index >= scene.objects.size())
        throw std::runtime_error("Invalid scene object index");
    return {index, scene.objects[index].id};
}
bool Runtime::object_alive(ObjectRef ref) const {
    return ref.id != 0 && ref.index < scene.objects.size() && scene.objects[ref.index].active &&
           scene.objects[ref.index].id == ref.id;
}
Object &Runtime::object(ObjectRef ref) {
    if (!object_alive(ref))
        throw std::runtime_error("Invalid or expired object reference");
    return scene.objects[ref.index];
}
const Object &Runtime::object(ObjectRef ref) const {
    if (!object_alive(ref))
        throw std::runtime_error("Invalid or expired object reference");
    return scene.objects[ref.index];
}
void Runtime::destroy_object(ObjectRef ref) {
    auto &o = object(ref);
    // Allocate before invalidation: an allocation failure leaves the object alive.
    free_objects_.push_back(ref);
    o.mesh = {};
    o.animator.reset();
    o.parent_transform = {};
    o.position = {};
    o.yaw = 0;
    o.active = false;
    prune_stale_references();
}
void Runtime::prune_stale_references() {
    attachments_.erase(std::remove_if(attachments_.begin(), attachments_.end(), [&](const Attachment &a) {
                           return !object_alive(a.child) || !object_alive(a.parent);
                       }),
                       attachments_.end());
    // Keep collider slots stable for existing handles, but release heavy BVHs
    // as soon as their owning object reference expires.
    for (auto &collider : colliders_)
        if (collider.id != 0 && !object_alive(collider.object)) {
            collider.mesh = {};
            collider.id = 0;
        }
    if (physics_)
        for (size_t i = 0; i < physics_bindings_.size();) {
            if (!object_alive(physics_bindings_[i].object)) {
                if (physics_->alive(physics_bindings_[i].body))
                    physics_->destroy_body(physics_bindings_[i].body);
                physics_bindings_.erase(physics_bindings_.begin() + i);
            } else {
                ++i;
            }
        }
}
void Runtime::attach(size_t child, size_t parent, const std::string &bone) {
    if (child >= scene.objects.size() || parent >= scene.objects.size())
        throw std::runtime_error("Invalid attachment objects");
    attach(object_ref(child), object_ref(parent), bone);
}
void Runtime::attach(ObjectRef child, ObjectRef parent, const std::string &bone) {
    prune_stale_references();
    if (!object_alive(child) || !object_alive(parent) || child == parent)
        throw std::runtime_error("Invalid attachment objects");
    for (const auto &c : colliders_)
        if (c.id != 0 && c.object == child)
            throw std::runtime_error("Bone attachments cannot carry static mesh colliders");
    if (has_rigid_body(child))
        throw std::runtime_error("Bone attachments cannot carry rigid bodies");
    if (!object(parent).animator)
        throw std::runtime_error("Attachment parent has no skeleton");
    for (const auto &a : attachments_)
        if (a.child == parent || a.parent == child)
            throw std::runtime_error("Nested bone attachments are not supported");
    object(parent).animator->node_transform(bone);
    for (auto &a : attachments_)
        if (a.child == child) {
            a = {child, parent, bone};
            update_attachments();
            return;
        }
    attachments_.push_back({child, parent, bone});
    update_attachments();
}
void Runtime::update_attachments() {
    prune_stale_references();
    for (const auto &a : attachments_) {
        auto &p = object(a.parent);
        Transform t;
        t.translation = p.position;
        t.rotation = {0, std::sin(p.yaw * .5f), 0, std::cos(p.yaw * .5f)};
        object(a.child).parent_transform = p.parent_transform * matrix(t) * p.animator->node_transform(a.bone);
    }
}
ColliderRef Runtime::add_collider(ObjectRef owner) {
    prune_stale_references();
    auto &o = object(owner);
    for (const auto &a : attachments_)
        if (a.child == owner)
            throw std::runtime_error("Attached mesh colliders are not implemented");
    if (next_collider_id_ == std::numeric_limits<uint64_t>::max())
        throw std::runtime_error("Collider ID space exhausted");
    CollisionMesh mesh;
    mesh.cook(o.mesh, config.collision_quality);
    const auto id = next_collider_id_++;
    for (size_t i = 0; i < colliders_.size(); ++i)
        if (colliders_[i].id == 0) {
            colliders_[i] = {owner, std::move(mesh), id};
            return {i, id};
        }
    colliders_.push_back({owner, std::move(mesh), id});
    return {colliders_.size() - 1, id};
}
void Runtime::remove_collider(ColliderRef ref) {
    if (ref.index >= colliders_.size() || colliders_[ref.index].id != ref.id)
        return;
    if (ref.index + 1 == colliders_.size())
        colliders_.pop_back();
    else {
        colliders_[ref.index].mesh = {};
        colliders_[ref.index].id = 0;
    }
}
bool Runtime::raycast(ColliderRef ref, Vec3 from, Vec3 direction, float distance, Hit &hit) const {
    if (ref.index >= colliders_.size() || ref.id == 0 || colliders_[ref.index].id != ref.id)
        throw std::runtime_error("Invalid or expired collider reference");
    const auto &collider = colliders_[ref.index];
    const auto &owner = object(collider.object);
    if (!collider.mesh.raycast(rotate_y(from - owner.position, -owner.yaw), rotate_y(direction, -owner.yaw),
                               distance, hit))
        return false;
    hit.position = rotate_y(hit.position, owner.yaw) + owner.position;
    hit.normal = rotate_y(hit.normal, owner.yaw);
    return true;
}
Runtime::PhysicsBinding &Runtime::physics_binding(ObjectRef owner) {
    if (!physics_)
        throw std::runtime_error("Advanced physics is disabled for this Runtime");
    object(owner);
    for (auto &binding : physics_bindings_)
        if (binding.object == owner)
            return binding;
    throw std::runtime_error("Object has no rigid body");
}
const Runtime::PhysicsBinding &Runtime::physics_binding(ObjectRef owner) const {
    if (!physics_)
        throw std::runtime_error("Advanced physics is disabled for this Runtime");
    object(owner);
    for (const auto &binding : physics_bindings_)
        if (binding.object == owner)
            return binding;
    throw std::runtime_error("Object has no rigid body");
}
bool Runtime::has_rigid_body(ObjectRef owner) const noexcept {
    if (!physics_ || !object_alive(owner))
        return false;
    for (const auto &binding : physics_bindings_)
        if (binding.object == owner && physics_->alive(binding.body))
            return true;
    return false;
}
BodyRef Runtime::add_rigid_body(ObjectRef owner, BodyDesc desc) {
    auto &o = object(owner);
    if (!physics_)
        throw std::runtime_error("Advanced physics is disabled; select physics.backend=\"jolt\"");
    if (has_rigid_body(owner))
        throw std::runtime_error("Object already has a rigid body");
    for (const auto &attachment : attachments_)
        if (attachment.child == owner)
            throw std::runtime_error("Attached objects cannot carry rigid bodies");
    desc.transform = {o.position, yaw_quaternion(o.yaw)};
    if (config.continuous_collision == "off")
        desc.continuous = false;
    auto body = physics_->create_body(desc);
    try {
        physics_bindings_.push_back({owner, body, desc.motion});
    } catch (...) {
        physics_->destroy_body(body);
        throw;
    }
    return body;
}
void Runtime::remove_rigid_body(ObjectRef owner) {
    object(owner);
    auto &binding = physics_binding(owner);
    if (physics_->alive(binding.body))
        physics_->destroy_body(binding.body);
    physics_bindings_.erase(std::remove_if(physics_bindings_.begin(), physics_bindings_.end(),
                                           [&](const PhysicsBinding &b) { return b.object == owner; }),
                            physics_bindings_.end());
}
BodyState Runtime::rigid_body_state(ObjectRef owner) const {
    const auto &binding = physics_binding(owner);
    return physics_->state(binding.body);
}
void Runtime::set_object_transform(ObjectRef owner, Vec3 position, float yaw) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(yaw))
        throw std::runtime_error("Non-finite object transform");
    auto &o = object(owner);
    o.position = position;
    o.yaw = yaw;
    if (has_rigid_body(owner)) {
        auto &binding = physics_binding(owner);
        // Kinematic velocity is derived by MoveKinematic on the next fixed step.
        // Teleporting it first would erase that velocity and stop it pushing bodies.
        if (binding.motion != BodyMotion::Kinematic)
            physics_->set_transform(binding.body, {position, yaw_quaternion(yaw)});
    }
}
void Runtime::set_rigid_body_velocity(ObjectRef owner, Vec3 value) {
    const auto &binding = physics_binding(owner);
    physics_->set_linear_velocity(binding.body, value);
}
Vec3 Runtime::rigid_body_velocity(ObjectRef owner) const {
    const auto &binding = physics_binding(owner);
    return physics_->linear_velocity(binding.body);
}
void Runtime::add_rigid_body_force(ObjectRef owner, Vec3 value) {
    const auto &binding = physics_binding(owner);
    physics_->add_force(binding.body, value);
}
void Runtime::add_rigid_body_impulse(ObjectRef owner, Vec3 value) {
    const auto &binding = physics_binding(owner);
    physics_->add_impulse(binding.body, value);
}
void Runtime::optimize_physics_broadphase() {
    if (!physics_)
        throw std::runtime_error("Advanced physics is disabled for this Runtime");
    physics_->optimize_static_broadphase();
}
void Runtime::step_physics(float delta) {
    if (!physics_)
        return;
    prune_stale_references();
    for (const auto &binding : physics_bindings_)
        if (binding.motion == BodyMotion::Kinematic) {
            const auto &o = object(binding.object);
            physics_->move_kinematic(binding.body, {o.position, yaw_quaternion(o.yaw)}, delta);
        }
    physics_->step(delta);
    for (const auto &binding : physics_bindings_)
        if (binding.motion == BodyMotion::Dynamic) {
            auto state = physics_->state(binding.body);
            auto &o = object(binding.object);
            o.position = state.transform.position;
            o.yaw = quaternion_yaw(state.transform.rotation);
        }
}
void Runtime::clear_scene() {
    if (physics_)
        for (const auto &binding : physics_bindings_)
            if (physics_->alive(binding.body))
                physics_->destroy_body(binding.body);
    scene = {};
    free_objects_.clear();
    colliders_.clear();
    attachments_.clear();
    physics_bindings_.clear();
    asset_cache.clear();
}
Runtime::EventConnection Runtime::observe(std::function<void(Runtime &, const Event &)> fn) {
    if (!fn) throw std::invalid_argument("Empty runtime observer");
    if (events_->next == UINT64_MAX) throw std::runtime_error("Observer ID space exhausted");
    auto id = events_->next++;
    events_->entries.emplace(id, std::make_shared<const EventListeners::Callback>([this, fn = std::move(fn)](const Event &e) { fn(*this, e); }));
    return EventConnection([weak = std::weak_ptr<EventListeners>(events_), id] {
        if (auto list = weak.lock()) list->entries.erase(id);
    });
}
void Runtime::dispatch(Event e) {
    std::exception_ptr error;
    if (on_event) {
        // Compatibility callback preserves mutable state. Replace it outside
        // its active call; use observe() for self-disconnecting subscriptions.
        try { on_event(*this, e); } catch (...) { error = std::current_exception(); }
    }
    try { events_->send(e); } catch (...) { if (!error) error = std::current_exception(); }
    if (error) std::rethrow_exception(error);
}
void Runtime::start() {
    dispatch({Event::Type::Start, 0, 0});
}
void Runtime::add_cue(double t, int code) {
    if (!std::isfinite(t) || t < time_)
        throw std::runtime_error("Cue must be finite and in the future");
    if (cue_index_) {
        cues_.erase(cues_.begin(), cues_.begin() + cue_index_);
        cue_index_ = 0;
    }
    auto where = std::upper_bound(cues_.begin(), cues_.end(), t,
        [](double time, const Cue &cue) { return time < cue.time; });
    cues_.insert(where, {t, code});
}
void Runtime::tick(double elapsed) {
    if (ticking_)
        throw std::runtime_error("Recursive Runtime::tick is not supported");
    struct TickGuard {
        bool &flag;
        explicit TickGuard(bool &value) : flag(value) { flag = true; }
        ~TickGuard() { flag = false; }
    } guard(ticking_);
    if (!std::isfinite(elapsed) || elapsed < 0)
        throw std::runtime_error("Invalid timestep");
    const double dt = config.fixed_delta();
    // Platform clocks cannot provide useful sub-microsecond game-frame deltas.
    // Ignoring them is deterministic and avoids ULP-dependent accumulation.
    if (elapsed > 0 && elapsed < 1e-6)
        return;
    accumulator_ += std::min(elapsed, .25);
    int steps = 0;
    while (accumulator_ + 1e-12 >= dt && steps < 8) {
        accumulator_ = std::max(0.0, accumulator_ - dt);
        time_ += dt;
        std::exception_ptr callback_error;
        auto notify = [&](Event event) {
            try {
                dispatch(event);
            } catch (...) {
                if (!callback_error)
                    callback_error = std::current_exception();
            }
        };
        notify({Event::Type::FixedUpdate, time_, 0});
        while (cue_index_ < cues_.size() && cues_[cue_index_].time <= time_) {
            int code = cues_[cue_index_++].code;
            notify({Event::Type::Cue, time_, code});
        }
        if (cue_index_ == cues_.size()) { cues_.clear(); cue_index_ = 0; }
        step_physics(float(dt));
        for (auto &o : scene.objects)
            if (o.active)
                update_animation(o, float(dt));
        update_attachments();
        ++steps;
        // The fixed step is internally complete even when user code fails.
        if (callback_error)
            std::rethrow_exception(callback_error);
    }
    if (steps == 8 && accumulator_ >= dt)
        accumulator_ = std::fmod(accumulator_, dt);
}
} // namespace ember
