#include "ember/vfx.hpp"

namespace ember::vfx {
namespace {
bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
Vec3 unit(Vec3 v) {
    double n = std::sqrt(double(v.x)*v.x + double(v.y)*v.y + double(v.z)*v.z);
    return n > 1e-8 ? Vec3{float(v.x/n), float(v.y/n), float(v.z/n)} : Vec3{};
}
void validate(const Burst &b, Vec3 position) {
    if (!finite(position) || !finite(b.particle.velocity) || !finite(b.particle.color) ||
        !std::isfinite(b.particle.gravity) || !std::isfinite(b.particle.size) || b.particle.size < 0 ||
        !std::isfinite(b.particle.end_size) || b.particle.end_size < 0 ||
        !std::isfinite(b.particle.shrink_time) || b.particle.shrink_time < 0 || b.particle.size_steps > 1024 ||
        !std::isfinite(b.speed) || b.speed < 0 || !std::isfinite(b.phase) ||
        !std::isfinite(b.speed_min_factor) || b.speed_min_factor < 0 || b.speed_min_factor > 1 ||
        !finite(b.particle.acceleration) || !finite(b.particle.plane_normal) ||
        !finite(b.direction) || !finite(b.up) || !std::isfinite(b.arc_min) || !std::isfinite(b.arc_max) ||
        b.arc_max < b.arc_min || !std::isfinite(b.cone_angle) || b.cone_angle < 0 || b.cone_angle > 3.1415927f ||
        !finite(b.position_min) || !finite(b.position_max) || b.position_min.x > b.position_max.x ||
        b.position_min.y > b.position_max.y || b.position_min.z > b.position_max.z ||
        !std::isfinite(b.volume_radius) || b.volume_radius < 0 ||
        (b.volume != SpawnVolume::Point && b.volume != SpawnVolume::Box && b.volume != SpawnVolume::Sphere) ||
        !std::isfinite(b.life_min) || !std::isfinite(b.life_max) || b.life_min <= 0 || b.life_max < b.life_min ||
        !finite(b.velocity_min) || !finite(b.velocity_max) || b.velocity_min.x > b.velocity_max.x ||
        b.velocity_min.y > b.velocity_max.y || b.velocity_min.z > b.velocity_max.z ||
        (b.pattern != Pattern::Ring && b.pattern != Pattern::Box3D &&
         b.pattern != Pattern::Sphere && b.pattern != Pattern::Cone) ||
        ((b.pattern == Pattern::Ring || b.pattern == Pattern::Cone) && dot(unit(b.direction), unit(b.direction)) < .5f))
        throw std::invalid_argument("Invalid VFX burst");
}
size_t checked_capacity(size_t capacity) {
    // Bounds work and triangle-count arithmetic. A zero-capacity system is valid.
    if (capacity > 65536)
        throw std::invalid_argument("VFX capacity must be 0..65536");
    return capacity;
}
} // namespace

System::System(size_t capacity, uint32_t seed) : pool_(checked_capacity(capacity)), random_(seed ? seed : 1) {}

float System::random_unit() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return float(random_ >> 8) * (1.f / 16777216.f);
}

