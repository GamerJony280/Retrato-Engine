#include "ember/asset.hpp"
#include "ember/collision.hpp"
#include "ember/runtime.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#ifdef EMBER_VIEWER
#include <SDL.h>
#endif
#ifdef EMBER_RMLUI
#include "ui.hpp"
#endif
using namespace ember;
int main(int argc, char **argv) {
#ifdef EMBER_VIEWER
    SDL_SetMainReady();
#endif
    try {
        std::string config = "config/engine.toml", backend, profile, filter, output, script, model, font,
                    ui_file, clip;
        int frames = 1, workers = -2;
        bool window = false, ui_snapshot = false, show_skeleton = false;
        double initial_time = 0;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing value: " + arg);
                return std::string(argv[++i]);
            };
            if (arg == "--config")
                config = value();
            else if (arg == "--backend")
                backend = value();
            else if (arg == "--profile")
                profile = value();
            else if (arg == "--filter")
                filter = value();
            else if (arg == "--out")
                output = value();
            else if (arg == "--lua")
                script = value();
            else if (arg == "--model")
                model = value();
            else if (arg == "--clip")
                clip = value();
            else if (arg == "--skeleton")
                show_skeleton = true;
            else if (arg == "--frames")
                frames = std::stoi(value());
            else if (arg == "--workers")
                workers = std::stoi(value());
            else if (arg == "--time")
                initial_time = std::stod(value());
            else if (arg == "--font")
                font = value();
            else if (arg == "--ui")
                ui_file = value();
            else if (arg == "--ui-snapshot")
                ui_snapshot = true;
            else if (arg == "--window")
                window = true;
            else if (arg == "--help") {
                std::cout << "Retrato 0.6.1\n--config path --backend cpu|opengl|vulkan|auto --profile "
                             "normal|ps1|n64|pixel\n--filter normal|crt --frames N --time seconds --out "
                             "frame.ppm\n--window --lua game.lua --model model.obj|fbx|gltf|glb|embasset "
                             "--clip name --skeleton --workers N\n--ui "
                             "assets/menu.rml --font path.ttf --ui-snapshot\nMultiple frames export as "
                             "name-00000.ppm etc. UI snapshot exports at 960x540.\n";
                return 0;
            } else
                throw std::runtime_error("Unknown argument: " + arg);
        }
        if (frames < 1 || frames > 100000 || workers < -2 || workers > 64 || !std::isfinite(initial_time) ||
            initial_time < 0)
            throw std::runtime_error("Invalid frame/time/worker count");
        Config cfg = Config::load(config);
        if (!backend.empty())
            cfg.backend = backend;
        if (!profile.empty())
            cfg.profile = parse_profile(profile);
        if (!filter.empty()) {
            if (filter != "normal" && filter != "crt")
                throw std::runtime_error("Unknown filter");
            cfg.filter = filter;
        }
        if (workers != -2)
            cfg.worker_threads = workers;
        Runtime runtime(cfg);
        runtime.scene = demo_scene();
        size_t model_id = 0;
        if (!model.empty()) {
            auto asset = import_asset(model);
            runtime.scene.objects.clear();
            runtime.scene.lights.clear();
            runtime.scene.objects.push_back(instantiate(asset));
            model_id = 0;
            auto &o = runtime.scene.objects[0];
            for (auto &c : asset->clips)
                std::cout << "clip: " << c.name << " duration=" << c.duration << "\n";
            for (auto &w : asset->warnings)
                std::cerr << "Warning: " << w << "\n";
            if (!clip.empty())
                o.animator->play(clip);
            else if (!asset->clips.empty())
                o.animator->play(asset->clips.front().name);
            o.animator->seek(float(initial_time));
            o.animator->mesh(o.mesh);
            Aabb bounds;
            for (auto &t : o.mesh.triangles)
                for (auto &v : t.v)
                    bounds.add(v.position);
            if (o.mesh.triangles.empty())
                throw std::runtime_error("Model has no renderable triangles");
            Vec3 center = (bounds.lo + bounds.hi) * .5f;
            float radius = std::max(.1f, length(bounds.hi - bounds.lo) * .5f);
            runtime.config.shadow_distance = std::clamp(int(std::ceil(radius * 8)), 4, 128);
            runtime.camera.target = center;
            runtime.camera.position = center + Vec3{1, .55f, -1.6f} * radius * 1.7f;
            runtime.camera.near_plane = std::max(.001f, radius * .01f);
            runtime.camera.far_plane = radius * 20 + 10;
            Mesh floor;
            constexpr int divisions = 10;
            float span = radius * 6, cell = span / divisions;
            for (int z = 0; z < divisions; z++)
                for (int x = 0; x < divisions; x++) {
                    Vec3 p0{-span * .5f + x * cell, 0, -span * .5f + z * cell};
                    Vec3 p1 = p0 + Vec3{0, 0, cell}, p2 = p0 + Vec3{cell, 0, cell},
                         p3 = p0 + Vec3{cell, 0, 0};
                    for (auto corners : {std::array<Vec3, 3>{p0, p1, p2}, std::array<Vec3, 3>{p0, p2, p3}}) {
                        Triangle t;
                        t.color = {.55f, .58f, .62f};
                        for (int k = 0; k < 3; k++)
                            t.v[k].position = corners[k];
                        floor.triangles.push_back(t);
                    }
                }
            runtime.scene.objects.push_back({std::move(floor), {center.x, bounds.lo.y - .03f, center.z}});
        }
        std::unique_ptr<Script> lua;
        if (!script.empty()) {
            lua = std::make_unique<Script>(runtime, script);
            runtime.on_event = [&](Runtime &, const Event &e) { lua->event(e); };
        } else
            runtime.on_event = [&](Runtime &r, const Event &e) {
                if (e.type == Event::Type::FixedUpdate && model.empty())
                    demo_animate(r.scene, e.time + initial_time);
            };
        runtime.start();
        if (!lua && model.empty())
            demo_animate(runtime.scene, initial_time);
        auto renderer = make_backend(runtime.config.backend, runtime.config.worker_threads);
        SceneRenderer prepare;
        std::cout << "backend=" << renderer->name() << " device=" << renderer->device_description()
                  << " profile=" << profile_name(cfg.profile) << " resolution=" << cfg.width() << "x"
                  << cfg.height() << "\n";
        if (cfg.physics_backend == "none")
            std::cerr << "Advanced physics disabled; arcade collision queries remain available.\n";
        bool paused = ui_snapshot;
        int viewport_w = 960, viewport_h = 540;
