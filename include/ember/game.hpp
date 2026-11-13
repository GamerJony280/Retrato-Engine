#pragma once
#include "physics.hpp"
#include <functional>
#include <memory>
#include <string>

namespace ember {
struct Config;
enum class Profile;
class Backend;
class SceneRenderer;
struct Frame;
class Runtime;
namespace game {
// Opaque, typed values. IDs are never addresses or scene indices.
class Entity {
    uint64_t id_ = 0;
    explicit Entity(uint64_t id) : id_(id) {}
    friend class Host;

  public:
    Entity() = default;
    bool operator==(Entity other) const { return id_ == other.id_; }
    bool operator!=(Entity other) const { return !(*this == other); }
};
class AssetHandle {
    uint64_t id_ = 0;
    explicit AssetHandle(uint64_t id) : id_(id) {}
    friend class Host;

  public:
    AssetHandle() = default;
};
struct Transform {
    Vec3 position{};
    float yaw = 0;
};
struct Sweep {
    bool hit = false;
    float fraction = 1;
    Vec3 position{}, normal{};
    Entity entity{};
};
struct Slide { Vec3 position{}, normal{}, remaining{}; int contacts = 0; };
// Renderer/platform independent input payload. SDL is only one possible
// producer and never leaks into the public game API.
struct InputEvent {
    enum class Type {
        KeyDown,
        KeyUp,
        MouseMove,
        MouseDown,
        MouseUp,
        MouseWheel,
        GamepadButtonDown,
        GamepadButtonUp,
        GamepadAxis
    } type;
    double time = 0;
    int code = 0, device = 0;
    float x = 0, y = 0, dx = 0, dy = 0;
};
// Disconnects automatically. May safely outlive Host. Move-only.
class Connection {
    std::function<void()> disconnect_;
    explicit Connection(std::function<void()> fn) : disconnect_(std::move(fn)) {}
    friend class Host;

  public:
    Connection() = default;
    ~Connection() { disconnect(); }
    Connection(Connection &&other) noexcept : disconnect_(std::move(other.disconnect_)) {
        other.disconnect_ = {};
    }
    Connection &operator=(Connection &&other) noexcept {
        if (this != &other) {
            disconnect();
            disconnect_ = std::move(other.disconnect_);
            other.disconnect_ = {};
        }
        return *this;
    }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    void disconnect() {
        if (disconnect_) {
            auto fn = std::move(disconnect_);
            disconnect_ = {};
            fn();
        }
    }
};
// Shared session ownership; copying Host keeps its Runtime alive, not a scene copy.
// Single simulation thread. No public mutable Scene or renderer layouts.
class Host {
    struct Impl;
    std::shared_ptr<Impl> impl_;

  public:
    uint64_t session_id() const; // opaque identity for native adapters, never an address
    explicit Host(const Config &);
    explicit Host(std::shared_ptr<Runtime>); // share one world with native code and Lua
    void unload_asset(AssetHandle);
    void set_box(Entity, Vec3 size, Vec3 color);
    void set_visible(Entity, bool);
    void set_camera(Vec3 position, Vec3 target);
    void set_light(int index, Vec3 position, Vec3 color, float radius);
    void add_box_collider(Entity, Vec3 half, uint32_t mask = 1);
    void remove_box_collider(Entity);
    Sweep sweep(Vec3 from, Vec3 delta, float radius, uint32_t mask = UINT32_MAX) const;
    Slide slide(Vec3 from, Vec3 delta, float radius, uint32_t mask = 1) const;
    void play_animation(Entity, const std::string &clip, bool loop = true);
    void reset_input();
    bool key_pressed(int code) const;
    bool key_released(int code) const;
    Entity create(const std::string &name);
    Entity create_box(const std::string &name, Vec3 size, Vec3 color);
    Entity entity(const std::string &name) const;
    AssetHandle load_asset(const std::string &path);
    // Optional Lua module, owned by this Host session. Fails clearly when Lua
    // was not built. Startup runs once, subsequent runtime events are shared.
    void load_script(const std::string &path);
    Entity spawn(AssetHandle, const std::string &name);
    bool alive(Entity) const;
    void destroy(Entity);
    void prune_destroyed(); // reclaim Host records after native Runtime/Scene destruction
    Transform transform(Entity) const;
    void set_transform(Entity, Transform);
    Vec3 position(Entity e) const { return transform(e).position; }
    void set_position(Entity e, Vec3 p) {
        auto t = transform(e);
        t.position = p;
        set_transform(e, t);
    }
    bool advanced_physics_enabled() const noexcept;
    void add_rigid_body(Entity, BodyDesc);
    bool has_rigid_body(Entity) const;
    void remove_rigid_body(Entity);
    BodyState rigid_body_state(Entity) const;
    void set_velocity(Entity, Vec3);
    Vec3 velocity(Entity) const;
    void add_force(Entity, Vec3);
    void add_impulse(Entity, Vec3);
    void optimize_physics_broadphase();
    Connection on_update(std::function<void(float)>);
    Connection on_signal(std::function<void(int64_t)>);
    Connection on_input(std::function<void(const InputEvent &)>);
    bool key_down(int code) const;
    Vec2 mouse_position() const;
    Vec2 mouse_delta() const;
    bool mouse_down(int button) const;
    bool gamepad_down(int device, int button) const;
    float gamepad_axis(int device, int axis) const;
    void emit(int64_t code); // queued; delivered after updates, bounded per tick
    // Native platform adapter entry; generated Nova code only receives input
    // through stable Host facilities, never SDL structures.
    void dispatch_input(InputEvent);
    void tick(double elapsed);
    double time() const;
    float delta() const;
    void set_profile(Profile profile);
    Profile profile() const;
    // Native application entry only; not a Nova builtin.
    void render(SceneRenderer &, Backend &, Frame &) const;
};
} // namespace game
} // namespace ember