size_t System::burst(Vec3 position, const Burst &b) {
    validate(b, position);
    const size_t count = std::min(b.count, capacity() - alive());
    const Vec3 forward = unit(b.direction);
    Vec3 right = unit(cross(unit(b.up), forward));
    if (dot(right, right) < .5f)
        right = unit(cross(std::fabs(forward.y) > .99f ? Vec3{0,0,1} : Vec3{0,1,0}, forward));
    const Vec3 up = cross(forward, right);
    const Vec3 plane = unit(b.particle.plane_normal);
    auto sphere = [&]() {
        double z = 2 * double(random_unit()) - 1;
        double a = double(random_unit()) * 6.283185307179586;
        double r = std::sqrt(std::max(0., 1 - z*z));
        return Vec3{float(r*std::cos(a)), float(r*std::sin(a)), float(z)};
    };
    for (size_t i = 0; i < count; ++i) {
        Particle p = b.particle;
        // Double intermediates avoid overflow when valid endpoints are far apart.
        auto lerp = [](float a, float z, float t) { return float(double(a) * (1 - t) + double(z) * t); };
        p.life = lerp(b.life_min, b.life_max, random_unit());
        Vec3 offset{};
        if (b.volume == SpawnVolume::Box)
            offset = {lerp(b.position_min.x, b.position_max.x, random_unit()),
                      lerp(b.position_min.y, b.position_max.y, random_unit()),
                      lerp(b.position_min.z, b.position_max.z, random_unit())};
        else if (b.volume == SpawnVolume::Sphere)
            offset = sphere() * (b.volume_radius * std::cbrt(random_unit()));
        if (dot(plane, plane) > .5f)
            offset = offset - plane * dot(offset, plane);
        p.position = position + offset;
        Vec3 velocity;
        if (b.pattern == Pattern::Ring) {
            const double a = double(b.arc_min) + (double(b.arc_max) - b.arc_min) * double(i) / double(b.count) + b.phase;
            velocity = right * float(std::cos(a)) + up * float(std::sin(a));
        } else if (b.pattern == Pattern::Cone) {
            double z = 1 - double(random_unit()) * (1 - std::cos(double(b.cone_angle)));
            double a = double(random_unit()) * 6.283185307179586;
            double r = std::sqrt(std::max(0., 1-z*z));
            velocity = forward * float(z) + right * float(r*std::cos(a)) + up * float(r*std::sin(a));
        } else if (b.pattern == Pattern::Sphere) {
            velocity = sphere();
        } else {
            velocity = {lerp(b.velocity_min.x, b.velocity_max.x, random_unit()),
                        lerp(b.velocity_min.y, b.velocity_max.y, random_unit()),
                        lerp(b.velocity_min.z, b.velocity_max.z, random_unit())};
        }
        if (b.pattern != Pattern::Box3D)
            velocity = velocity * (b.speed * lerp(b.speed_min_factor, 1, random_unit()));
        p.velocity = p.velocity + velocity;
        pool_.spawn(p);
    }
    return count;
}

size_t Emitter::emit(System &system, Vec3 position, float dt) {
    if (!std::isfinite(rate) || rate < 0 || rate > 1000000 || !std::isfinite(dt) || dt < 0 || dt > 1)
        throw std::invalid_argument("VFX emitter requires rate 0..1000000 and delta 0..1");
    validate(effect, position);
    double total = fraction_ + double(rate) * dt;
    double whole = std::floor(total);
    fraction_ = total - whole;
    Burst b = effect;
    b.count = size_t(whole); // bounded by rate and delta above
    return system.burst(position, b);
}

float particle_size(const Particle &p) {
    float t = p.shrink_time > 0 ? std::clamp(p.life / p.shrink_time, 0.f, 1.f) : 1.f;
    if (p.size_steps)
        t = std::floor(t * p.size_steps) / p.size_steps;
    return float(double(p.end_size) * (1 - t) + double(p.size) * t);
}

Runtime &Visuals::runtime() const {
    if (lifetime_.expired())
        throw std::runtime_error("VFX visuals outlived their Runtime");
    return *runtime_;
}

Visuals::Visuals(Runtime &rt, size_t capacity, Shape shape)
    : runtime_(&rt), lifetime_(rt.lifetime_token()), capacity_(checked_capacity(capacity)), shape_(shape) {
    if (shape != Shape::Cube && shape != Shape::Billboard)
        throw std::invalid_argument("Invalid VFX shape");
    if (shape == Shape::Cube)
        unit_ = box({1, 1, 1}, {1, 1, 1});
    // Cube: 12 triangles. Billboard: 4 triangles (two faces, shared semantics
    // on CPU/GL/Vulkan, no new shader/material/blending contracts).
    storage_.triangles.reserve(capacity_ * (shape == Shape::Cube ? 12 : 4));
}

Visuals::Visuals(Runtime &rt, Mesh prototype, size_t capacity)
    : runtime_(&rt), lifetime_(rt.lifetime_token()), unit_(std::move(prototype)),
      capacity_(checked_capacity(capacity)), shape_(Shape::Custom) {
    if (unit_.triangles.empty() || unit_.triangles.size() > 1024)
        throw std::invalid_argument("Particle prototype requires 1..1024 low-poly triangles");
    for (const auto &t : unit_.triangles) {
        if (!finite(t.color)) throw std::invalid_argument("Invalid particle prototype color");
        for (const auto &v : t.v)
            if (!finite(v.position) || !finite(v.color) || !finite(v.normal) ||
                !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y))
                throw std::invalid_argument("Invalid particle prototype vertex");
    }
    storage_.triangles.reserve(capacity_ * unit_.triangles.size());
}

Visuals::~Visuals() {
    if (!lifetime_.expired() && runtime_->object_alive(object_))
        runtime_->destroy_object(object_);
}

void Visuals::clear() {
    Runtime &rt = runtime();
    if (rt.object_alive(object_))
        rt.object(object_).mesh.triangles.clear(); // retains reserved memory
}

