#include "ember/c_api.h"
#include "ember/collision.hpp"
#include "ember/game.hpp"
#include "ember/runtime.hpp"
#include "ember/save.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace ember;
static void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "test source directory required");
        std::string root = argv[1];
        Config config = Config::load(root + "/config/engine.toml");
        {
            Scene empty;
            SceneRenderer renderer;
            Frame frame;
            for (auto profile : {Profile::Normal, Profile::Pixel, Profile::PS1, Profile::N64}) {
                for (bool crt : {false, true}) {
                    Config background = config;
                    background.profile = profile;
                    background.filter = crt ? "crt" : "normal";
                    renderer.prepare(empty, Camera{}, background, frame);
                    // Exercise partial tiles and worker row boundaries.
                    frame.width = 17;
                    frame.height = frame.settings.height = 9;
                    for (int workers : {0, 3, 7}) {
                        auto cpu = make_cpu(workers);
                        cpu->draw(frame);
                        for (int y = 0; y < frame.height; ++y)
                            for (int x = 0; x < frame.width; ++x)
                                require(cpu->readback()[size_t(y) * frame.width + x] ==
                                            finish_pixel({.09f,.14f,.21f}, x, y, profile, crt),
                                        "background fast path preserves profile/filter pixel semantics");
                    }
                }
            }
            // A superficially identical but alpha-discarded quad must NOT bypass shading.
            for (auto &vertex : frame.vertices) {
                vertex.r = 1;
                vertex.opacity = 0;
                vertex.alpha_cutoff = .5f;
            }
            auto cpu = make_cpu(0);
            cpu->draw(frame);
            require(cpu->readback()[0] == finish_pixel({.09f,.14f,.21f}, 0, 0, frame.profile, frame.crt),
                    "modified background packet takes ordinary material path");
        }
        require(config.width() % 16 == 0, "aligned width");
        require(config.height() * 16 == config.width() * 9, "aspect ratio");
        require(config.physics_backend == "none" && config.physics_settings().plane == PhysicsPlane::Full3D,
                "advanced physics is explicitly opt-in");
        {
            Config bad = config;
            bad.resolution_quality = std::numeric_limits<int>::max();
            bool rejected_direct = false;
            try { bad.width(); } catch (const std::exception &) { rejected_direct = true; }
            require(rejected_direct, "direct resolution rejects overflow before arithmetic");
            bad = config;
            bad.toon_bands = 1;
            rejected_direct = false;
            try { Runtime invalid(bad); } catch (const std::exception &) { rejected_direct = true; }
            require(rejected_direct, "direct runtime configuration rejects toon division by zero");
        }
        {
            game::Host editor(config);
            auto floor = editor.create_box("floor", {8, .25f, 8}, {.3f, .3f, .4f});
            editor.set_position(floor, {0, -1, 0});
            require(editor.position(floor).y == -1 && !editor.has_rigid_body(floor),
                    "visual floor works without Jolt");
            SceneRenderer renderer;
            Frame frame;
            auto cpu = make_cpu(1);
            editor.set_profile(Profile::PS1);
            editor.render(renderer, *cpu, frame);
            require(frame.profile == Profile::PS1 && editor.profile() == Profile::PS1,
                    "editor profile changes the rendered frame");
            editor.set_profile(Profile::Normal);
            editor.render(renderer, *cpu, frame);
            require(frame.profile == Profile::Normal, "profile change is reversible");
            bool bad_box = false;
            try { editor.create_box("invalid", {0, 1, 1}, {1, 1, 1}); }
            catch (const std::exception &) { bad_box = true; }
            require(bad_box, "invalid editor box dimensions rejected");
        }
        require(parse_physics_plane("xy") == PhysicsPlane::XY &&
                    std::string(physics_plane_name(PhysicsPlane::XZ)) == "xz",
                "2.5D plane parsing");
        require(BodyShape::box({1, 2, 3}).half_extents.y == 2 && BodyShape::sphere(.5f).radius == .5f,
                "rigid body primitive descriptions");
        {
            std::ofstream f("invalid.toml");
            f << "[graphics]\nresolution_quality=0\n";
        }
        bool rejected = false;
        try {
            Config::load("invalid.toml");
        } catch (...) {
            rejected = true;
        }
        require(rejected, "invalid range rejected");
        SaveData save;
        save.set("health", int64_t(100));
        save.set("speed", 3.5);
        save.set("alive", true);
        save.set("name", std::string("player"));
        save.set("spawn", Vec3{1, 2, 3});
        save.write("test.embsave");
        auto loaded_save = SaveData::read("test.embsave");
        require(loaded_save.integer("health") == 100 && loaded_save.number("speed") == 3.5 &&
                    loaded_save.boolean("alive") && loaded_save.string("name") == "player" &&
                    loaded_save.vec3("spawn").z == 3,
                "versioned save roundtrip");
        rejected = false;
        try {
            loaded_save.number("health");
        } catch (...) {
            rejected = true;
        }
        require(rejected, "save type mismatch rejected");
        {
            std::ofstream bad_save("bad.embsave", std::ios::binary);
            bad_save << "EMBSAVE";
        }
        rejected = false;
        try {
            SaveData::read("bad.embsave");
        } catch (...) {
            rejected = true;
        }
        require(rejected, "truncated save rejected");
        auto cube = box({2, 2, 2}, {1, 1, 1});
        CollisionMesh mesh;
        mesh.cook(cube);
        Hit hit;
        require(mesh.raycast({0, 0, -4}, {0, 0, 1}, 10, hit), "ray hits cube");
        require(std::abs(hit.distance - 3) < 1e-5, "ray distance");
        require(!mesh.raycast({4, 0, -4}, {0, 0, 1}, 10, hit), "ray misses");
        mesh.save("test.embcol");
        auto restored = CollisionMesh::load("test.embcol");
        require(restored.raycast({0, 0, -4}, {0, 0, 1}, 10, hit) && std::abs(hit.distance - 3) < 1e-5,
                "cooked roundtrip");
        float t;
        Vec3 normal;
        require(sweep_sphere_box({-10, 0, 0}, {20, 0, 0}, .5f, {{-1, -1, -1}, {1, 1, 1}}, t, normal),
                "continuous sweep catches thin obstacle");
        require(std::abs(t - .425f) < 1e-6f && normal.x == -1, "sweep time and normal");
        auto slide = slide_sphere_boxes({-3, 0, 0}, {4, 0, 2}, .5f, {{{-1, -10, -10}, {1, 10, 10}}});
        require(slide.contacts == 1 && std::abs(slide.position.x + 1.5001f) < 1e-4f &&
                    std::abs(slide.position.z - 2) < 1e-4f,
                "arcade sphere response slides along wall");
        {
            Runtime disabled(config);
            auto object = disabled.add_object({box({1, 1, 1}, {1, 1, 1}), {0, 0, 0}});
            rejected = false;
            try {
                disabled.add_rigid_body(object, {});
            } catch (...) {
                rejected = true;
            }
            require(rejected, "disabled advanced physics rejects body creation clearly");
            const std::function<void()> queries[] = {
                [&] { disabled.rigid_body_state(object); },
                [&] { disabled.rigid_body_velocity(object); },
                [&] { disabled.set_rigid_body_velocity(object, {}); },
                [&] { disabled.add_rigid_body_force(object, {}); },
                [&] { disabled.add_rigid_body_impulse(object, {}); }
            };
            for (const auto &query : queries) {
                rejected = false;
                try { query(); } catch (const std::exception &) { rejected = true; }
                require(rejected, "disabled physics access throws before dereferencing the backend");
            }
        }
        if (jolt_physics_available()) {
            Config physics_config = config;
            physics_config.physics_backend = "jolt";
            physics_config.physics_plane = "xy";
            physics_config.worker_threads = 0;
            Runtime physics(physics_config);
            auto floor = physics.add_object({box({10, 1, 2}, {.4f, .4f, .4f}), {0, -.5f, 0}});
            BodyDesc floor_body;
            floor_body.shape = BodyShape::box({5, .5f, 1});
            physics.add_rigid_body(floor, floor_body);
            auto ball = physics.add_object({box({1, 1, 1}, {1, .3f, .2f}), {0, 3, 0}});
            BodyDesc ball_body;
            ball_body.shape = BodyShape::sphere(.5f);
            ball_body.motion = BodyMotion::Dynamic;
            ball_body.allow_rotation = false;
            ball_body.continuous = true;
            physics.add_rigid_body(ball, ball_body);
            physics.add_rigid_body_impulse(ball, {0, 0, 10});
            for (int i = 0; i < 180; ++i)
                physics.tick(1.0 / 60.0);
            require(std::abs(physics.object(ball).position.z) < 1e-4f &&
                        physics.object(ball).position.y > .45f && physics.object(ball).position.y < .65f,
                    "Jolt response stacks on a floor and XY mode locks depth");
            physics.destroy_object(ball);
            require(!physics.object_alive(ball), "rigid body owner destruction is safe");
        }
        Runtime runtime(config);
        int starts = 0, updates = 0, cues = 0;
        runtime.on_event = [&](Runtime &, const Event &e) {
            if (e.type == Event::Type::Start)
                starts++;
            if (e.type == Event::Type::FixedUpdate)
                updates++;
            if (e.type == Event::Type::Cue)
                cues++;
        };
        runtime.add_cue(.5, 1);
        runtime.start();
        for (int i = 0; i < 60; i++)
            runtime.tick(1.0 / 60);
        require(starts == 1 && updates == 60 && cues == 1, "fixed loop and cue exactly once");
        Runtime failing(config);
        int failed_cues = 0;
        failing.add_cue(1.0 / 60.0, 7);
        failing.on_event = [&](Runtime &, const Event &e) {
            if (e.type == Event::Type::Cue)
                ++failed_cues;
            if (e.type == Event::Type::FixedUpdate)
                throw std::runtime_error("callback failure");
        };
        rejected = false;
        try {
            failing.tick(1.0 / 60.0);
        } catch (...) {
            rejected = true;
        }
        require(rejected && failed_cues == 1 && std::abs(failing.time() - 1.0 / 60.0) < 1e-9,
                "failed callback leaves a complete fixed step");
        {
            Runtime recursive(config);
            recursive.on_event = [](Runtime &r, const Event &) { r.tick(1.0 / 60); };
            rejected = false;
            try { recursive.tick(1.0 / 60); } catch (const std::exception &) { rejected = true; }
            require(rejected && std::abs(recursive.time() - 1.0 / 60) < 1e-9,
                    "recursive runtime tick rejected with one complete step");
            recursive.on_event = {};
            recursive.tick(1.0 / 60);
            require(std::abs(recursive.time() - 2.0 / 60) < 1e-9, "tick guard resets after exception");
        }
        {
            Runtime lifetime(config);
            auto object = lifetime.add_object({box({1, 1, 1}, {1, 0, 0}), {0, 0, 0}});
            auto collider = lifetime.add_collider(object);
            auto persistent = lifetime.add_collider(object);
            lifetime.remove_collider(collider);
            auto reused = lifetime.add_collider(object);
            require(reused.index == collider.index && reused.id != collider.id,
                    "collider holes reused with fresh identity");
            require(lifetime.raycast(persistent, {0, 0, -4}, {0, 0, 1}, 10, hit),
                    "reusing a slot preserves neighbouring handles");
            lifetime.destroy_object(object);
            require(!lifetime.object_alive(object), "destroy expires object ref");
            rejected = false;
            try {
                lifetime.raycast(collider, {0, 0, -4}, {0, 0, 1}, 10, hit);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "destroy expires collider ref");
            auto replacement = lifetime.add_object({box({1, 1, 1}, {0, 1, 0}), {3, 0, 0}});
            require(replacement.index == object.index && replacement.id != object.id &&
                        !lifetime.object_alive(object),
                    "dead slot reuse preserves generational identity");
        }
        Scene scene = demo_scene();
        {
            Runtime cooked(config);
            const auto index = cooked.add_model(root + "/assets/robot.embasset");
            auto &robot = cooked.scene.objects[index];
            require(robot.animator && !robot.mesh.triangles.empty(), "cooked asset loads without importer dependency");
            robot.animator->play("Wave");
            robot.animator->seek(0.f);
            robot.animator->mesh(robot.mesh);
            std::vector<Vec3> before;
            for (const auto &triangle : robot.mesh.triangles)
                for (const auto &vertex : triangle.v)
                    before.push_back(vertex.position);
            robot.animator->seek(.5f);
            robot.animator->mesh(robot.mesh);
            float movement = 0.f;
            size_t vertex_index = 0;
            for (const auto &triangle : robot.mesh.triangles)
                for (const auto &vertex : triangle.v)
                    movement = std::max(movement, length(vertex.position - before.at(vertex_index++)));
            require(movement > .1f, "cooked skeleton moves vertices in minimal runtime");
        }
        Camera camera;
        SceneRenderer prepare;
        Frame frame;
        config.resolution_quality = 1;
        config.shadowed_lights = 0;
        prepare.prepare(scene, camera, config, frame);
        require(!frame.vertices.empty(), "visible geometry");
        {
            Scene culling_scene;
            culling_scene.objects.push_back({box({1, 1, 1}, {1, 1, 1}), {0, 1, 0}});
            Camera culling_camera;
            culling_camera.position = {0, 1, -5};
            culling_camera.target = {0, 1, 0};
            config.shadowed_lights = 0;
            prepare.prepare(culling_scene, culling_camera, config, frame);
            require(frame.vertices.size() > 6, "in-frustum geometry survives conservative reject");
            culling_scene.objects[0].position.x = 10000;
            prepare.prepare(culling_scene, culling_camera, config, frame);
            require(frame.vertices.size() == 6, "off-frustum geometry emits only background");
        }
        prepare.prepare(scene, camera, config, frame);
        auto serial = make_cpu(0), parallel = make_cpu(2);
        auto malformed = frame;
        malformed.vertices.pop_back();
        rejected = false;
        try {
            serial->draw(malformed);
        } catch (...) {
            rejected = true;
        }
        require(rejected, "malformed frame rejected before rasterization");
        for (int case_id = 0; case_id < 5; ++case_id) {
            malformed = frame;
            if (case_id == 0) malformed.settings.profile = -1;
            if (case_id == 1) malformed.settings.up[0] = std::numeric_limits<float>::quiet_NaN();
            if (case_id == 2) {
                malformed.vertices[0].texture_width = malformed.vertices[0].texture_height = 1;
                malformed.vertices[0].texture_offset = 1e30f;
            }
            if (case_id == 3) malformed.vertices[0].wrap = 1e30f;
            if (case_id == 4) malformed.vertices[0].u = 1e30f;
            rejected = false;
            try { serial->draw(malformed); } catch (const std::exception &) { rejected = true; }
            require(rejected, "invalid shader/material contract rejected before conversion");
        }
        {
            auto invalid_camera = camera;
            invalid_camera.far_plane = invalid_camera.near_plane;
            rejected = false;
            try { prepare.prepare(scene, invalid_camera, config, malformed); }
            catch (const std::exception &) { rejected = true; }
            require(rejected, "degenerate camera rejected before projection");
        }
        {
            Scene invalid_scene;
            invalid_scene.objects.push_back({box({1, 1, 1}, {1, 1, 1}), {0, 1, 0}});
            auto image = std::make_shared<ImageData>();
            image->width = image->height = 2;
            image->rgba = {0xffffffffu};
            auto material = std::make_shared<Material>();
            material->alpha_cutoff = .5f;
            auto &triangle = invalid_scene.objects[0].mesh.triangles[0];
            triangle.image = image;
            triangle.material = material;
            Config with_shadows = config;
            with_shadows.shadowed_lights = 1;
            rejected = false;
            try { prepare.prepare(invalid_scene, camera, with_shadows, malformed); }
            catch (const std::exception &) { rejected = true; }
            require(rejected, "short scene image rejected before alpha shadow raster reads it");
            image->rgba.resize(4, 0xffffffffu);
            triangle.v[0].uv.x = std::numeric_limits<float>::quiet_NaN();
            rejected = false;
            try { prepare.prepare(invalid_scene, camera, with_shadows, malformed); }
            catch (const std::exception &) { rejected = true; }
            require(rejected, "invalid scene UV rejected before texel conversion");
        }
        serial->draw(frame);
        parallel->draw(frame);
        require(serial->readback() == parallel->readback(), "parallel raster output matches serial");
        {
            Scene lit;
            lit.objects.push_back({box({1, 1, 1}, {1, 1, 1}), {0, 1, 0}});
            lit.sun = {};
            lit.lights = {{{100, 1, 0}, {1, 0, 0}, 1}, {{0, -1e6f, 0}, {}, 0},
                          {{0, 2, -2}, {0, 1, 0}, 6}};
            Config light_config = config;
            light_config.local_lights_per_object = 1;
            Camera light_camera;
            light_camera.position = {0, 1, -5};
            light_camera.target = {0, 1, 0};
            for (auto profile : {Profile::Normal, Profile::PS1}) {
                light_config.profile = profile;
                Frame light_frame;
                prepare.prepare(lit, light_camera, light_config, light_frame);
                serial->draw(light_frame);
                auto selected = serial->readback();
                const auto all_lights = lit.lights;
                lit.lights = {all_lights.back()};
                prepare.prepare(lit, light_camera, light_config, light_frame);
                serial->draw(light_frame);
                require(selected == serial->readback(), "local budget selects affecting lights even after PS1 sort");
                lit.lights.clear();
                prepare.prepare(lit, light_camera, light_config, light_frame);
                serial->draw(light_frame);
                require(selected != serial->readback(), "selected local light contributes to the image");
                lit.lights = all_lights;
            }
        }
        prepare.prepare(scene, camera, config, frame);
        serial->draw(frame);
        auto initial = serial->readback();
        demo_animate(scene, 1.0);
        prepare.prepare(scene, camera, config, frame);
        serial->draw(frame);
        require(initial != serial->readback(), "animated scene changes image");
        auto unshadowed = serial->readback();
        config.shadowed_lights = 1;
        prepare.prepare(scene, camera, config, frame);
        serial->draw(frame);
        require(unshadowed != serial->readback(), "directional shadows change image");
        if (const char *backend = std::getenv("EMBER_TEST_BACKEND")) {
            auto gpu = make_backend(backend, 0);
            for (auto profile : {Profile::Normal, Profile::PS1, Profile::N64, Profile::Pixel})
                for (bool crt : {false, true}) {
                    config.profile = profile;
                    config.filter = crt ? "crt" : "normal";
                    prepare.prepare(scene, camera, config, frame);
                    serial->draw(frame);
                    gpu->draw(frame);
                    const auto &expected = serial->readback();
                    const auto &actual = gpu->readback();
                    require(expected.size() == actual.size(), "GPU image size");
                    double error = 0;
                    for (size_t i = 0; i < actual.size(); i++)
                        for (int channel = 0; channel < 3; channel++)
                            error += std::abs(int((actual[i] >> (8 * channel)) & 255) -
                                              int((expected[i] >> (8 * channel)) & 255));
                    error /= actual.size() * 3;
                    std::cout << backend << " " << profile_name(profile) << " crt=" << crt << " MAE=" << error
                              << "\n";
                    require(error < 1.0, "GPU/CPU image tolerance (<1/255 mean)");
                }
        }
        for (auto p : {Profile::PS1, Profile::N64, Profile::Normal, Profile::Pixel}) {
            config.profile = p;
            camera.position = {0, 1, 0};
            camera.target = {0, 1, 1};
            prepare.prepare(scene, camera, config, frame);
            for (auto v : frame.vertices)
                require(std::isfinite(v.x) && std::isfinite(v.y) && v.z >= -.0001 && v.z <= 1.0001,
                        "near clip finite");
            serial->draw(frame);
        }
        auto *h = ember_create((root + "/config/engine.toml").c_str(), "cpu");
        require(h != nullptr, "C ABI create");
        require(ember_add_box(h, 0, 0, 0, 2, 2, 2) == 0, "C ABI box");
        require(ember_set_position(h, 77, 0, 0, 0) == -1, "C ABI invalid handle");
        require(ember_render(h) == 0, "C ABI draw");
        int w, hh;
        require(ember_pixels(h, &w, &hh) != nullptr && w > 0 && hh > 0, "C ABI pixels");
        ember_destroy(h);
        std::cout << "PASS: config, BVH, cooking, sweep, events, clipping, CPU threads, C ABI\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