#ifdef EMBER_RMLUI
        std::unique_ptr<DemoUi> ui;
        if (!ui_file.empty()) {
            if (font.empty())
                throw std::runtime_error("Supply --font for RmlUi");
            ui = std::make_unique<DemoUi>(runtime.config, ui_file.c_str(), font.c_str(), viewport_w,
                                          viewport_h, paused);
        }
#else
        if (!ui_file.empty())
            throw std::runtime_error("RmlUi was not compiled");
#endif
        std::vector<double> timings;
        std::vector<uint32_t> composite;
        auto render = [&]() {
            auto start = std::chrono::steady_clock::now();
            size_t original_objects = runtime.scene.objects.size();
            if (show_skeleton && !model.empty()) {
                auto &anim = *runtime.scene.objects[model_id].animator;
                auto transforms = anim.transforms();
                for (size_t i = 0; i < transforms.size(); i++) {
                    int p = anim.asset().nodes[i].parent;
                    if (p < 0)
                        continue;
                    Vec3 a = transforms[p].point({}), b = transforms[i].point({});
                    // Small markers sample bone segments, requiring no separate line pipeline.
                    for (int k = 0; k <= 8; k++)
                        runtime.scene.objects.push_back(
                            {box({.025f, .025f, .025f}, {1, .1f, .65f}), mix(a, b, k / 8.f)});
                }
            }
            prepare.prepare(runtime.scene, runtime.camera, runtime.config, runtime.frame);
            runtime.scene.objects.resize(original_objects);
            renderer->draw(runtime.frame);
            const auto &pixels = renderer->readback();
            timings.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            return &pixels;
        };
        auto compose = [&](const std::vector<uint32_t> &source) {
            composite.resize(size_t(viewport_w) * viewport_h);
            int w = runtime.frame.width, h = runtime.frame.height;
            float scale = std::min(float(viewport_w) / w, float(viewport_h) / h);
            if (runtime.config.pixel_upscale && scale >= 1)
                scale = std::floor(scale);
            int dw = std::max(1, int(w * scale)), dh = std::max(1, int(h * scale)),
                ox = (viewport_w - dw) / 2, oy = (viewport_h - dh) / 2;
            std::fill(composite.begin(), composite.end(), 0xff100c09u);
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++)
                    composite[size_t(y + oy) * viewport_w + x + ox] =
                        source[size_t(std::min(h - 1, int(y / scale))) * w + std::min(w - 1, int(x / scale))];