void Visuals::sync(const System &system) {
    Runtime &rt = runtime();
    size_t count = std::min(capacity_, system.alive());
    if (!count) {
        clear();
        return;
    }
    Vec3 right{1, 0, 0}, up{0, 1, 0};
    if (shape_ == Shape::Billboard) {
        if (!finite(rt.camera.position) || !finite(rt.camera.target))
            throw std::invalid_argument("Invalid VFX billboard camera");
        Vec3 forward = normalized(rt.camera.target - rt.camera.position);
        if (length(forward) < .5f)
            throw std::invalid_argument("VFX billboard camera has no direction");
        right = normalized(cross(forward, std::fabs(forward.y) > .99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}));
        up = normalized(cross(right, forward));
    }
    if (!rt.object_alive(object_)) {
        // Handles survive vector reallocations; scene clear/destroy invalidates
        // the ID. Create a new owned slot without touching a reused foreign one.
        storage_.triangles.reserve(capacity_ * (shape_ == Shape::Billboard ? 4 : unit_.triangles.size()));
        Object object;
        object.mesh = std::move(storage_);
        object_ = rt.add_object(std::move(object));
    }
    auto &object = rt.object(object_);
    object.position = {};
    object.yaw = 0;
    object.parent_transform = {};
    auto &out = object.mesh.triangles;
    const size_t stride = shape_ == Shape::Billboard ? 4 : unit_.triangles.size();
    size_t previous = out.size();
    out.resize(count * stride);
    if (shape_ != Shape::Billboard)
        for (size_t i = previous; i < out.size(); ++i)
            out[i] = unit_.triangles[i % stride];
    for (size_t i = 0; i < count; ++i) {
        const auto &p = system.particles()[i];
        const float size = particle_size(p);
        if (shape_ != Shape::Billboard) {
            for (size_t j = 0; j < stride; ++j) {
                auto &t = out[i * stride + j];
                t.color = mul(p.color, unit_.triangles[j].color);
                for (int v = 0; v < 3; ++v)
                    t.v[v].position = p.position + unit_.triangles[j].v[v].position * size;
            }
        } else {
            Vec3 a = right * (size * .5f), b = up * (size * .5f);
            Vec3 corners[4] = {p.position - a - b, p.position + a - b, p.position + a + b, p.position - a + b};
            constexpr int indices[4][3] = {{0, 1, 2}, {0, 2, 3}, {2, 1, 0}, {3, 2, 0}};
            for (size_t j = 0; j < 4; ++j) {
                auto &t = out[i * 4 + j];
                t.color = p.color;
                for (int v = 0; v < 3; ++v)
                    t.v[v].position = corners[indices[j][v]];
            }
        }
    }
}

Runtime &TimedMeshes::runtime() const {
    if (lifetime_.expired())
        throw std::runtime_error("VFX timed meshes outlived their Runtime");
    return *runtime_;
}

TimedMeshes::TimedMeshes(Runtime &rt, size_t capacity)
    : runtime_(&rt), lifetime_(rt.lifetime_token()), capacity_(checked_capacity(capacity)) {
    entries_.reserve(capacity_);
}

TimedMeshes::~TimedMeshes() {
    if (!lifetime_.expired())
        clear();
}

ObjectRef TimedMeshes::spawn(Mesh mesh, Vec3 position, float life, float yaw) {
    Runtime &rt = runtime();
    if (!finite(position) || !std::isfinite(life) || !std::isfinite(yaw))
        throw std::invalid_argument("Invalid timed VFX mesh");
    update(0); // release stale handles before applying capacity
    if (life <= 0 || entries_.size() == capacity_)
        return {};
    ObjectRef ref = rt.add_object(Object{std::move(mesh), position, yaw});
    entries_.push_back({ref, life});
    return ref;
}

void TimedMeshes::update(float dt) {
    Runtime &rt = runtime();
    if (!std::isfinite(dt) || dt < 0)
        throw std::invalid_argument("Invalid timed VFX delta");
    size_t kept = 0;
    for (auto entry : entries_) {
        if (!rt.object_alive(entry.object))
            continue;
        entry.life -= dt;
        if (entry.life <= 0)
            rt.destroy_object(entry.object);
        else
            entries_[kept++] = entry;
    }
    entries_.resize(kept);
}

void TimedMeshes::clear() {
    Runtime &rt = runtime();
    for (const auto &entry : entries_)
        if (rt.object_alive(entry.object))
            rt.destroy_object(entry.object);
    entries_.clear();
}

size_t TimedMeshes::alive() const {
    if (lifetime_.expired())
        return 0;
    return size_t(std::count_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
        return runtime_->object_alive(entry.object);
    }));
}

} // namespace ember::vfx
