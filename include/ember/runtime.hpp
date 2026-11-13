#pragma once
#include "asset.hpp"
#include "collision.hpp"
#include "physics.hpp"
#include "render.hpp"
#include <functional>
#include <unordered_map>
namespace ember {
struct Event {
    enum class Type {
        Start,
        FixedUpdate,
        KeyDown,
        KeyUp,
        Cue,
        MouseMove,
        MouseDown,
        MouseUp,
        MouseWheel,
        GamepadButtonDown,
        GamepadButtonUp,
        GamepadAxis
    } type;
    double time = 0;
    int code = 0;
    int device = 0;
    float x = 0, y = 0, dx = 0, dy = 0;
};
struct ObjectRef {
    size_t index = 0;
    uint64_t id = 0;
    bool operator==(const ObjectRef &other) const { return index == other.index && id == other.id; }
};
struct ColliderRef {
    size_t index = 0;
    uint64_t id = 0;
};
class Runtime {
  public:
    class EventConnection {
        std::function<void()> disconnect_;
        explicit EventConnection(std::function<void()> fn) : disconnect_(std::move(fn)) {}
        friend class Runtime;
      public:
        EventConnection() = default;
        ~EventConnection() { disconnect(); }
        EventConnection(const EventConnection &) = delete;
        EventConnection &operator=(const EventConnection &) = delete;
        EventConnection(EventConnection &&other) noexcept : disconnect_(std::move(other.disconnect_)) { other.disconnect_ = {}; }
        EventConnection &operator=(EventConnection &&other) noexcept {
            if (this != &other) { disconnect(); disconnect_ = std::move(other.disconnect_); other.disconnect_ = {}; }
            return *this;
        }
        void disconnect() { if (disconnect_) { auto fn = std::move(disconnect_); disconnect_ = {}; fn(); } }
    };
    EventConnection observe(std::function<void(Runtime &, const Event &)>);
  private:
    struct EventListeners;
    std::shared_ptr<EventListeners> events_;

    struct Cue {
        double time;
        int code;
    };
    std::vector<Cue> cues_;
    size_t cue_index_ = 0;
    double accumulator_ = 0, time_ = 0;
    bool ticking_ = false;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    struct Attachment {
        ObjectRef child, parent;
        std::string bone;
    };
    struct Collider {
        ObjectRef object;
        CollisionMesh mesh;
        uint64_t id = 0;
    };
    struct PhysicsBinding {
        ObjectRef object;
        BodyRef body;
        BodyMotion motion = BodyMotion::Static;
    };
    std::vector<Attachment> attachments_;
    // Identity as well as index protects against direct replacement/reordering
    // of the public Scene vector. Only destroy_object publishes reusable slots.
    std::vector<ObjectRef> free_objects_;
    std::vector<Collider> colliders_;
    std::vector<PhysicsBinding> physics_bindings_;
    std::unique_ptr<PhysicsWorld> physics_;
    uint64_t next_collider_id_ = 1;
    void prune_stale_references();
    PhysicsBinding &physics_binding(ObjectRef);
    const PhysicsBinding &physics_binding(ObjectRef) const;
    void step_physics(float delta);

  public:
    std::unordered_map<std::string, std::weak_ptr<const Asset>> asset_cache;
    ObjectRef add_object(Object object);
    size_t add_model(const std::string &, Vec3 position = {});
    void destroy_object(ObjectRef object);
    void attach(size_t child, size_t parent, const std::string &bone);
    void attach(ObjectRef child, ObjectRef parent, const std::string &bone);
    void update_attachments();
    ObjectRef object_ref(size_t index) const;
    bool object_alive(ObjectRef ref) const;
    Object &object(ObjectRef ref);
    const Object &object(ObjectRef ref) const;
    ColliderRef add_collider(ObjectRef object);
    void remove_collider(ColliderRef collider);
    bool raycast(ColliderRef collider, Vec3 from, Vec3 direction, float distance, Hit &hit) const;
    bool advanced_physics_enabled() const noexcept { return bool(physics_); }
    BodyRef add_rigid_body(ObjectRef object, BodyDesc desc);
    bool has_rigid_body(ObjectRef object) const noexcept;
    void remove_rigid_body(ObjectRef object);
    BodyState rigid_body_state(ObjectRef object) const;
    void set_object_transform(ObjectRef object, Vec3 position, float yaw);
    void set_rigid_body_velocity(ObjectRef object, Vec3 velocity);
    Vec3 rigid_body_velocity(ObjectRef object) const;
    void add_rigid_body_force(ObjectRef object, Vec3 force);
    void add_rigid_body_impulse(ObjectRef object, Vec3 impulse);
    void optimize_physics_broadphase();
    void clear_scene();
    Config config;
    Scene scene;
    Camera camera;
    Frame frame;
    std::function<void(Runtime &, const Event &)> on_event;
    explicit Runtime(Config c);
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    Runtime(Runtime &&) = delete;
    Runtime &operator=(Runtime &&) = delete;
    std::weak_ptr<void> lifetime_token() const { return lifetime_; }
    void start();
    void tick(double elapsed);
    void dispatch(Event event);
    void add_cue(double time, int code);
    size_t pending_cues() const noexcept { return cues_.size() - cue_index_; }
    double time() const { return time_; }
};
class Script {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Script(Runtime &, const std::string &file);
    ~Script();
    Script(const Script &) = delete;
    Script &operator=(const Script &) = delete;
    void event(const Event &);
};
} // namespace ember