#ifdef EMBER_RMLUI
            if (ui) {
                const auto &overlay = ui->pixels();
                for (size_t i = 0; i < overlay.size(); i++) {
                    uint32_t p = overlay[i], a = p >> 24;
                    if (!a)
                        continue;
                    uint32_t dest = composite[i], out = 0xff000000u;
                    for (int k = 0; k < 3; k++) {
                        uint32_t v =
                            ((p >> (8 * k)) & 255) + (((dest >> (8 * k)) & 255) * (255 - a) + 127) / 255;
                        out |= std::min(255u, v) << (8 * k);
                    }
                    composite[i] = out;
                }
            }
#endif
        };
        if (window) {
#ifdef EMBER_VIEWER
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                throw std::runtime_error(SDL_GetError());
            std::vector<SDL_GameController *> controllers;
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
                std::cerr << "warning: gamepad input unavailable: " << SDL_GetError() << "\n";
            else
                for (int i = 0; i < SDL_NumJoysticks(); ++i)
                    if (SDL_IsGameController(i))
                        if (auto *controller = SDL_GameControllerOpen(i))
                            controllers.push_back(controller);
            SDL_Window *win =
                SDL_CreateWindow("EMBER / reference viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 viewport_w, viewport_h, SDL_WINDOW_RESIZABLE);
            if (!win)
                throw std::runtime_error(SDL_GetError());
            std::cout
                << "Preview: CPU composition + explicit GPU readback. Not production GPU presentation.\n";
            bool running = true, dirty = true;
            auto previous = std::chrono::steady_clock::now();
            SDL_Event e{};
            while (running) {
                auto frame_start = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(frame_start - previous).count();
                previous = frame_start;
                if (paused && !dirty)
                    SDL_WaitEventTimeout(nullptr, 100);
                while (SDL_PollEvent(&e)) {
#ifdef EMBER_RMLUI
                    if (ui)
                        ui->event(e);
#endif
                    dirty = true;
                    if (e.type == SDL_QUIT)
                        running = false;
                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        viewport_w = std::max(1, e.window.data1);
                        viewport_h = std::max(1, e.window.data2);
#ifdef EMBER_RMLUI
                        if (ui)
                            ui->resize(viewport_w, viewport_h);
#endif
                    }
                    if (e.type == SDL_KEYDOWN) {
                        int key = e.key.keysym.sym;
                        if (key == SDLK_ESCAPE)
                            running = false;
                        if (key == SDLK_SPACE)
                            paused = !paused;
                        if (key == SDLK_1)
                            runtime.config.profile = Profile::PS1;
                        if (key == SDLK_2)
                            runtime.config.profile = Profile::N64;
                        if (key == SDLK_3)
                            runtime.config.profile = Profile::Pixel;
                        if (key == SDLK_4)
                            runtime.config.profile = Profile::Normal;
                        if (key == SDLK_b)
                            show_skeleton = !show_skeleton;
                        if (key == SDLK_TAB && !model.empty()) {
                            auto &a = *runtime.scene.objects[model_id].animator;
                            auto &clips = a.asset().clips;
                            if (!clips.empty()) {
                                int next = (a.clip_index() + 1) % int(clips.size());
                                a.play(clips[next].name, true, .2f);
                                a.mesh(runtime.scene.objects[model_id].mesh);
                                std::cout << "clip: " << clips[next].name << "\n";
                            }
                        }
                        if (key == SDLK_c)
                            runtime.config.filter = runtime.config.filter == "crt" ? "normal" : "crt";
                        if (key == SDLK_LEFT || key == SDLK_RIGHT)
                            runtime.camera.position =
                                rotate_y(runtime.camera.position, key == SDLK_LEFT ? -.1f : .1f);
                        runtime.dispatch({Event::Type::KeyDown, runtime.time(), key});
#ifdef EMBER_RMLUI
                        if (ui)
                            ui->invalidate();
#endif
                    }
                    if (e.type == SDL_KEYUP)
                        runtime.dispatch({Event::Type::KeyUp, runtime.time(), e.key.keysym.sym});
                    if (e.type == SDL_MOUSEMOTION)
                        runtime.dispatch({Event::Type::MouseMove, runtime.time(), 0, 0, float(e.motion.x),
                                          float(e.motion.y), float(e.motion.xrel), float(e.motion.yrel)});
                    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
                        runtime.dispatch({e.type == SDL_MOUSEBUTTONDOWN ? Event::Type::MouseDown
                                                                      : Event::Type::MouseUp,
                                          runtime.time(), int(e.button.button), 0, float(e.button.x),
                                          float(e.button.y)});
                    if (e.type == SDL_MOUSEWHEEL)
                        runtime.dispatch({Event::Type::MouseWheel, runtime.time(), 0, 0, float(e.wheel.x),
                                          float(e.wheel.y)});
                    if (e.type == SDL_CONTROLLERDEVICEADDED && SDL_IsGameController(e.cdevice.which))
                        if (auto *controller = SDL_GameControllerOpen(e.cdevice.which))
                            controllers.push_back(controller);
                    if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                        auto it = std::find_if(controllers.begin(), controllers.end(), [&](auto *controller) {
                            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) ==
                                   e.cdevice.which;
                        });
                        if (it != controllers.end()) {
                            SDL_GameControllerClose(*it);
                            controllers.erase(it);
                        }
                    }
                    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP)
                        runtime.dispatch({e.type == SDL_CONTROLLERBUTTONDOWN ? Event::Type::GamepadButtonDown
                                                                            : Event::Type::GamepadButtonUp,
                                          runtime.time(), int(e.cbutton.button), int(e.cbutton.which)});
                    if (e.type == SDL_CONTROLLERAXISMOTION) {
                        const float value = e.caxis.value < 0 ? e.caxis.value / 32768.f : e.caxis.value / 32767.f;
                        runtime.dispatch({Event::Type::GamepadAxis, runtime.time(), int(e.caxis.axis),
                                          int(e.caxis.which), value});
                    }
                }
                if (!paused) {
                    runtime.tick(dt);
                    dirty = true;
                }
                if (dirty && running) {
                    compose(*render());
                    SDL_Surface *surface = SDL_GetWindowSurface(win);
                    if (surface) {
                        SDL_Surface *image =
                            SDL_CreateRGBSurfaceWithFormatFrom(composite.data(), viewport_w, viewport_h, 32,
                                                               viewport_w * 4, SDL_PIXELFORMAT_RGBA32);
                        SDL_BlitSurface(image, nullptr, surface, nullptr);
                        SDL_FreeSurface(image);
                        SDL_UpdateWindowSurface(win);
                    }
                    dirty = false;
                }
                double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start)
                        .count();
                if (!paused && ms < 33.333)
                    SDL_Delay(Uint32(33.333 - ms));
            }
            for (auto *controller : controllers)
                SDL_GameControllerClose(controller);
            SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
            SDL_DestroyWindow(win);
