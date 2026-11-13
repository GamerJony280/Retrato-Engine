#include "ember/vfx.hpp"
#include "ember/vfx_recipes.hpp"
#include "ember/mesh_builder.hpp"
#include <iostream>
#include <cstdlib>
#include <limits>
#include <type_traits>

using namespace ember;
static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
template<class F> static void invalid(F fn) {
    bool caught = false;
    try { fn(); } catch (const std::exception &) { caught = true; }
    require(caught, "invalid VFX input must fail");
}
static Aabb bounds(Vec3 lo, Vec3 hi) { Aabb b; b.add(lo); b.add(hi); return b; }

int main() {
    try {
        static_assert(!std::is_copy_constructible<vfx::Visuals>::value, "visual ownership is unique");
        static_assert(!std::is_copy_constructible<vfx::TimedMeshes>::value, "effect ownership is unique");
        vfx::System fx(4, 9), twin(4, 9);
        vfx::Burst burst;
        burst.count = 8;
        burst.pattern = vfx::Pattern::Box3D;
        require(fx.burst({0, 2, 0}, burst) == 4, "burst cannot grow past capacity");
        twin.burst({0, 2, 0}, burst);
        for (size_t i = 0; i < fx.alive(); ++i) {
            const auto &a = fx.particles()[i], &b = twin.particles()[i];
            require(a.life == b.life && a.velocity.x == b.velocity.x && a.velocity.z == b.velocity.z,
                    "same seed reproduces emission on this build");
        }
        require(fx.burst({}, burst) == 0, "full pool drops overflow");
        auto data = fx.particles().data();
        fx.update(0);
        require(fx.alive() == 4 && data == fx.particles().data(), "zero delta is a no-op");
        fx.update(2);
        require(fx.alive() == 0 && fx.particles().data() == data, "expiry retains reserved particle storage");
        fx.burst({}, burst);
        require(fx.particles().data() == data, "respawn reuses particle storage");
        invalid([&] { vfx::System bad(65537); });
        invalid([&] { auto bad = burst; bad.life_min = 0; fx.burst({}, bad); });
        invalid([&] { auto bad = burst; bad.particle.color.x = std::numeric_limits<float>::quiet_NaN(); fx.burst({}, bad); });
        invalid([&] { fx.update(-1); });
        fx.step.collisions = true;
        invalid([&] { fx.update(.01f, {Aabb{}}); });
        fx.step.collisions = false;
        {
            ParticlePool original(8);
            original.spawn({}, {}, {1,1,1}, 1);
            ParticlePool copy = original;
            auto reserved = copy.items().data();
            for (int i = 0; i < 7; ++i) copy.spawn({}, {}, {1,1,1}, 1);
            require(copy.items().data() == reserved && original.alive() == 1,
                    "copy reserves full capacity and owns independent particle data");
            ParticlePool moved = std::move(copy);
            require(moved.alive() == 8 && copy.capacity() == 0 && !copy.spawn({}, {}, {1,1,1}, 1),
                    "move transfers storage and leaves a disabled source");
        }

        // Fast damage debris hits a thin floor and spends the remaining step
        // bouncing away. This is continuous response, not endpoint collision.
        vfx::System bounce(1);
        bounce.step.collisions = true;
        bounce.step.friction = 0;
        bounce.step.bounce = .5f;
        Particle p;
        p.position = {0, 1, 0}; p.velocity = {0, -100, 0}; p.gravity = 0; p.life = 3;
        bounce.spawn(p);
        const auto floor = bounds({-10, -.01f, -10}, {10, 0, 10});
        bounce.update(.02f, {floor});
        require(bounce.particles()[0].position.y > .55f && bounce.particles()[0].velocity.y == 50,
                "continuous bounce uses time after impact");

        // Multiple walls in one step, with a small contact budget. No tunnel.
        vfx::System corridor(1);
        corridor.step.collisions = true;
        corridor.step.bounce = 1; corridor.step.friction = 0; corridor.step.max_contacts = 2;
        p.position = {}; p.velocity = {1000, 0, 0}; corridor.spawn(p);
        corridor.update(.02f, {bounds({1, -2, -2}, {1.01f, 2, 2}), bounds({-1.01f, -2, -2}, {-1, 2, 2})});
        require(std::fabs(corridor.particles()[0].position.x) < 1, "budget exhaustion keeps particles between walls");

        vfx::System settling(1);
        settling.step.collisions = true;
        settling.step.bounce = 0; settling.step.settle_speed = 1; settling.step.settle_life = .1f;
        p.position = {0, .08f, 0}; p.velocity = {2, -2, 0}; settling.spawn(p);
        settling.update(.02f, {floor});
        require(settling.particles()[0].life <= .1f && settling.particles()[0].velocity.y == 0,
                "settled debris expires early");
        settling.update(.2f, {floor}); require(settling.alive() == 0, "settled debris is reclaimed");

        vfx::System ghost(1);
        ghost.step.collisions = false;
        p.position = {0, 1, 0}; p.velocity = {0, -100, 0}; ghost.spawn(p);
        ghost.update(.02f, {Aabb{}}); // not consulted when collision is disabled
        require(ghost.particles()[0].position.y < 0, "collision can be removed per system");
        ghost.step.kill_below = -2;
        ghost.update(.02f); require(ghost.alive() == 0, "world cutoff removes lost particles");

        vfx::System trail(64);
        vfx::Emitter emitter; emitter.rate = 30;
        for (int i = 0; i < 120; ++i) emitter.emit(trail, {}, 1.f / 120);
        require(trail.alive() == 30, "emission rate is independent of update frequency");
        vfx::System small(1); emitter.rate = 1000; emitter.emit(small, {}, 1);
        small.clear(); emitter.rate = 0;
        require(emitter.emit(small, {}, 0) == 0, "overflow never accumulates a delayed burst");
        invalid([&] { emitter.emit(small, {}, 2); });
        p.life = .2f; p.shrink_time = .4f; p.size = .12f; p.end_size = .02f; p.size_steps = 4;
        require(std::fabs(vfx::particle_size(p) - .07f) < .00001f, "quantized size curve");

        Runtime runtime{Config{}};
        fx.clear(); burst.count = 2; fx.burst({0, 2, 0}, burst);
        {
            vfx::Visuals visual(runtime, 4);
            require(runtime.scene.objects.empty(), "empty adapter creates no scene object");
            visual.sync(fx);
            ObjectRef owned = visual.object_ref();
            auto &mesh = runtime.object(owned).mesh;
            auto triangles = mesh.triangles.data();
            auto capacity = mesh.triangles.capacity();
            require(mesh.triangles.size() == 24 && runtime.scene.objects.size() == 1, "one batch instead of two objects");
            fx.update(.01f); visual.sync(fx);
            require(runtime.object(owned).mesh.triangles.data() == triangles, "sync reuses mesh storage");
            runtime.scene.objects.reserve(100); visual.sync(fx); // Runtime vector may relocate.
            fx.clear(); visual.sync(fx);
            require(runtime.object(owned).mesh.triangles.empty() &&
                    runtime.object(owned).mesh.triangles.capacity() == capacity, "idle batch retains storage");
            runtime.destroy_object(owned);
            auto foreign = runtime.add_object(Object{box({1,1,1}, {1,0,0})});
            fx.burst({}, burst); visual.sync(fx);
            require(runtime.object_alive(foreign) && !(visual.object_ref() == foreign), "reused slot is never claimed by stale ID");
        }
        require(runtime.scene.objects[0].active, "destructor leaves foreign object intact");
        runtime.clear_scene();
        {
            vfx::Visuals billboard(runtime, 1, vfx::Shape::Billboard);
            runtime.camera.position = {0, 3, 0}; runtime.camera.target = {};
            billboard.sync(fx);
            auto ref = billboard.object_ref();
            require(runtime.object(ref).mesh.triangles.size() == 4, "double-sided billboard costs four triangles");
            SceneRenderer prepare;
            prepare.prepare(runtime.scene, runtime.camera, runtime.config, runtime.frame);
            auto cpu = make_backend("cpu", 1); cpu->draw(runtime.frame);
            require(!cpu->readback().empty(), "VFX uses actual Ember renderer");
            if (const char *name = std::getenv("EMBER_TEST_BACKEND")) {
                // Use larger mixed primitives so this tests visible coverage,
                // not just two identically empty background images.
                fx.clear(); burst.particle.size = .7f; burst.count = 3;
                fx.burst({0, 1, 0}, burst);
                runtime.camera.position = {3, 2, -5}; runtime.camera.target = {0, 1, 0};
                vfx::Visuals cubes(runtime, 4);
                cubes.sync(fx); billboard.sync(fx);
                vfx::Visuals custom(runtime, mesh::quad({-.5f,-.5f,0}, {.5f,-.5f,0},
                    {.5f,.5f,0}, {-.5f,.5f,0}, {.4f,.7f,1}), 4);
                custom.sync(fx);
                auto gpu = make_backend(name, 1);
                for (auto profile : {Profile::Normal, Profile::PS1, Profile::N64, Profile::Pixel}) {
                    runtime.config.profile = profile;
                    prepare.prepare(runtime.scene, runtime.camera, runtime.config, runtime.frame);
                    cpu->draw(runtime.frame); gpu->draw(runtime.frame);
                    const auto &a = cpu->readback(), &b = gpu->readback();
                    require(a.size() == b.size(), "VFX backend dimensions agree");
                    double error = 0;
                    for (size_t i = 0; i < a.size(); ++i)
                        for (int c = 0; c < 3; ++c)
                            error += std::abs(int((a[i] >> (8*c)) & 255) - int((b[i] >> (8*c)) & 255));
                    error /= a.size() * 3;
                    require(error < 1, "VFX GPU/CPU image tolerance below 1/255 mean");
                    std::cout << name << " VFX " << profile_name(profile) << " MAE=" << error << '\n';
                }
            }
            runtime.clear_scene(); billboard.sync(fx);
            require(runtime.object_alive(billboard.object_ref()), "scene reset safely recreates batch");
        }
        {
            vfx::TimedMeshes meshes(runtime, 1);
            auto ref = meshes.spawn(mesh::disc(.1f, 1, 0, 3, 8, {1,1,1}, {0,0,1}), {}, .1f);
            require(runtime.object_alive(ref) && meshes.alive() == 1, "timed slash spawns");
            require(meshes.spawn(Mesh{}, {}, 1).id == 0, "timed capacity is bounded");
            meshes.update(.05f); require(runtime.object_alive(ref), "slash remains until expiry");
            meshes.update(.06f); require(!runtime.object_alive(ref) && meshes.alive() == 0, "slash expires");
            ref = meshes.spawn(Mesh{}, {}, 1);
            runtime.destroy_object(ref);
            auto foreign = runtime.add_object(Object{});
            meshes.update(2);
            require(runtime.object_alive(foreign), "timed stale ref never destroys replacement");
            meshes.spawn(Mesh{}, {}, 1);
            invalid([&] { meshes.update(-1); });
        }
        auto ephemeral = std::make_unique<Runtime>(Config{});
        auto visual = std::make_unique<vfx::Visuals>(*ephemeral, 4);
        auto timed = std::make_unique<vfx::TimedMeshes>(*ephemeral);
        visual->sync(fx); timed->spawn(Mesh{}, {}, 1);
        ephemeral.reset();
        invalid([&] { visual->sync(fx); });
        invalid([&] { timed->update(.1f); });
        visual.reset(); timed.reset(); // no dangling dereference on destruction
        std::cout << "VFX capacity, physics, emission, rendering and lifetime tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
