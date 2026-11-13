#pragma once
#include "collision.hpp"
#include <stdexcept>
#include <vector>

namespace ember {
struct Projectile {
    Vec3 position{}, velocity{};
    float radius = .05f, life = 1;
    uint64_t id = 0, payload = 0;
};
struct ProjectileContact { float fraction = 1; Vec3 normal{}; uint64_t target = 0; };
struct ProjectileQuery {
    using Sweep = bool(*)(Vec3, Vec3, float, ProjectileContact &, const void *);
    Sweep sweep = nullptr; const void *context = nullptr;
};
struct ProjectileImpact { Projectile projectile; Vec3 normal{}; uint64_t target = 0; };
// Data-only, bounded, no renderer/Runtime/VFX dependency. Collision is a nearest
// swept contact, not discrete substeps. Caller owns damage/team/visual policy.
class ProjectilePool {
    std::vector<Projectile> items_;
    std::vector<ProjectileImpact> hits_;
    size_t capacity_;
    uint64_t next_ = 1;
    bool updating_ = false;
    static bool finite(Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
  public:
    explicit ProjectilePool(size_t capacity=24) : capacity_(capacity) {
        if(capacity>65536) throw std::invalid_argument("Projectile capacity must be 0..65536");
        items_.reserve(capacity); hits_.reserve(capacity);
    }
    ProjectilePool(const ProjectilePool &) = delete;
    ProjectilePool &operator=(const ProjectilePool &) = delete;
    uint64_t spawn(Projectile p) {
        if(updating_) throw std::logic_error("Cannot mutate projectiles during a collision query");
        if(!finite(p.position)||!finite(p.velocity)||!std::isfinite(p.radius)||p.radius<0||!std::isfinite(p.life))
            throw std::invalid_argument("Invalid projectile");
        if(p.life<=0||items_.size()==capacity_) return 0;
        if(next_==UINT64_MAX) throw std::runtime_error("Projectile IDs exhausted");
        p.id=next_++; items_.push_back(p); return p.id;
    }
    void update(float dt, ProjectileQuery query={}) {
        if(updating_) throw std::logic_error("Recursive projectile update");
        struct Guard {bool &flag;Guard(bool &f):flag(f){flag=true;}~Guard(){flag=false;}} guard(updating_);
        if(!std::isfinite(dt)||dt<0) throw std::invalid_argument("Invalid projectile delta");
        hits_.clear();
        if(dt==0) return;
        size_t kept=0;
        for(auto p : items_) {
            float travel=std::min(dt,p.life);
            Vec3 delta=p.velocity*travel;
            if(!finite(delta)||!finite(p.position+delta)) continue;
            ProjectileContact hit;
            bool collided=query.sweep && query.sweep(p.position,delta,p.radius,hit,query.context);
            if(collided && (!std::isfinite(hit.fraction)||hit.fraction<0||hit.fraction>1||!finite(hit.normal)))
                throw std::invalid_argument("Invalid projectile contact");
            p.position=p.position+delta*(collided ? hit.fraction : 1);
            p.life-=travel;
            if(collided) hits_.push_back({p,hit.normal,hit.target});
            else if(p.life>0) items_[kept++]=p;
        }
        items_.resize(kept);
    }
    void clear() { if(updating_) throw std::logic_error("Cannot clear projectiles during a query"); items_.clear(); hits_.clear(); }
    const std::vector<Projectile> &items() const { return items_; }
    const std::vector<ProjectileImpact> &impacts() const { return hits_; }
    size_t alive() const { return items_.size(); }
};
}
