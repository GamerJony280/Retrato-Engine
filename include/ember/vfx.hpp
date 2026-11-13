#pragma once

#include "particles.hpp"
#include "runtime.hpp"
#include <cstdint>

// Optional C++ module. Link ember_vfx, never the renderer directly.
namespace ember::vfx {

enum class Pattern { Box3D, Ring, Sphere, Cone, RingXY = Ring }; // RingXY: source compatibility alias
enum class SpawnVolume { Point, Box, Sphere };
enum class Shape { Cube, Billboard, Custom };

struct Burst {
    Particle particle{}; // color, gravity, size and shrinking
    size_t count = 8;
    Pattern pattern = Pattern::Sphere;
    float speed = 4;
    float speed_min_factor = 1; // random speed between speed*factor and speed
    float phase = 0;
    Vec3 direction{0, 0, 1}, up{0, 1, 0};
    float arc_min = 0, arc_max = 6.2831853f; // ring, radians
    float cone_angle = .5f; // half-angle in radians, 0 = jet
    float life_min = .5f, life_max = .9f;
    // Box3D samples each component independently, then adds particle.velocity.
    Vec3 velocity_min{-2, -2, -2}, velocity_max{2, 2, 2};
    SpawnVolume volume = SpawnVolume::Point;
    Vec3 position_min{}, position_max{}; // world-space offsets for Box
    float volume_radius = 0;
};

// Pure simulation: owns data only, never retains a Scene/Runtime/collider.
class System {
    ParticlePool pool_;
    uint32_t random_;
    float random_unit();

  public:
    ParticleStep step{};
    explicit System(size_t capacity = 96, uint32_t seed = 1);
    size_t burst(Vec3 position, const Burst &);
    bool spawn(const Particle &p) { return pool_.spawn(p); }
    size_t update(float dt, const std::vector<Aabb> &solids = {}, ParticleCollider collider = {}) {
        return pool_.update(dt, solids, step, collider);
    }
    void clear() { pool_.clear(); }
    size_t alive() const { return pool_.alive(); }
    size_t capacity() const { return pool_.capacity(); }
    const std::vector<Particle> &particles() const { return pool_.items(); }
};

// Fractional-rate emitter for dust, sparks and trails. Call only while enabled;
// missed emissions at capacity are dropped, never queued for a later explosion.
class Emitter {
    double fraction_ = 0;

  public:
    float rate = 30;
    Burst effect{};
    size_t emit(System &, Vec3 position, float dt);
    void reset() { fraction_ = 0; }
};

float particle_size(const Particle &);

// One bounded mesh per batch. No per-particle objects, colliders, materials or
// meshes. sync() rewrites positions/colors without reallocating after creation.
// Simulation may run at a different rate from sync()/rendering.
class Visuals {
    Runtime *runtime_;
    std::weak_ptr<void> lifetime_;
    ObjectRef object_{};
    Mesh storage_;
    Mesh unit_;
    size_t capacity_;
    Shape shape_;
    Runtime &runtime() const;

  public:
    Visuals(Runtime &, size_t capacity = 96, Shape = Shape::Cube);
    Visuals(Runtime &, Mesh prototype, size_t capacity = 96);
    ~Visuals();
    Visuals(const Visuals &) = delete;
    Visuals &operator=(const Visuals &) = delete;
    Visuals(Visuals &&) = delete;
    Visuals &operator=(Visuals &&) = delete;
    // Billboard orientation uses Runtime::camera. Excess particles are omitted.
    void sync(const System &);
    void clear();
    ObjectRef object_ref() const { return object_; }
};

// Bounded lifetime ownership for user-made slashes, rings and shockwaves.
// Returned ObjectRefs can be transformed through Runtime while alive. No
// callbacks, scheduler or gameplay policy: expiration only destroys our IDs.
class TimedMeshes {
    struct Entry { ObjectRef object; float life; };
    Runtime *runtime_;
    std::weak_ptr<void> lifetime_;
    std::vector<Entry> entries_;
    size_t capacity_;
    Runtime &runtime() const;

  public:
    explicit TimedMeshes(Runtime &, size_t capacity = 8);
    ~TimedMeshes();
    TimedMeshes(const TimedMeshes &) = delete;
    TimedMeshes &operator=(const TimedMeshes &) = delete;
    TimedMeshes(TimedMeshes &&) = delete;
    TimedMeshes &operator=(TimedMeshes &&) = delete;
    ObjectRef spawn(Mesh, Vec3 position, float life, float yaw = 0);
    void update(float dt);
    void clear();
    size_t alive() const;
};

} // namespace ember::vfx
