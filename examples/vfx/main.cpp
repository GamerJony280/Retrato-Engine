// Minimal C++ game-side recipe, no SDL, Lua, Nova or asset import required.
#include "ember/vfx.hpp"
#include "ember/mesh_builder.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        ember::Config cfg;
        cfg.backend = "cpu";
        cfg.worker_threads = 1;
        cfg.resolution_quality = 50;
        ember::Runtime runtime(cfg);
        runtime.camera.position = {4, 3, -6};
        runtime.camera.target = {0, .8f, 0};
        runtime.add_object(ember::Object{ember::box({8, .2f, 6}, {.25f, .3f, .4f}), {0, -.1f, 0}});
        runtime.add_object(ember::Object{ember::box({.7f, .7f, .7f}, {.9f, .4f, .15f}), {0, .35f, 0}});
        ember::Aabb floor;
        floor.add({-4, -.2f, -3}); floor.add({4, 0, 3});

        ember::vfx::System particles(96, 7);
        particles.step.collisions = true;
        particles.step.settle_life = .45f;
        particles.step.settle_speed = 1;
        ember::vfx::Visuals visual(runtime, 96);
        ember::vfx::Burst hit;
        hit.count = 32;
        hit.pattern = ember::vfx::Pattern::Box3D;
        hit.particle.color = {1, .65f, .1f};
        hit.particle.acceleration = {0, -20, 0};
        hit.velocity_min = {-4, 1, -3}; hit.velocity_max = {4, 6, 3};
        hit.life_min = 1; hit.life_max = 1.4f;
        particles.burst({0, .8f, 0}, hit);

        ember::vfx::TimedMeshes effects(runtime, 2);
        effects.spawn(ember::mesh::disc(.5f, 1.1f, -.6f, 2, 12, {.9f,.97f,1}, {.2f,.5f,1}),
                      {.5f, .8f, -.1f}, .4f);
        std::vector<ember::Aabb> solids{floor};
        for (int i = 0; i < 12; ++i) {
            particles.update(1.f / 60, solids);
            effects.update(1.f / 60);
        }
        visual.sync(particles); // once at render time, after simulation steps
        ember::SceneRenderer renderer;
        renderer.prepare(runtime.scene, runtime.camera, cfg, runtime.frame);
        auto backend = ember::make_backend("cpu", 1);
        backend->draw(runtime.frame);
        const std::string output = argc > 1 ? argv[1] : "vfx.ppm";
        ember::write_ppm(output, runtime.frame.width, runtime.frame.height, backend->readback());
        std::cout << "particles=" << particles.alive() << " output=" << output << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
