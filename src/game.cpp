#include "ember/game.hpp"
#include "ember/runtime.hpp"
#include "detail/listeners.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <deque>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace ember::game {
namespace {
bool finite_vec(Vec3);
uint64_t next_id() {
    static std::atomic<uint64_t> next{1};
    // Saturate rather than reusing IDs after overflow.
    auto n = next.load(std::memory_order_relaxed);
    do {
        if (n == UINT64_MAX)
            throw std::runtime_error("Handle space exhausted");
    } while (!next.compare_exchange_weak(n, n + 1, std::memory_order_relaxed));
    return n;
}
template<class T> using Listeners = detail::Listeners<T>;
} // namespace
struct Host::Impl {
    uint64_t session = next_id();
    std::shared_ptr<Runtime> owner;
    Runtime &runtime;
    Runtime::EventConnection runtime_events;
    struct Box { Entity entity; Vec3 half; uint32_t mask; };
    std::vector<Box> boxes;
    mutable std::vector<Aabb> query_boxes;
    std::unordered_map<uint64_t, ObjectRef> objects;
    std::unordered_map<std::string, uint64_t> names, paths;
    std::unordered_map<uint64_t, std::string> object_names;
    std::unordered_map<uint64_t, std::shared_ptr<const Asset>> assets;
    std::shared_ptr<Listeners<float>> updates = std::make_shared<Listeners<float>>();
    std::shared_ptr<Listeners<int64_t>> signals = std::make_shared<Listeners<int64_t>>();
    std::shared_ptr<Listeners<const InputEvent &>> inputs = std::make_shared<Listeners<const InputEvent &>>();
    std::unordered_set<int> keys, pressed_keys, released_keys, mouse_buttons;
    std::set<std::pair<int, int>> gamepad_buttons;
    std::map<std::pair<int, int>, float> gamepad_axes;
    Vec2 mouse_position{}, mouse_delta{};
    std::deque<int64_t> pending;
    bool ticking = false;
    struct OwnedScript { std::unique_ptr<Script> script;Runtime::EventConnection events; };
    std::vector<std::unique_ptr<OwnedScript>> scripts;
    explicit Impl(std::shared_ptr<Runtime> rt) : owner(std::move(rt)), runtime(*owner) {
        runtime.config.fixed_delta();
    }
    void receive(const Event &e) {
            if (e.type != Event::Type::FixedUpdate)
                return;
            std::exception_ptr error;
            try {
                updates->send(float(runtime.config.fixed_delta()));
            } catch (...) {
                error = std::current_exception();
            }
            // Events emitted by receivers wait for the next fixed tick.
            size_t count = pending.size();
            while (count--) {
                auto code = pending.front();
                pending.pop_front();
                try {
                    signals->send(code);
                } catch (...) {
                    if (!error)
                        error = std::current_exception();
                }
            }
            mouse_delta = {};
            pressed_keys.clear(); released_keys.clear();
            if (error)
                std::rethrow_exception(error);
    }
    Entity add(Object object, const std::string &name) {
        auto old=names.find(name);
        if(old!=names.end()) {
            auto item=objects.find(old->second);
            if(item==objects.end() || !runtime.object_alive(item->second)) {
                auto id=old->second;
                boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[&](const Box &b){return b.entity.id_==id;}),boxes.end());
                objects.erase(id); object_names.erase(id); names.erase(old);
            }
        }
        if (name.empty() || names.count(name))
            throw std::runtime_error("Empty or duplicate entity name: " + name);
        auto id = next_id();
        names.emplace(name, id);
        try {
            object_names.emplace(id, name);
        } catch (...) {
            names.erase(name);
            throw;
        }
        ObjectRef ref;
        try {
            ref = runtime.add_object(std::move(object));
            objects.emplace(id, ref);
        } catch (...) {
            if (ref.id)
                runtime.destroy_object(ref);
            object_names.erase(id);
            names.erase(name);
            throw;
        }
        return Entity(id);
    }
};
uint64_t Host::session_id() const { return impl_->session; }
Host::Host(const Config &c) : Host(std::make_shared<Runtime>(c)) {}
Host::Host(std::shared_ptr<Runtime> rt) {
    if (!rt) throw std::invalid_argument("Host requires a Runtime");
    impl_ = std::make_shared<Impl>(std::move(rt));
    auto weak = std::weak_ptr<Impl>(impl_);
    impl_->runtime_events = impl_->runtime.observe([weak](Runtime &, const Event &e) {
        if (auto session = weak.lock()) session->receive(e);
    });
}
Entity Host::create(const std::string &name) {
    return create_box(name, {1, 1, 1}, {.8f, .4f, .2f});
}
Entity Host::create_box(const std::string &name, Vec3 size, Vec3 color) {
    if (!(size.x > 0 && size.y > 0 && size.z > 0) || !std::isfinite(size.x) ||
        !std::isfinite(size.y) || !std::isfinite(size.z))
        throw std::runtime_error("Box dimensions must be positive and finite");
    if (!finite_vec(color)) throw std::invalid_argument("Invalid box color");
    return impl_->add(Object(box(size, color)), name);
}
Entity Host::entity(const std::string &name) const {
    auto it = impl_->names.find(name);
    if (it == impl_->names.end())
        throw std::runtime_error("Unknown entity: " + name);
    Entity e(it->second);
    if(!alive(e)) throw std::runtime_error("Expired entity: " + name);
    return e;
}
AssetHandle Host::load_asset(const std::string &path) {
    auto it = impl_->paths.find(path);
    if (it != impl_->paths.end())
        return AssetHandle(it->second);
    auto asset = import_asset(path);
    auto id = next_id();
    impl_->assets.emplace(id, std::move(asset));
    try {
        impl_->paths.emplace(path, id);
    } catch (...) {
        impl_->assets.erase(id);
        throw;
    }
    return AssetHandle(id);
}
Entity Host::spawn(AssetHandle handle, const std::string &name) {
    auto it = impl_->assets.find(handle.id_);
    if (it == impl_->assets.end())
        throw std::runtime_error("Invalid or foreign asset handle");
    return impl_->add(instantiate(it->second), name);
}
bool Host::alive(Entity e) const {
    auto it = impl_->objects.find(e.id_);
    return it != impl_->objects.end() && impl_->runtime.object_alive(it->second);
}
void Host::destroy(Entity e) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end() || !impl_->runtime.object_alive(it->second))
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.destroy_object(it->second);
    impl_->boxes.erase(std::remove_if(impl_->boxes.begin(), impl_->boxes.end(),
        [&](const Impl::Box &b) { return b.entity == e; }), impl_->boxes.end());
    auto name = impl_->object_names.find(e.id_);
    if (name != impl_->object_names.end()) {
        impl_->names.erase(name->second);
        impl_->object_names.erase(name);
    }
    impl_->objects.erase(it);
}
void Host::prune_destroyed() {
    auto &s=*impl_;
    for(auto it=s.objects.begin();it!=s.objects.end();) {
        if(s.runtime.object_alive(it->second)) {++it;continue;}
        auto name=s.object_names.find(it->first);
        if(name!=s.object_names.end()) {s.names.erase(name->second);s.object_names.erase(name);}
        it=s.objects.erase(it);
    }
    s.boxes.erase(std::remove_if(s.boxes.begin(),s.boxes.end(),[&](const Impl::Box &b){return !alive(b.entity);}),s.boxes.end());
}
Transform Host::transform(Entity e) const {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    const auto &o = impl_->runtime.object(it->second);
    return {o.position, o.yaw};
}
void Host::set_transform(Entity e, Transform t) {
    if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) || !std::isfinite(t.position.z) ||
        !std::isfinite(t.yaw))
        throw std::runtime_error("Non-finite transform");
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.set_object_transform(it->second, t.position, t.yaw);
}
bool Host::advanced_physics_enabled() const noexcept {
    return impl_->runtime.advanced_physics_enabled();
}
void Host::add_rigid_body(Entity e, BodyDesc desc) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.add_rigid_body(it->second, std::move(desc));
}
bool Host::has_rigid_body(Entity e) const {
    auto it = impl_->objects.find(e.id_);
    return it != impl_->objects.end() && impl_->runtime.has_rigid_body(it->second);
}
void Host::remove_rigid_body(Entity e) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.remove_rigid_body(it->second);
}
BodyState Host::rigid_body_state(Entity e) const {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    return impl_->runtime.rigid_body_state(it->second);
}
void Host::set_velocity(Entity e, Vec3 value) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.set_rigid_body_velocity(it->second, value);
}
Vec3 Host::velocity(Entity e) const {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    return impl_->runtime.rigid_body_velocity(it->second);
}
void Host::add_force(Entity e, Vec3 value) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.add_rigid_body_force(it->second, value);
}
void Host::add_impulse(Entity e, Vec3 value) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end())
        throw std::runtime_error("Invalid or foreign entity handle");
    impl_->runtime.add_rigid_body_impulse(it->second, value);
}
void Host::optimize_physics_broadphase() {
    impl_->runtime.optimize_physics_broadphase();
}
Connection Host::on_update(std::function<void(float)> fn) {
    auto list = impl_->updates;
    auto id = next_id();
    if (!fn)
        throw std::runtime_error("Empty update callback");
    list->entries.emplace(id, std::make_shared<const decltype(fn)>(std::move(fn)));
    return Connection([weak = std::weak_ptr<Listeners<float>>(list), id] {
        if (auto p = weak.lock())
            p->entries.erase(id);
    });
}
Connection Host::on_signal(std::function<void(int64_t)> fn) {
    auto list = impl_->signals;
    auto id = next_id();
    if (!fn)
        throw std::runtime_error("Empty signal callback");
    list->entries.emplace(id, std::make_shared<const decltype(fn)>(std::move(fn)));
    return Connection([weak = std::weak_ptr<Listeners<int64_t>>(list), id] {
        if (auto p = weak.lock())
            p->entries.erase(id);
    });
}
Connection Host::on_input(std::function<void(const InputEvent &)> fn) {
    auto list = impl_->inputs;
    auto id = next_id();
    if (!fn)
        throw std::runtime_error("Empty input callback");
    list->entries.emplace(id, std::make_shared<const decltype(fn)>(std::move(fn)));
    return Connection([weak = std::weak_ptr<Listeners<const InputEvent &>>(list), id] {
        if (auto p = weak.lock())
            p->entries.erase(id);
    });
}
bool Host::key_down(int code) const {
    return impl_->keys.count(code) != 0;
}
Vec2 Host::mouse_position() const {
    return impl_->mouse_position;
}
Vec2 Host::mouse_delta() const {
    return impl_->mouse_delta;
}
bool Host::mouse_down(int button) const {
    return impl_->mouse_buttons.count(button) != 0;
}
bool Host::gamepad_down(int device, int button) const {
    return impl_->gamepad_buttons.count({device, button}) != 0;
}
float Host::gamepad_axis(int device, int axis) const {
    auto it = impl_->gamepad_axes.find({device, axis});
    return it == impl_->gamepad_axes.end() ? 0.f : it->second;
}
void Host::emit(int64_t code) {
    if (impl_->pending.size() >= 4096)
        throw std::runtime_error("Signal queue limit exceeded");
    impl_->pending.push_back(code);
}
void Host::load_script(const std::string &path) {
    if(impl_->ticking)throw std::runtime_error("Load scripts outside the simulation callback");
    auto owned=std::make_unique<Impl::OwnedScript>();
    owned->script=std::make_unique<Script>(impl_->runtime,path);
    owned->script->event({Event::Type::Start});
    auto *script=owned->script.get();
    owned->events=impl_->runtime.observe([script](Runtime &,const Event &event){script->event(event);});
    impl_->scripts.push_back(std::move(owned));
}
void Host::dispatch_input(InputEvent event) {
    auto state=impl_;
    if (!std::isfinite(event.time) || !std::isfinite(event.x) || !std::isfinite(event.y) ||
        !std::isfinite(event.dx) || !std::isfinite(event.dy))
        throw std::runtime_error("Non-finite input event");
    switch (event.type) {
    case InputEvent::Type::KeyDown: if (state->keys.insert(event.code).second) state->pressed_keys.insert(event.code); break;
    case InputEvent::Type::KeyUp: if (state->keys.erase(event.code)) state->released_keys.insert(event.code); break;
    case InputEvent::Type::MouseMove:
        state->mouse_position = {event.x, event.y};
        state->mouse_delta = state->mouse_delta + Vec2{event.dx, event.dy};
        break;
    case InputEvent::Type::MouseDown: state->mouse_buttons.insert(event.code); break;
    case InputEvent::Type::MouseUp: state->mouse_buttons.erase(event.code); break;
    case InputEvent::Type::GamepadButtonDown: state->gamepad_buttons.insert({event.device, event.code}); break;
    case InputEvent::Type::GamepadButtonUp: state->gamepad_buttons.erase({event.device, event.code}); break;
    case InputEvent::Type::GamepadAxis:
        if (event.x < -1.f || event.x > 1.f)
            throw std::runtime_error("Gamepad axis outside -1..1");
        state->gamepad_axes[{event.device, event.code}] = event.x;
        break;
    case InputEvent::Type::MouseWheel: break;
    }
    state->inputs->send(event);
}
void Host::tick(double elapsed) {
    auto state=impl_; // callbacks may release the caller's last Host
    if (state->ticking)
        throw std::runtime_error("Recursive Host::tick is not allowed");
    state->ticking = true;
    try {
        state->runtime.tick(elapsed);
    } catch (...) {
        state->ticking = false;
        throw;
    }
    state->ticking = false;
}
double Host::time() const {
    return impl_->runtime.time();
}
float Host::delta() const {
    return float(impl_->runtime.config.fixed_delta());
}
void Host::set_profile(Profile profile) {
    impl_->runtime.config.profile = profile;
}
Profile Host::profile() const {
    return impl_->runtime.config.profile;
}
void Host::render(SceneRenderer &renderer, Backend &backend, Frame &frame) const {
    renderer.prepare(impl_->runtime.scene, impl_->runtime.camera, impl_->runtime.config, frame);
    backend.draw(frame);
}
namespace {
bool finite_vec(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
void box_values(Vec3 half) {
    if (!finite_vec(half) || half.x <= 0 || half.y <= 0 || half.z <= 0)
        throw std::invalid_argument("Box extents must be positive and finite");
}
}
void Host::unload_asset(AssetHandle a) {
    if (!impl_->assets.erase(a.id_)) throw std::runtime_error("Invalid or foreign asset handle");
    for (auto it = impl_->paths.begin(); it != impl_->paths.end();) {
        if (it->second == a.id_) it = impl_->paths.erase(it); else ++it;
    }
}
void Host::set_box(Entity e, Vec3 size, Vec3 color) {
    box_values(size);
    if (!finite_vec(color)) throw std::invalid_argument("Invalid box color");
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end()) throw std::runtime_error("Invalid entity");
    impl_->runtime.object(it->second).mesh = box(size, color);
}
void Host::set_visible(Entity e, bool value) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end()) throw std::runtime_error("Invalid entity");
    impl_->runtime.object(it->second).visible = value;
}
void Host::set_camera(Vec3 p, Vec3 target) {
    if (!finite_vec(p) || !finite_vec(target) || !finite_vec(target-p) || length(target-p) < 1e-6f)
        throw std::invalid_argument("Invalid camera");
    impl_->runtime.camera.position = p; impl_->runtime.camera.target = target;
}
void Host::set_light(int index, Vec3 p, Vec3 color, float radius) {
    if (index < 0 || index >= 64 || !finite_vec(p) || !finite_vec(color) || !std::isfinite(radius) || radius < 0)
        throw std::invalid_argument("Invalid light parameters (index must be 0..63)");
    auto &lights = impl_->runtime.scene.lights;
    if (size_t(index) >= lights.size()) lights.resize(size_t(index)+1, Light{{},{},0});
    lights[size_t(index)] = {p,color,radius};
}
void Host::add_box_collider(Entity e, Vec3 half, uint32_t mask) {
    box_values(half); transform(e);
    for (auto &box : impl_->boxes) if (box.entity == e) { box.half = half; box.mask = mask; return; }
    impl_->boxes.push_back({e,half,mask});
    impl_->query_boxes.reserve(impl_->boxes.size());
}
void Host::remove_box_collider(Entity e) {
    if (!alive(e)) throw std::runtime_error("Invalid entity");
    impl_->boxes.erase(std::remove_if(impl_->boxes.begin(), impl_->boxes.end(),
        [&](const Impl::Box &b) { return b.entity == e; }), impl_->boxes.end());
}
Sweep Host::sweep(Vec3 from, Vec3 delta, float radius, uint32_t mask) const {
    if (!finite_vec(from) || !finite_vec(delta) || !finite_vec(from+delta) || !std::isfinite(radius) || radius < 0)
        throw std::invalid_argument("Invalid sweep");
    Sweep result; result.position = from+delta;
    for (const auto &box : impl_->boxes) {
        if (!(box.mask & mask)) continue;
        const auto item = impl_->objects.find(box.entity.id_);
        if (item == impl_->objects.end() || !impl_->runtime.object_alive(item->second)) continue;
        auto p = impl_->runtime.object(item->second).position;
        Aabb a; a.add(p-box.half); a.add(p+box.half);
        float t; Vec3 normal;
        if (sweep_sphere_box(from,delta,radius,a,t,normal) && (!result.hit || t < result.fraction)) {
            result.hit = true; result.fraction = t; result.normal = normal; result.entity = box.entity;
            result.position = from+delta*t;
        }
    }
    return result;
}
Slide Host::slide(Vec3 from, Vec3 delta, float radius, uint32_t mask) const {
    auto &boxes = impl_->query_boxes; boxes.clear();
    for (const auto &box : impl_->boxes) {
        if (!(box.mask & mask)) continue;
        const auto item = impl_->objects.find(box.entity.id_);
        if (item == impl_->objects.end() || !impl_->runtime.object_alive(item->second)) continue;
        auto p = impl_->runtime.object(item->second).position; Aabb a; a.add(p-box.half); a.add(p+box.half); boxes.push_back(a);
    }
    auto r = slide_sphere_boxes(from,delta,radius,boxes);
    return {r.position,r.normal,r.remaining,r.contacts};
}
void Host::play_animation(Entity e, const std::string &clip, bool loop) {
    auto it = impl_->objects.find(e.id_);
    if (it == impl_->objects.end()) throw std::runtime_error("Invalid entity");
    auto &o = impl_->runtime.object(it->second);
    if (!o.animator) throw std::runtime_error("Entity has no animator");
    o.animator->play(clip, loop);
}
void Host::reset_input() {
    for (auto k : impl_->keys) impl_->released_keys.insert(k);
    impl_->keys.clear(); impl_->pressed_keys.clear(); impl_->mouse_buttons.clear();
    impl_->gamepad_buttons.clear(); impl_->gamepad_axes.clear(); impl_->mouse_delta = {};
}
bool Host::key_pressed(int k) const { return impl_->pressed_keys.count(k) != 0; }
bool Host::key_released(int k) const { return impl_->released_keys.count(k) != 0; }
} // namespace ember::game
