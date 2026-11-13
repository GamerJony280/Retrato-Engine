#pragma once

#include "collision.hpp"
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ember {

struct Particle {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 color{1, 1, 1};
    float life = 0;
    float gravity = 0; // legacy shorthand for acceleration in -Y
    // Full visual width. Collision radius is configured separately.
    float size = 0.12f;
    float end_size = 0.03f;
    float shrink_time = 0.45f;
    unsigned size_steps = 4; // 0 = smooth; otherwise retro quantized shrinking
    Vec3 acceleration{};
    Vec3 plane_normal{}; // zero = free 3D; otherwise constrain travel to this plane
};

enum class ParticleResponse { Bounce, Slide, Stop, Kill };
struct ParticleContact { float fraction = 1; Vec3 normal{}; };
struct ParticleCollider {
    // Borrowed only during update(); replace the built-in boxes with a mesh,
    // plane, tilemap or physics query. Return nearest contact on this segment.
    using Sweep = bool (*)(Vec3, Vec3, float, ParticleContact &, const void *);
    Sweep sweep = nullptr;
    const void *context = nullptr;
};
struct ParticleStep {
    bool collisions = false;
    ParticleResponse response = ParticleResponse::Bounce;
    Vec3 acceleration{}; // shared forces such as gravity/wind, added to each particle
    float bounce = 0.35f;
    float radius = 0.06f;
    float friction = 8;
    float drag = 0;
    unsigned max_contacts = 4;
    float settle_speed = 0.05f;
    float settle_life = 0; // 0 keeps the original lifetime
    Vec3 settle_normal{}; // zero = any surface, otherwise only aligned normals
    float kill_below = std::numeric_limits<float>::lowest(); // no default Y cutoff
    bool bounds_enabled = false;
    Aabb bounds{}; // optional world-space center bounds, unrelated to a game genre
};

// Fixed-capacity, data-only particle simulation. Rendering is deliberately
// game-side. Static-box collision uses Ember's continuous sphere sweep, so a
// fast particle cannot skip a thin floor merely because its endpoint is past it.
class ParticlePool {
    std::vector<Particle> items_;
    size_t cap_;
    bool updating_ = false;

  public:
    explicit ParticlePool(size_t cap = 256) : cap_(cap) { items_.reserve(cap); }
    ParticlePool(const ParticlePool &other) : cap_(other.cap_) {
        items_.reserve(cap_); // copies preserve the no-growth reservation
        items_.assign(other.items_.begin(), other.items_.end());
    }
    ParticlePool &operator=(const ParticlePool &other) {
        if(updating_) throw std::logic_error("Cannot replace particles during a query");
        if (this != &other) {
            ParticlePool copy(other);
            items_.swap(copy.items_);
            std::swap(cap_, copy.cap_);
        }
        return *this;
    }
    ParticlePool(ParticlePool &&other) : cap_(0) {
        if(other.updating_) throw std::logic_error("Cannot move particles during a query");
        items_=std::move(other.items_); cap_=other.cap_;
        other.items_.clear(); other.cap_=0;
    }
    ParticlePool &operator=(ParticlePool &&other) {
        if(updating_||other.updating_) throw std::logic_error("Cannot move particles during a query");
        if (this != &other) {
            items_ = std::move(other.items_);
            cap_ = other.cap_;
            other.items_.clear();
            other.cap_ = 0;
        }
        return *this;
    }

    size_t capacity() const { return cap_; }
    size_t alive() const { return items_.size(); }
    const std::vector<Particle> &items() const { return items_; }

    bool spawn(Vec3 position, Vec3 velocity, Vec3 color, float life, float gravity = 20) {
        return spawn(Particle{position, velocity, color, life, gravity});
    }

    bool spawn(const Particle &p) {
        if(updating_) throw std::logic_error("Cannot spawn particles during a collision query");
        if (!finite(p.position) || !finite(p.velocity) || !finite(p.color) || !std::isfinite(p.life) ||
            !std::isfinite(p.gravity) || !std::isfinite(p.size) || p.size < 0 ||
            !std::isfinite(p.end_size) || p.end_size < 0 || !std::isfinite(p.shrink_time) ||
            p.shrink_time < 0 || p.size_steps > 1024 || !finite(p.acceleration) || !finite(p.plane_normal))
            throw std::invalid_argument("Invalid particle values");
        if (items_.size() >= cap_ || p.life <= 0)
            return false;
        Particle ready = p;
        ready.plane_normal = unit(p.plane_normal);
        ready.velocity = project(p.velocity, ready.plane_normal);
        if (!finite(ready.velocity))
            throw std::invalid_argument("Particle plane projection overflow");
        items_.push_back(ready);
        return true;
    }

    void clear() { if(updating_) throw std::logic_error("Cannot clear particles during a query"); items_.clear(); }

    size_t update(float dt, const std::vector<Aabb> &solids, float bounce = 0.35f, float radius = 0.06f,
                  float friction = 8) {
        ParticleStep step;
        step.collisions = true; // preserve the original convenience overload
        step.bounce = bounce;
        step.radius = radius;
        step.friction = friction;
        return update(dt, solids, step);
    }

    size_t update(float dt, const std::vector<Aabb> &solids, const ParticleStep &step,
                  ParticleCollider collider = {}) {
        if(updating_) throw std::logic_error("Recursive particle update");
        struct Guard {bool &flag;Guard(bool &f):flag(f){flag=true;}~Guard(){flag=false;}} guard(updating_);
        if (!std::isfinite(dt) || dt < 0 || !std::isfinite(step.bounce) || step.bounce < 0 || step.bounce > 1 ||
            !std::isfinite(step.radius) || step.radius < 0 || !std::isfinite(step.friction) || step.friction < 0 ||
            !std::isfinite(step.drag) || step.drag < 0 || step.max_contacts < 1 || step.max_contacts > 16 ||
            !std::isfinite(step.settle_speed) || step.settle_speed < 0 ||
            !std::isfinite(step.settle_life) || step.settle_life < 0 || !std::isfinite(step.kill_below) ||
            !finite(step.acceleration) || !finite(step.settle_normal) ||
            (step.response != ParticleResponse::Bounce && step.response != ParticleResponse::Slide &&
             step.response != ParticleResponse::Stop && step.response != ParticleResponse::Kill) ||
            (step.bounds_enabled && (!finite(step.bounds.lo) || !finite(step.bounds.hi) ||
             step.bounds.lo.x > step.bounds.hi.x || step.bounds.lo.y > step.bounds.hi.y ||
             step.bounds.lo.z > step.bounds.hi.z)))
            throw std::invalid_argument("Invalid particle-step parameters");
        // Validate once per step, not once per particle/contact.
        if (step.collisions && !collider.sweep && !items_.empty() && dt > 0)
            for (const auto &box : solids)
                if (!finite(box.lo) || !finite(box.hi) || box.lo.x > box.hi.x ||
                    box.lo.y > box.hi.y || box.lo.z > box.hi.z)
                    throw std::invalid_argument("Invalid particle collision box");
        if (dt == 0 || items_.empty())
            return items_.size();

        const Vec3 settle = unit(step.settle_normal);
        for (auto &particle : items_) {
            particle.life -= dt;
            if (particle.life <= 0)
                continue;
            Vec3 force = step.acceleration + particle.acceleration + Vec3{0, -particle.gravity, 0};
            particle.velocity = particle.velocity + project(force, particle.plane_normal) * dt;
            particle.velocity = particle.velocity * float(1. / (1. + double(step.drag) * dt));
            float remaining = dt;
            for (unsigned contact_index = 0; contact_index < step.max_contacts && remaining > 0; ++contact_index) {
                Vec3 delta = particle.velocity * remaining;
                if (!finite(delta) || !finite(particle.velocity) || !finite(particle.position + delta)) {
                    particle.life = 0; // do not forward overflow to rendering/collision
                    break;
                }
                float first = 1;
                Vec3 normal{};
                bool hit = false;
                if (step.collisions && collider.sweep) {
                    ParticleContact contact;
                    hit = collider.sweep(particle.position, delta, step.radius, contact, collider.context);
                    if (hit) {
                        if (!std::isfinite(contact.fraction) || contact.fraction < 0 || contact.fraction > 1 ||
                            !finite(contact.normal))
                            throw std::invalid_argument("Invalid custom particle contact");
                        first = contact.fraction;
                        normal = unit(contact.normal);
                    }
                } else if (step.collisions) {
                    Vec3 to = particle.position + delta;
                    Vec3 r{step.radius, step.radius, step.radius};
                    Aabb path;
                    path.add(particle.position - r); path.add(particle.position + r);
                    path.add(to - r); path.add(to + r);
                    for (const auto &box : solids) {
                        if (path.hi.x < box.lo.x || path.lo.x > box.hi.x || path.hi.y < box.lo.y ||
                            path.lo.y > box.hi.y || path.hi.z < box.lo.z || path.lo.z > box.hi.z)
                            continue;
                        float t;
                        Vec3 n;
                        if (sweep_sphere_box(particle.position, delta, step.radius, box, t, n) && t <= first) {
                            first = t; normal = n; hit = true;
                        }
                    }
                }
                particle.position = particle.position + delta * first;
                if (!hit)
                    break;
                if (step.response == ParticleResponse::Kill) {
                    particle.life = 0;
                    break;
                }
                if (step.response == ParticleResponse::Stop) {
                    particle.velocity = {};
                    break;
                }
                normal = unit(project(normal, particle.plane_normal));
                // Started inside an expanded collider: no reliable exit normal.
                // Stop rather than tunneling or pretending to solve penetration.
                if (dot(normal, normal) < .5f) {
                    particle.velocity = {};
                    break;
                }
                particle.position = particle.position + normal * 1e-4f;
                float inward = dot(particle.velocity, normal);
                if (inward < 0)
                    particle.velocity = particle.velocity - normal *
                        ((1 + (step.response == ParticleResponse::Bounce ? step.bounce : 0)) * inward);
                float speed = dot(particle.velocity, normal);
                if (std::fabs(speed) < step.settle_speed) {
                    particle.velocity = particle.velocity - normal * speed;
                    if (step.settle_life > 0 && (dot(settle, settle) < .5f || dot(normal, settle) > .5f))
                        particle.life = std::min(particle.life, step.settle_life);
                }
                Vec3 nv = normal * dot(particle.velocity, normal);
                particle.velocity = nv + (particle.velocity - nv) * std::max(0.f, 1 - step.friction * dt);
                remaining *= 1 - first;
                // Exhausted budget drops remaining travel, keeping walls solid.
            }
            if (particle.position.y < step.kill_below || !finite(particle.position) || !finite(particle.velocity))
                particle.life = 0;
            if (step.bounds_enabled && (particle.position.x < step.bounds.lo.x || particle.position.x > step.bounds.hi.x ||
                particle.position.y < step.bounds.lo.y || particle.position.y > step.bounds.hi.y ||
                particle.position.z < step.bounds.lo.z || particle.position.z > step.bounds.hi.z))
                particle.life = 0;
        }

        size_t kept = 0;
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].life > 0)
                items_[kept++] = items_[i];
        items_.resize(kept);
        return kept;
    }

  private:
    static Vec3 unit(Vec3 v) {
        double n = std::sqrt(double(v.x)*v.x + double(v.y)*v.y + double(v.z)*v.z);
        return n > 1e-8 ? Vec3{float(v.x/n), float(v.y/n), float(v.z/n)} : Vec3{};
    }
    static Vec3 project(Vec3 v, Vec3 normal) {
        if (dot(normal, normal) < .5f) return v;
        double d = double(v.x)*normal.x + double(v.y)*normal.y + double(v.z)*normal.z;
        return {float(double(v.x)-normal.x*d), float(double(v.y)-normal.y*d), float(double(v.z)-normal.z*d)};
    }
    static bool finite(Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
};

} // namespace ember