#else
            throw std::runtime_error("Viewer was not compiled");
#endif
        } else {
            for (int i = 0; i < frames; i++) {
                if (i && !paused)
                    runtime.tick(1.0 / runtime.config.fixed_hz);
                const auto *pixels = render();
                int w = runtime.frame.width, h = runtime.frame.height;
                if (ui_snapshot) {
                    compose(*pixels);
                    pixels = &composite;
                    w = viewport_w;
                    h = viewport_h;
                }
                if (!output.empty()) {
                    std::string path = output;
                    if (frames > 1) {
                        std::ostringstream s;
                        s << output << '-' << std::setw(5) << std::setfill('0') << i << ".ppm";
                        path = s.str();
                    }
                    write_ppm(path, w, h, *pixels);
                }
            }
        }
        if (!timings.empty()) {
            auto sorted = timings;
            std::sort(sorted.begin(), sorted.end());
            double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
            std::cout << "frames=" << timings.size() << " triangles=" << runtime.frame.vertices.size() / 3
                      << " frame_mean_ms=" << mean
                      << " frame_p95_ms=" << sorted[std::min(sorted.size() - 1, size_t(sorted.size() * .95))]
                      << " (includes GPU readback; excludes simulation/animation/UI/export)\n";
        }
#ifdef EMBER_RMLUI
        if (ui)
            std::cout << "ui_redraws=" << ui->redraws() << "\n";
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
