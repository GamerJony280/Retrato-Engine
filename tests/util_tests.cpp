// Unit tests for the header-only game utilities (character, input, loop,
// mesh_builder, particles, follow, lights). No window, no files.
#include "ember/character.hpp"
#include "ember/follow.hpp"
#include "ember/input.hpp"
#include "ember/lights.hpp"
#include "ember/loop.hpp"
#include "ember/mesh_builder.hpp"
#include "ember/particles.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace ember;
static void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::vector<Aabb> ground(float top = 0) {
    Aabb b;
    b.add({-10, top - 2, -10});
    b.add({10, top, 10});
    return {b};
}
int main() {
    try {
        // character: falls, lands, reports ground
        {
            CharacterBody c;
            c.position = {0, 2, 0};
            c.radius = 0.35f;
            bool landed = false;
            for (int i = 0; i < 120 && !landed; i++) {
                c.update(ground(), 1.0f / 60, 30, 11.5f, 0.1f);
                landed = c.grounded;
            }
            require(landed, "character lands");
            require(std::fabs(c.position.y - 0.35f) < 0.02f, "character rests on top");
            require(c.ground_normal.y > 0.9f, "ground normal up");
        }
        // character: jump buffer + coyote + cut + air jump
        {
            CharacterBody c;
            c.position = {0, 0.36f, 0};
            c.grounded = true;
            c.queue_jump(0.12f);
            require(c.update(ground(), 1.0f / 60, 30, 11.5f, 0.1f), "buffered jump fires");
            require(!c.grounded && c.velocity.y > 10.9f, "jump velocity");
            c.cut_jump(5);
            require(c.velocity.y == 5, "release cuts the rise");
            float before = c.velocity.y;
            c.air_jump(10);
            require(c.velocity.y == 10 && before != 10, "air jump sets velocity");
            // coyote: walk off, still jumpable for 0.1 s
            CharacterBody w;
            w.position = {0, 1.0f, 0};
            for (int i = 0; i < 120 && !w.grounded; i++)
                w.update(ground(), 1.0f / 60, 30, 11.5f, 0.1f);
            require(w.grounded, "coyote setup landed");
            w.grounded = false; // walked off an edge (coyote still armed)
            w.queue_jump(0.12f);
            require(w.update(ground(), 1.0f / 60, 30, 11.5f, 0.1f), "coyote jump fires");
        }
        // character: wall blocks horizontal, head bump stops rise
        {
            Aabb wall;
            wall.add({2, -2, -2});
            wall.add({3, 4, 2});
            CharacterBody c;
            c.position = {0, 0.35f, 0};
            CharacterStep s = c.move_collide({wall}, {5, 0, 0});
            require(s.hit_wall && c.position.x < 2, "wall stops");
            require(std::fabs(s.wall_normal.x) > 0.5f, "wall normal sideways");
            CharacterBody w2;
            w2.position = {0, 0.36f, 0};
            w2.velocity = {7, 0, 0};
            w2.update(ground(), 1.0f / 60, 30, 11.5f, 0.1f);
            require(w2.hit_wall == false, "open ground has no wall");
            w2.position = {1.5f, 0.36f, 0};
            w2.velocity = {30, 0, 0};
            w2.update({wall}, 1.0f / 60, 30, 11.5f, 0.1f);
            require(w2.hit_wall && std::fabs(w2.last_wall_normal.x) > 0.5f, "update keeps wall info");
            Aabb ceil;
            ceil.add({-2, 3, -2});
            ceil.add({2, 4, 2});
            CharacterBody h;
            h.position = {0, 2.0f, 0};
            h.velocity = {0, 10, 0};
            s = h.move_collide({ceil}, {0, 1.0f, 0});
            require(s.hit_head && h.velocity.y <= 0, "head bump stops rise");
            CharacterBody z;
            z.position = {0, 1, 0};
            z.move_collide({}, {0, 0, 2});
            require(std::fabs(z.position.z - 2) < 1e-5f, "character supports X/Z horizontal motion");
        }
        // input: edges live one frame
        {
            ActionMap in;
            in.bind(0, 10).bind(1, 11).bind(1, 12);
            in.key_down(10);
            require(in.held(0) && in.pressed(0) && !in.held(1), "press edge");
            in.next_frame();
            require(in.held(0) && !in.pressed(0), "edge expires, hold stays");
            in.key_down(12);
            require(in.held(1) && in.pressed(1), "second key same action");
            in.next_frame();
            in.key_down(11);
            in.key_down(11); // OS repeat must not create a new edge
            require(in.held(1) && !in.pressed(1), "repeat does not create another press edge");
            in.key_up(12);
            require(in.held(1) && !in.released(1), "one of two held keys does not release action");
            in.key_up(10);
            in.key_up(11);
            require(!in.held(0) && !in.held(1) && in.released(1), "release edge");
            in.next_frame();
            require(!in.released(1), "release expires");
            in.key_down(99); // unbound: no crash, no state
            require(!in.held(2), "unknown key ignored");
            require(!in.held(-1) && !in.pressed(-1) && !in.released(-1), "negative query is safe");
            bool bad_action = false;
            try {
                in.bind(-1, 1);
            } catch (const std::invalid_argument &) {
                bad_action = true;
            }
            require(bad_action, "negative bind rejected");
            in.key_down(10);
            in.bind(2, 10);
            require(!in.held(0) && in.held(2), "rebind reconciles a held key");
            in.unbind_key(10);
            require(!in.held(2) && in.released(2), "unbind releases the old action");
            in.key_down(12);
            in.next_frame();
            in.release_all();
            require(!in.held(1) && in.released(1), "focus loss releases held actions");
            in.next_frame();
            in.key_down(12);
            require(in.pressed(1), "new press after focus loss works");
            in.key_up(12);
            require(in.pressed(1) && in.released(1), "short tap retains both edges until consumed");
        }
        // loop: fixed steps, cadence, backlog drop, determinism
        {
            LoopTimer t;
            t.render_dt = 0.03; // every other tick: no float-edge comparisons
            auto a = t.update(1.0 / 60);
            require(a.steps == 1 && !a.render, "one step, no render yet");
            auto b = t.update(1.0 / 60);
            require(b.steps == 1 && b.render, "render on the second tick");
            LoopTimer slow;
            slow.render_dt = 1.0 / 30.0;
            auto c = slow.update(1.0); // huge hitch: capped, never spirals
            require(c.steps == 8, "steps capped at 8");
            LoopTimer x, y;
            for (int i = 0; i < 10; i++) {
                auto rx = x.update(0.017);
                auto ry = y.update(0.01 + 0.007);
                require(rx.steps == ry.steps && rx.render == ry.render, "split dt matches");
            }
            LoopTimer s;
            auto d = s.update(1.0 / 60);
            require(d.sleep_ms >= 0 && d.sleep_ms <= 1000.0 / 60, "sleep hint sane");
            bool invalid_timer = false;
            try {
                s.fixed_dt = 0;
                s.update(0.01);
            } catch (const std::invalid_argument &) {
                invalid_timer = true;
            }
            require(invalid_timer, "invalid timer rejected instead of producing NaN");
            LoopTimer rounding;
            auto near = rounding.update(rounding.fixed_dt - 5e-13);
            require(near.steps == 1 && rounding.acc >= 0, "rounding tolerance cannot leave negative time");
            require(rounding.update(0).steps == 0, "zero time cannot repeat the consumed step");
            LoopTimer fast;
            fast.fixed_dt = 1.0 / 240;
            fast.render_dt = 1.0 / 30;
            require(fast.update(0).sleep_ms < 1000.0 / 240,
                    "pacing never sleeps past the next simulation step");
            LoopTimer tiny;
            tiny.fixed_dt = 1e-15;
            require(tiny.update(0).steps == 0, "tolerance cannot fabricate tiny simulation steps");
        }
        // mesh_builder: counts and bounds
        {
            Mesh q = mesh::quad({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 1, 1});
            require(q.triangles.size() == 4, "double-sided quad");
            Mesh q1 = mesh::quad({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 1, 1}, false);
            require(q1.triangles.size() == 2, "single-sided quad");
            Mesh d = mesh::disc(0.5f, 1.0f, 0, 3.14159f, 4, {1, 1, 1}, {1, 0, 0});
            require(d.triangles.size() == 16, "crescent fan count");
            for (auto &t : d.triangles)
                for (auto &v : t.v) {
                    float r = std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y);
                    require(r <= 1.01f && std::fabs(v.position.z) <= 0.16f, "fan inside radii");
                }
            require(d.triangles[0].v[0].color.x == 1 && d.triangles[0].v[1].color.x == 1,
                    "fan vertex colors");
            Mesh g = mesh::grid(2, 3, 1.0f, {0.5f, 0.5f, 0.5f});
            require(g.triangles.size() == 12, "grid count");
            Mesh bad = mesh::disc(2, 1, 0, 1, 4, {1, 1, 1}, {1, 1, 1});
            require(bad.triangles.empty(), "inverted radii rejected");
        }
        // particles: cap, gravity, age-out, bounce on ground
        {
            ParticlePool pool(4);
            require(pool.spawn({0, 5, 0}, {0, 0, 0}, {1, 1, 1}, 10), "spawn");
            require(!pool.spawn({0, 0, 0}, {0, 0, 0}, {1, 1, 1}, -1), "dead life rejected");
            pool.spawn({0, 5, 0}, {0, 0, 0}, {1, 1, 1}, 10);
            pool.spawn({0, 5, 0}, {0, 0, 0}, {1, 1, 1}, 10);
            pool.spawn({0, 5, 0}, {0, 0, 0}, {1, 1, 1}, 10);
            require(!pool.spawn({0, 5, 0}, {0, 0, 0}, {1, 1, 1}, 10), "cap enforced");
            require(pool.alive() == 4, "pool full");
            float y0 = pool.items()[0].position.y;
            pool.update(1.0f / 60, {}, 0.35f, 0.06f, 8);
            require(pool.items()[0].position.y < y0, "gravity integrates");
            require(pool.update(30, {}, 0.35f, 0.06f, 8) == 0, "long step ages everything out");
            ParticlePool drop(8);
            drop.spawn({0, 2, 0}, {0, -30, 0}, {1, 0, 0}, 5);
            for (int i = 0; i < 120; i++)
                drop.update(1.0f / 60, ground(), 0.35f, 0.06f, 8);
            require(drop.alive() == 1 && drop.items()[0].position.y >= -0.1f, "ground stops the fall");
            ParticlePool fast(1);
            fast.spawn({0, 5, 0}, {0, -1000, 0}, {1, 1, 1}, 1, 0);
            fast.update(0.01f, ground(), 0, 0.06f, 0);
            require(fast.items()[0].position.y >= 0.05f, "continuous sweep prevents tunneling");
        }
        // follow: smoothing converges, bounds clamp, shake decays
        {
            FollowCam cam;
            cam.snap({0, 0, 0});
            cam.bounds_lo = {-5, 0, -5};
            cam.bounds_hi = {5, 5, 5};
            cam.smooth = 1000; // near-instant for the clamp test
            Vec3 at = cam.update({99, 99, 99}, 1.0f / 60);
            require(std::fabs(at.x - 5) < 1e-4f && std::fabs(at.y - 5) < 1e-4f, "bounds clamp");
            cam.smooth = 2;
            cam.snap({0, 0, 0});
            for (int i = 0; i < 600; i++)
                at = cam.update({4, 0, 0}, 1.0f / 60);
            require(std::fabs(at.x - 4) < 0.05f, "smoothing converges");
            cam.add_shake(1);
            require(cam.shake_offset(0).x != 0 || cam.shake_offset(0).y != 0, "shake nonzero");
            for (int i = 0; i < 600; i++)
                cam.update({4, 0, 0}, 1.0f / 60);
            Vec3 off = cam.shake_offset(99);
            require(off.x == 0 && off.y == 0, "shake decays to zero");
            Camera c = cam.camera({0, 1.6f, -9.5f}, 1.0f);
            require(c.position.z < c.target.z, "offset behind target");
            bool negative_dt = false;
            try {
                cam.update({}, -0.1f);
            } catch (const std::invalid_argument &) {
                negative_dt = true;
            }
            require(negative_dt, "negative camera delta rejected");
        }
        // lights: acquire/set/park/release reuse
        {
            Scene scene;
            LightPool pool;
            static_assert(!std::is_copy_constructible<LightPool>::value,
                          "copying a light pool would duplicate ownership");
            int a = pool.acquire(scene);
            int b = pool.acquire(scene);
            require(a != b && scene.lights.size() == 2, "slots grow");
            pool.set(scene, a, {1, 2, 3}, {1, 0, 0}, 6);
            require(scene.lights[size_t(a)].position.x == 1, "set writes");
            pool.release(scene, a);
            require(pool.parked() == 1, "release parks");
            require(!pool.release(scene, a) && pool.parked() == 1, "duplicate release is harmless");
            int c = pool.acquire(scene);
            require(c == a && scene.lights.size() == 2, "slot reused");
            pool.set(scene, 99, {}, {}, 0); // out of range: no crash
            require(scene.lights.size() == 2, "bad id ignored");
        }
        std::cout << "util_tests: all passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "util_tests FAILED: " << e.what() << "\n";
        return 1;
    }
}
