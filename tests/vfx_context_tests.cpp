#include "ember/vfx_recipes.hpp"
#include "ember/mesh_builder.hpp"
#include <iostream>
#include <limits>
using namespace ember;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
struct Plane { Vec3 normal; float offset; mutable int queries = 0; };
static bool sweep_plane(Vec3 from, Vec3 delta, float radius, ParticleContact &contact, const void *context) {
    const auto &plane = *static_cast<const Plane *>(context);
    ++plane.queries;
    float travel = dot(plane.normal, delta);
    if (travel >= -1e-8f) return false;
    float t = (plane.offset + radius - dot(plane.normal, from)) / travel;
    if (t < 0 || t > 1) return false;
    contact = {t, plane.normal * 10}; // the engine normalizes returned normals
    return true;
}
int main() {
    try {
        vfx::System free(1);
        Particle p; p.position = {4,5,6}; p.velocity = {1,2,3}; p.life = 2;
        free.spawn(p); free.update(.25f);
        auto at = free.particles()[0].position;
        check(at.x == 4.25f && at.y == 5.5f && at.z == 6.75f && !free.step.collisions,
              "default particles have no genre-specific gravity, collision or plane");
        free.clear();
        Particle distant; distant.position = {0,-2e30f,0}; distant.life = 2;
        free.spawn(distant); free.update(.25f);
        check(free.alive() == 1, "neutral defaults impose no world-Y retirement boundary");

        vfx::System forces(1);
        forces.step.acceleration = {4,0,0};
        p.position = {}; p.velocity = {}; p.acceleration = {0,0,8}; forces.spawn(p);
        forces.update(.25f);
        check(forces.particles()[0].velocity.x == 1 && forces.particles()[0].velocity.z == 2,
              "system and per-particle vector forces combine");
        forces.step.bounds_enabled = true;
        forces.step.bounds.add({-.1f,-1,-1}); forces.step.bounds.add({.1f,1,1});
        forces.update(.25f); check(forces.alive() == 0, "bounds can retire particles on any axis");

        vfx::System planar(1);
        p.position = {2,3,4}; p.velocity = {1,2,4}; p.acceleration = {0,0,10};
        p.plane_normal = {1,1,1}; p.life = 10;
        planar.step.acceleration = {0,-9,0}; planar.spawn(p);
        for (int i = 0; i < 60; ++i) planar.update(1.f/60);
        check(std::fabs(dot(planar.particles()[0].position - p.position, normalized(p.plane_normal))) < .00001f,
              "arbitrary gameplay plane stays fixed without forcing XY");

        vfx::Burst jet; jet.count = 16; jet.pattern = vfx::Pattern::Cone;
        jet.direction = {1,0,0}; jet.up = jet.direction; // degenerate up uses a stable fallback
        jet.cone_angle = 0; jet.speed = 3;
        vfx::System jets(16); jets.burst({}, jet);
        for (const auto &q : jets.particles())
            check(q.velocity.x == 3 && q.velocity.y == 0 && q.velocity.z == 0, "jet follows its axis, not world Y");
        jet.cone_angle = .2f; jets.clear(); jets.burst({}, jet);
        for (const auto &q : jets.particles())
            check(q.velocity.x >= 3*std::cos(.2f)-.00001f, "cone respects its half-angle");

        vfx::System round(32);
        vfx::Burst ring; ring.count = 32; ring.pattern = vfx::Pattern::Ring; ring.direction = {0,1,0};
        round.burst({}, ring);
        bool positive = false, negative = false;
        for (const auto &q : round.particles()) {
            check(std::fabs(q.velocity.y) < .00001f, "ring orients into XZ");
            positive |= q.velocity.z > 1; negative |= q.velocity.z < -1;
        }
        check(positive && negative, "general ring has no forced upward hemisphere");
        vfx::Burst sphere; sphere.count = 32; sphere.volume = vfx::SpawnVolume::Sphere; sphere.volume_radius = 2;
        round.clear(); round.burst({}, sphere);
        positive = negative = false;
        for (const auto &q : round.particles()) {
            check(length(q.position) <= 2.00001f && std::fabs(length(q.velocity)-sphere.speed) < .00001f,
                  "sphere samples bounded positions and isotropic directions");
            positive |= q.velocity.y > 0; negative |= q.velocity.y < 0;
        }
        check(positive && negative, "sphere emits above and below");
        sphere.volume = vfx::SpawnVolume::Box;
        sphere.position_min = {-1,-2,-3}; sphere.position_max = {1,2,3};
        round.clear(); round.burst({}, sphere);
        for (const auto &q : round.particles())
            check(std::fabs(q.position.x) <= 1 && std::fabs(q.position.y) <= 2 && std::fabs(q.position.z) <= 3,
                  "box spawn volume independent of velocity pattern");

        Plane wall{{1,0,0},0}; ParticleCollider query{sweep_plane,&wall};
        p = Particle{}; p.position = {1,0,0}; p.velocity = {-100,2,0}; p.life = 3;
        for (auto response : {ParticleResponse::Bounce, ParticleResponse::Slide, ParticleResponse::Stop, ParticleResponse::Kill}) {
            vfx::System contacts(1); contacts.step.collisions = true; contacts.step.friction = 0;
            contacts.step.response = response; contacts.spawn(p);
            contacts.update(.02f, {}, query);
            if (response == ParticleResponse::Kill) check(contacts.alive() == 0, "kill on custom impact");
            else {
                const auto &q = contacts.particles()[0];
                check(q.position.x >= .0599f, "custom query prevents tunneling");
                if (response == ParticleResponse::Bounce) check(q.velocity.x > 0, "custom contact bounces");
                if (response == ParticleResponse::Slide) check(q.velocity.x == 0 && q.velocity.y == 2, "custom contact slides");
                if (response == ParticleResponse::Stop) check(length(q.velocity) == 0, "custom contact stops");
            }
        }
        check(wall.queries <= 16, "custom queries obey the contact budget");
        auto bad = [](Vec3, Vec3, float, ParticleContact &c, const void *) { c.fraction = 2; return true; };
        bool rejected = false;
        try { vfx::System s(1); s.step.collisions = true; s.spawn(p); s.update(.01f, {}, {bad,nullptr}); }
        catch (const std::invalid_argument &) { rejected = true; }
        check(rejected, "invalid external collision result rejected");

        auto dust = vfx::recipes::running_dust({1,0,0}, {0,0,4});
        check(dust.particle.acceleration.x == -20 && dust.particle.acceleration.y == 0,
              "running dust works on differently oriented surfaces");
        round.clear(); round.burst({}, dust);
        for (const auto &q : round.particles()) check(q.velocity.x > 0, "dust launches off the selected surface");
        auto sparks = vfx::recipes::planar_sparks({1,0,0}, 4, {0,1,0}, {0,0,1});
        check(sparks.particle.acceleration.z == -20 && sparks.particle.plane_normal.y == 1,
              "planar sparks recipe is reorientable");

        Runtime runtime{Config{}};
        auto prototype = mesh::quad({-.5f,0,-.5f},{.5f,0,-.5f},{.5f,0,.5f},{-.5f,0,.5f},{.5f,.8f,1});
        const size_t triangles = prototype.triangles.size();
        vfx::Visuals custom(runtime, std::move(prototype), 4);
        custom.sync(round);
        check(runtime.object(custom.object_ref()).mesh.triangles.size() == round.alive()*triangles,
              "custom low-poly particle mesh is batched");
        check(runtime.object(custom.object_ref()).mesh.triangles[0].v[0].color.x == .5f,
              "prototype vertex colors preserved");
        std::cout << "Generic VFX contexts, forces, volumes, collision adapters and recipes passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
