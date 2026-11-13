// EMBERFALL — a self-contained 2.5D validation game for Ember.
//
// Design notes:
// - 2.5D side-view platformer on the XY plane (z locked to 0). Movement uses
//   the engine's arcade path only: slide_sphere_boxes vs hand-authored static
//   AABBs. No Jolt, no Lua, no Nova needed — pure C++ on the public Runtime.
// - Fixed 60 Hz simulation driven by Runtime::tick (FixedUpdate events).
// - Same binary runs interactively (--window, SDL2 viewer) and headless
//   (--frames N --out shot.ppm) for CI/screenshots. --auto enables a simple
//   autopilot (hold-right + stuck-jump) as a smoke test.
// - HUD is diegetic 3D: heart pips + ability icons float in front of the
//   camera. Story/progress messages go to stdout (audio is a future module).
//
// Controls: A/D or arrows = move, Shift = sprint, Space = jump (with coyote + buffer),
// S/Down = slide while running (momentum locks briefly, jump cancels into a leap),
// F/J = attack, P = pause, R = respawn at checkpoint, Esc = quit.
#include "ember/character.hpp"
#include "ember/collision.hpp"
#include "ember/follow.hpp"
#include "ember/input.hpp"
#include "ember/lights.hpp"
#include "ember/loop.hpp"
#include "ember/mesh_builder.hpp"
#include "ember/runtime.hpp"
#include "ember/projectiles.hpp"
#ifdef EMBER_HAS_VFX
#include "ember/vfx.hpp"
#include "ember/vfx_recipes.hpp"
#endif
#ifdef EMBER_RMLUI
#include "ui.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef EMBER_VIEWER
#include <SDL.h>
#endif

using namespace ember;

namespace {
// ---------------------------------------------------------------- tuning
constexpr float kGravity = 30.0f;
constexpr float kMoveSpeed = 7.0f;
constexpr float kSprintSpeed = 10.5f; // Shift: horizontal only, jump height untouched
constexpr float kJumpV = 11.5f;     // ~2.2 m single jump
constexpr float kDoubleV = 10.5f;   // reaches ~4 m total with boots
constexpr float kPlayerR = 0.35f;
constexpr float kCoyote = 0.10f;
constexpr float kBuffer = 0.12f;
constexpr int kMaxHp = 5;
// Grapple tuning: single impulse toward the grabbed point (no pull).
// 15.5 from the ground apexes ~4.35 m: strong, yet short of the 5 m gate
// (and the moss sits past the wall, so no sequence break either way).
constexpr float kHookSpeed = 22.0f;
constexpr float kHookRange = 9.0f;
constexpr float kHookImpulse = 15.5f;
constexpr float kHookCd = 0.5f;
constexpr float kSlashT = 0.28f; // readable swing: charge .05, sweep .13, fade .10
constexpr float kWardenHp = 8; // hook + sword stack: needs a real pool + enrage

// ---------------------------------------------------------------- helpers
Aabb aabb_of(float x0, float y0, float x1, float y1, float zhalf = 1.2f) {
    Aabb b;
    b.add({x0, y0, -zhalf});
    b.add({x1, y1, zhalf});
    return b;
}
bool overlap(Vec3 a, Vec3 ha, Vec3 b, Vec3 hb) {
    return std::fabs(a.x - b.x) < ha.x + hb.x && std::fabs(a.y - b.y) < ha.y + hb.y;
}

// ---------------------------------------------------------------- game
struct Game {
    Runtime &rt;
    std::vector<Aabb> solids; // static colliders, z-inclusive

    ObjectRef player{};
    Vec3 vel{};
    bool grounded = false, has_double = false, used_double = false, has_key = false;
    bool has_grapple = true; // key mechanic: granted from the start, no pickup
    bool has_climb = false;  // ledge grab (Zone B pickup: ungated grab would
                             // out-reach double jump and break its wall gate)
    bool facing_right = true;
    float iframes = 0, atk_cd = 0, blink_t = 0;
    CharacterBody body; // engine arcade controller; vel/grounded mirror it
    float boost_x = 0, lunge_t = 0; // grapple momentum + attack dash (decay)
    bool prev_jump = false, jump_cut = false; // only a player jump can be shortened by releasing Space
    float fall_v = 0; // most negative vy this flight, for landing dust
    int hp = kMaxHp, deaths = 0;
    Vec3 checkpoint{-0.5f, 1.0f, 0};
    int zone = 0; // 0 A, 1 B, 2 C

    enum class State { Play, Win, Dead };
    State state = State::Play;
    double win_time = 0;
    bool paused = false;
    bool god = false, auto_pilot = false, auto_sprint = false;
    float auto_stuck_t = 0, auto_jump_t = 0, last_px = 0;
    float air_stuck = 0, last_air_x = 0;
    bool prev_grapple = false;
    float auto_hold = 0; // autopilot holds jump for full height (release-cut)
    float sprint_fx_t = 0; // dust puffs while sprinting on ground
    Vec3 pilot_aim{1, 0, 0}; // autopilot hook aim: up-forward when stuck
    int dbg_ground = 0, dbg_air = 0; // jump counters for tuning
    int dbg_slides = 0, dbg_mantles = 0;

    struct Enemy {
        ObjectRef ref{};
        Vec3 home{};
        Vec3 half{.45f, .3f, 0}; // overlap half-extents
        int kind = 0; // 0 crawler, 1 bat, 2 warden
        bool alive = true;
        float dir = 1, t = 0, hp = 1;
        float x0 = 0, x1 = 0; // patrol bounds (crawlers)
        Vec3 vel{};           // warden
        bool wground = false;
        int look = -1; // warden visual state: 0 idle, 1 telegraph squash, 2 hit flash
        float flash = 0;
        // Floating HP bar (same look as the player's): shown once damaged.
        ObjectRef bar_bg{}, bar_fg{};
        int maxhp = 1, bar_shown = -2;
        float bar_w = 1.2f;
    };
    std::vector<Enemy> enemies;
    bool warden_announced = false;

    struct Pickup {
        ObjectRef ref{};
        int kind = 0; // 0 double-jump boots, 1 brass key, 2 (retired grapnel), 3 climbing claws
        Vec3 base{};
        bool taken = false;
    };
    std::vector<Pickup> pickups;

    struct Anchor {
        ObjectRef ref{};
        Vec3 pos{};
    };
    std::vector<Anchor> anchors;

    // Ledge grab state: hanging on a wall face with a reachable top.
    bool grabbing = false;
    float grab_tx = 0, grab_ty = 0, grab_nx = 0;

    // Grappling hook state machine.
    enum class Hook { Idle, Flying };
    Hook hook = Hook::Idle;
    ObjectRef hook_ref{};
    Vec3 hook_pos{}, hook_dir{};
    float hook_traveled = 0, hook_cd = 0, rope_t = 0;
    bool hook_air = false; // one hook per airtime; landing + gold anchors refund
    Vec3 rope_b{}; // rope far end while flashing
    std::vector<ObjectRef> rope_seg;
    std::vector<ObjectRef> aim_dots; // live aim guide: where the hook goes
    Vec3 aim_now{1, 0, 0};           // last computed aim (for the guide)
    int aim_look = -1;               // dot color state: 0 none, 1 anchor, 2 foe, 3 flying
    // FPS overlay (F3) via the engine's RmlUi, created on first toggle.
#ifdef EMBER_RMLUI
    std::unique_ptr<DemoUi> ui;
#endif
    bool fps_visible = false;
    int ui_fps = -1;
    void toggle_fps(int w, int h) {
        (void)w;
        (void)h;
        fps_visible = !fps_visible;
        ui_fps = -1;
#ifdef EMBER_RMLUI
        if (fps_visible) {
            if (!ui)
                ui = std::make_unique<DemoUi>(rt.config, "assets/fps.rml", "assets/ui.ttf", w, h, paused);
            ui->set_visible(true);
        } else if (ui) {
            ui->set_visible(false);
        }
#else
        if (fps_visible)
            std::cout << "FPS overlay needs an EMBER_RMLUI build.\n";
#endif
        std::cout << (fps_visible ? "FPS overlay on.\n" : "FPS overlay off.\n");
    }
    void push_fps(int v) {
        if (!fps_visible)
            return;
#ifdef EMBER_RMLUI
        if (ui && v != ui_fps) {
            ui_fps = v;
            ui->set_text("fps", std::to_string(v));
        }
#else
        (void)v;
#endif
    }
    bool want_grapple = false;
    int hooks_fired = 0, anchors_hit = 0, enemies_yanked = 0;

    ObjectRef door_ref{};
    size_t door_solid = 0;
    bool door_open = false;
    Vec3 door_pos{};

#ifdef EMBER_HAS_VFX
    vfx::System particles{96};
    vfx::Visuals particle_visuals;
    vfx::Emitter slash_trail;
    vfx::TimedMeshes timed_fx;
#endif

    ProjectilePool projectiles{24};
    std::vector<ObjectRef> bolt_visuals;
    float shot_cd = 0;
    int shots = 0;
    ObjectRef slash_ref{};
    float slash_t = 0, slash_fx = 1;
    int slash_look = -1; // crescent phase x mirror (gated mesh swaps)
    LightPool light_pool;
    int fx_idx = -1; // reusable dynamic light for combat flashes
    float fx_light_t = 0;
    FollowCam cam; // smoothed follow + trauma shake

    // HP bar (bg + fg, fg rebuilt only when hp changes) + ability icons.
    ObjectRef bar_bg{}, bar_fg{};
    ObjectRef icon_double{}, icon_key{}, icon_hook{}, icon_climb{};
    int bar_shown = -1;
    bool icons_shown[4] = {false, false, false, false};
    float fx_t = 0; // flash timer for HUD events
    float slide_t = 0, slide_vx = 0, slide_cd = 0;
    bool prev_slide = false, auto_was_ground = false;
    int last_pmesh = -1; // player mesh key: blink x slide (gated swaps)

    // Manual input through the engine ActionMap (autopilot drives want_*
    // directly below). S/Down doubles as aim-down and slide trigger.
    enum Action { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_JUMP, A_ATTACK, A_GRAPPLE, A_SPRINT, A_SHOOT };
    ActionMap actions;

    explicit Game(Runtime &r) : rt(r)
#ifdef EMBER_HAS_VFX
        , particle_visuals(r, 96), timed_fx(r, 2)
#endif
    {
#ifdef EMBER_HAS_VFX
        particles.step.collisions = true;
        particles.step.settle_normal = {0,1,0};
        particles.step.settle_speed = 1.4f;
        particles.step.settle_life = .45f;
        particles.step.kill_below = -20;
        slash_trail.rate = 30;
        slash_trail.effect.particle.color = {.85f, .95f, 1};
        slash_trail.effect.particle.gravity = 0;
        slash_trail.effect.particle.plane_normal = {0,0,1};
        slash_trail.effect.speed = .5f;
        slash_trail.effect.life_min = slash_trail.effect.life_max = .25f;
#endif
    }

    void say(const std::string &msg) {
        char b[32];
        std::snprintf(b, sizeof b, "[t=%6.1fs] ", rt.time());
        std::cout << b << msg << "\n" << std::flush;
    }

    // -- world construction ------------------------------------------------
    ObjectRef add_visual(Mesh m, Vec3 p, float yaw = 0) {
        Object o(std::move(m), p, yaw);
        return rt.add_object(std::move(o));
    }
    void solid(float x0, float y0, float x1, float y1, Vec3 color, float zhalf = 1.2f) {
        Vec3 c{(x0 + x1) * .5f, (y0 + y1) * .5f, 0};
        add_visual(box({x1 - x0, y1 - y0, zhalf * 2}, color), c);
        solids.push_back(aabb_of(x0, y0, x1, y1, zhalf + 0.4f));
    }
    void decor(float x0, float y0, float x1, float y1, float z, Vec3 color) {
        Vec3 c{(x0 + x1) * .5f, (y0 + y1) * .5f, z};
        add_visual(box({x1 - x0, y1 - y0, 1.0f}, color), c);
    }

    void enemy_bars(Enemy &e) {
        e.maxhp = int(e.hp);
        e.bar_w = std::max(1.0f, e.half.x * 2 + 0.4f);
        e.bar_bg = add_visual(box({e.bar_w, .16f, .1f}, {.12f, .12f, .15f}), {0, -99, 0});
        e.bar_fg = add_visual(box({e.bar_w - .2f, .1f, .1f}, {.25f, .9f, .35f}), {0, -99, 0});
    }

    void build_world() {
#ifdef EMBER_VIEWER
        actions.bind(A_LEFT, SDLK_a).bind(A_LEFT, SDLK_LEFT).bind(A_RIGHT, SDLK_d)
            .bind(A_RIGHT, SDLK_RIGHT)
            .bind(A_UP, SDLK_w)
            .bind(A_UP, SDLK_UP)
            .bind(A_DOWN, SDLK_s)
            .bind(A_DOWN, SDLK_DOWN)
            .bind(A_JUMP, SDLK_SPACE)
            .bind(A_ATTACK, SDLK_f)
            .bind(A_ATTACK, SDLK_j)
            .bind(A_GRAPPLE, SDLK_g)
            .bind(A_SPRINT, SDLK_LSHIFT)
            .bind(A_SPRINT, SDLK_RSHIFT)
            .bind(A_SHOOT, SDLK_k).bind(A_SHOOT, SDLK_x);
#endif
        // Zone A — Ashen Gate (x -2..26), warm grey stone.
        const Vec3 stone{.45f, .42f, .48f}, dark{.25f, .23f, .28f};
        solid(-3, -2, -2, 6, dark);             // west boundary cliff
        solid(-2, -2, 14, 0, stone);            // ground A
        solid(5, 0, 7, 1.0f, dark);             // steps to the sigil ledge
        solid(9, 0, 11, 2.0f, dark);
        solid(13, 0, 17, 3.0f, stone);          // sigil ledge
        solid(14, -2, 26, 0, stone);            // ground A-east
        solid(24, 0, 25, 3.6f, {.6f, .3f, .2f}); // WALL: double jump only
        // (jump+rock-hook apexes ~3.6 < 3.95; double apexes ~4.4)
        // Zone B — Sunken Foundry (x 26..58), rust tones + lava pit.
        const Vec3 rust{.5f, .32f, .2f};
        solid(25, -2, 36, 0, rust);
        solid(37, -2, 39, 1.2f, dark);          // lava platforms
        solid(41, -2, 43, 1.2f, dark);
        solid(46, -2, 58, 0, rust);
        add_visual(box({10, 0.3f, 2.2f}, {1.0f, .35f, .05f}), {41, -2.35f, 0}); // lava glow
        // GATE (blocker, removed w/ key). 7 m: jump+hook+double apexes ~6.5,
        // so even the full combo can't skip it. Gold anchors refund double;
        // rock bites consume it (see yank()).
        door_solid = solids.size();
        door_ref = add_visual(box({1.0f, 7.0f, 2.4f}, {.2f, .2f, .25f}), {56.5f, 3.5f, 0});
        solids.push_back(aabb_of(56, 0, 57, 7.0f, 1.6f));
        door_pos = {56.5f, 3.5f, 0};
        // Zone C — Warden's Core (x 58..86), cold blue stone arena.
        const Vec3 cold{.3f, .38f, .5f};
        solid(58, -2, 86, 0, cold);
        solid(86, -2, 87, 6, dark);             // east boundary cliff
        solid(62, 0, 63, 3.0f, dark);           // entrance pillar (jump it)
        decor(81, 0, 82, 4.0f, 6.0f, dark);     // backdrop spire: decor only,
                                                // the arena stays open so the Warden never wedges
        add_visual(box({1.2f, 1.8f, 1.2f}, {.4f, 1.0f, .9f}), {84, 1.2f, 0}); // Heart crystal
        rt.scene.lights.push_back({{84, 2.5f, -2}, {.4f, 1.0f, .9f}, 9});
        // Background silhouettes (z behind play plane).
        for (int i = 0; i < 14; i++) {
            float x = -2 + i * 6.5f;
            float h = 4 + (i % 4) * 2.0f;
            Vec3 c = i < 5 ? Vec3{.16f, .14f, .18f} : i < 9 ? Vec3{.18f, .12f, .09f} : Vec3{.1f, .14f, .2f};
            decor(x, 0, x + 3, h, 6.0f, c);
        }
        // Torches: bright markers, no real lights (budget).
        for (float x : {4.0f, 20.0f, 30.0f, 50.0f, 64.0f, 78.0f})
            add_visual(box({.25f, .6f, .25f}, {1.0f, .6f, .15f}), {x, 1.6f, -1.0f});

        // Player.
        player = add_visual(box({.7f, .7f, .7f}, {1.0f, .55f, .15f}), {-0.5f, 1.0f, 0});
        cam.bounds_lo = {-1, 0.8f, -1};
        cam.bounds_hi = {84, 6, 1};
        cam.smooth = 10;
        cam.snap({1.0f, 1.5f, 0});
        // Enemies: crawler A, crawler B, bat, warden (dormant).
        auto crawl = [&](float x, float top, float a, float b) {
            Enemy e;
            e.ref = add_visual(box({.8f, .5f, .8f}, {.7f, .15f, .3f}), {x, top + .25f, 0});
            e.home = {x, top + .25f, 0};
            e.x0 = a;
            e.x1 = b;
            e.hp = 1;
            enemy_bars(e);
            enemies.push_back(e);
        };
        crawl(20.0f, 0, 17.5f, 23.0f);
        crawl(51.0f, 0, 47.5f, 54.5f);
        Enemy bat;
        bat.kind = 1;
        bat.half = {.35f, .35f, 0};
        bat.ref = add_visual(box({.6f, .6f, .6f}, {.25f, .3f, .8f}), {41, 3.5f, 0});
        bat.home = {41, 3.5f, 0};
        bat.hp = 1;
        enemy_bars(bat);
        enemies.push_back(bat);
        Enemy w;
        w.kind = 2;
        w.half = {.85f, .85f, 0};
        w.ref = add_visual(box({1.6f, 1.6f, 1.6f}, {.55f, .1f, .12f}), {72, 1.0f, 0});
        w.home = {72, 1.0f, 0};
        w.hp = kWardenHp;
        enemy_bars(w);
        enemies.push_back(w);
        // Pickups.
        Pickup boots;
        boots.kind = 0;
        boots.base = {15, 3.9f, 0};
        boots.ref = add_visual(box({.5f, .5f, .5f}, {.2f, .9f, 1.0f}), boots.base);
        pickups.push_back(boots);
        rt.scene.lights.push_back({boots.base, {.2f, .9f, 1.0f}, 6});
        Pickup key;
        key.kind = 1;
        key.base = {42, 1.9f, 0};
        key.ref = add_visual(box({.45f, .45f, .45f}, {1.0f, .8f, .2f}), key.base);
        pickups.push_back(key);
        // (No grapnel pickup: the hook is a core mechanic, granted at spawn.)
        Pickup claws; // ledge grab: gated here so jump+grab can't skip the double wall
        claws.kind = 3;
        claws.base = {38, 2.0f, 0};
        claws.ref = add_visual(box({.45f, .45f, .45f}, {.75f, .35f, .95f}), claws.base);
        pickups.push_back(claws);
        // Grapple anchors: bright studs with full-strength yank. Placed so
        // none of them (nor any rock line) skips a gate by itself.
        const Vec3 anchor_pos[] = {{10, 5.2f, 0}, {30, 5.5f, 0}, {41, 5.4f, 0}, {70, 5.6f, 0}};
        for (Vec3 ap : anchor_pos) {
            Anchor a;
            a.pos = ap;
            a.ref = add_visual(box({.45f, .45f, .45f}, {1.0f, .85f, .3f}), ap);
            anchors.push_back(a);
        }
        // HUD: hp bar + ability icons (repositioned every frame,
        // meshes rebuilt only on change). Rope segments parked out of view.
        bar_bg = add_visual(box({2.4f, .3f, .1f}, {.12f, .12f, .15f}), {0, -99, 0});
        bar_fg = add_visual(box({2.2f, .2f, .1f}, {.2f, .9f, .3f}), {0, -99, 0});
        icon_double = add_visual(box({.24f, .24f, .24f}, {.2f, .2f, .25f}), {0, -99, 0});
        icon_key = add_visual(box({.24f, .24f, .24f}, {.2f, .2f, .25f}), {0, -99, 0});
        icon_hook = add_visual(box({.24f, .24f, .24f}, {.2f, .2f, .25f}), {0, -99, 0});
        icon_climb = add_visual(box({.24f, .24f, .24f}, {.2f, .2f, .25f}), {0, -99, 0});
        hook_ref = add_visual(box({.25f, .25f, .25f}, {1.0f, .85f, .3f}), {0, -99, 0});
        for (int i = 0; i < 12; i++)
            rope_seg.push_back(add_visual(box({.13f, .13f, .13f}, {.9f, .8f, .5f}), {0, -99, 0}));
        for (int i = 0; i < 6; i++)
            aim_dots.push_back(add_visual(box({.12f, .12f, .12f}, {.5f, .5f, .55f}), {0, -99, 0}));

        bolt_visuals.reserve(24);
        for(int i=0;i<24;++i) {
            auto ref = add_visual(box({.24f,.24f,.16f},{1,.8f,.25f}), {});
            rt.object(ref).visible = false;
            bolt_visuals.push_back(ref);
        }
        say("EMBERFALL: find the Heart of the Forge. A/D move, Space jump, F attack, K/X shoot.");
        say("G + direction fires the hook from the start: gold = full yank, rock bites, foes bleed.");
        say("--- Ashen Gate --- (the high wall east needs a gift of the sky)");
    }

    // -- particles ----------------------------------------------------------
    void burst(Vec3 p, Vec3 color, int n, float spread = 4, float grav = 1, float life = -1) {
#ifdef EMBER_HAS_VFX
        vfx::Burst effect = vfx::recipes::planar_sparks(color, spread);
        effect.count = size_t(std::max(0, n));
        effect.particle.color = color;
        effect.particle.acceleration = {0, -20 * grav, 0};
        effect.speed = spread;
        effect.phase = 0; // the recipe uses a half-ring; rotation is a game choice
        if (life > 0)
            effect.life_min = effect.life_max = life;
        particles.burst(p, effect);
#else
        (void)p; (void)color; (void)n; (void)spread; (void)grav; (void)life;
#endif
    }
    void update_particles(float dt) {
#ifdef EMBER_HAS_VFX
        particles.update(dt, solids);
#else
        (void)dt;
#endif
    }
    void sync_vfx() {
#ifdef EMBER_HAS_VFX
        particle_visuals.sync(particles);
#endif
    }

    // -- damage / checkpoints -------------------------------------------------
    void respawn() {
        auto &o = rt.object(player);
        o.position = checkpoint;
        vel = {};
        projectiles.clear();
        jump_cut = false;
        grabbing = false;
        o.active = true;
    }
    void hurt(Vec3 from) {
        if (god || iframes > 0 || state != State::Play)
            return;
        grabbing = false; // hits knock you off the wall
        jump_cut = false;
        hp--;
        fx_t = 0;
        cam.add_shake(0.35f);
        say("Ouch! HP " + std::to_string(std::max(0, hp)) + "/" + std::to_string(kMaxHp));
        burst(rt.object(player).position, {1, .2f, .2f}, 8, 3);
        fx_flash(rt.object(player).position, {1, .25f, .25f}, 0.2f);
        if (hp <= 0) {
            deaths++;
            hp = kMaxHp;
            say("You fall... (deaths: " + std::to_string(deaths) + ") The embers return you.");
            respawn();
        } else {
            vel.x = (rt.object(player).position.x < from.x ? -6 : 6);
            vel.y = 6;
        }
        iframes = 1.0f;
    }

    bool point_in_solids(Vec3 p, float pad = 0.15f) {
        for (auto &b : solids)
            if (p.x > b.lo.x - pad && p.x < b.hi.x + pad && p.y > b.lo.y - pad && p.y < b.hi.y + pad &&
                p.z > b.lo.z - pad && p.z < b.hi.z + pad)
                return true;
        return false;
    }

    // Shared soft-aim: nearest anchor/enemy inside a 35-degree cone of the
    // aim wins (anchors preferred), else the hook flies exactly where pointed.
    // Returns flight dir; kind 1 = anchor, 2 = foe, 0 = open line.
    Vec3 aim_assist(Vec3 from, Vec3 aim, int &kind) {
        Vec3 best = aim;
        kind = 0;
        float best_score = 1e30f;
        auto consider = [&](Vec3 p, float bonus, int k) {
            Vec3 d = p - from;
            float dist = length(d);
            if (dist > kHookRange || dist < 0.5f)
                return;
            float align = dot(d / dist, aim); // 1 = dead on
            if (align < 0.8f)
                return;
            float score = dist - bonus + (1 - align) * 6;
            if (score < best_score) {
                best_score = score;
                best = d / dist;
                kind = k;
            }
        };
        for (auto &a : anchors)
            consider(a.pos, 1.5f, 1); // anchors preferred: the mobility tool
        for (auto &e : enemies)
            if (e.alive && rt.object_alive(e.ref))
                consider(rt.object(e.ref).position, 0, 2);
        return best;
    }

    void fire_hook(Vec3 from, Vec3 aim) {
        int kind = 0;
        hook = Hook::Flying;
        hook_pos = from;
        hook_dir = aim_assist(from, aim, kind);
        (void)kind;
        hook_traveled = 0;
        hook_cd = kHookCd;
        hook_air = true; // spent until landing or a gold anchor
        hooks_fired++;
        burst(from, {1.0f, .85f, .3f}, 3, 1.5f);
    }

    static bool query_bolt(Vec3 from, Vec3 delta, float radius, ProjectileContact &hit, const void *ctx) {
        auto &game = *static_cast<const Game *>(ctx);
        bool found = false;
        auto consider = [&](Aabb box, uint64_t target) {
            float t; Vec3 normal;
            if(sweep_sphere_box(from,delta,radius,box,t,normal) && (!found || t<hit.fraction)) {
                found=true; hit={t,normal,target};
            }
        };
        for(const auto &box : game.solids) consider(box,0); // walls win ties
        for(size_t i=0;i<game.enemies.size();++i) {
            const auto &e=game.enemies[i];
            if(!e.alive || !game.rt.object_alive(e.ref)) continue;
            auto pos=game.rt.object(e.ref).position;
            Aabb box; box.add(pos-Vec3{e.half.x,e.half.y,.5f}); box.add(pos+Vec3{e.half.x,e.half.y,.5f});
            consider(box,i+1);
        }
        return found;
    }
    void update_bullets(float dt, Vec3 p, Vec3 aim, bool held) {
        shot_cd=std::max(0.f,shot_cd-dt);
        if(held && shot_cd<=0) {
            // Begin at the muzzle origin, sweeping even the first travel segment
            // so an aim offset cannot teleport a shot through a nearby wall.
            Projectile b; b.position=p+Vec3{0,.2f,0}; b.velocity=aim*26; b.life=1.1f;
            if(projectiles.spawn(b)) { shot_cd=.25f; ++shots; fx_flash(p,{1,.7f,.3f},.08f,5); }
        }
        projectiles.update(dt,{query_bolt,this});
        for(const auto &hit : projectiles.impacts()) {
            burst(hit.projectile.position,{1,.7f,.3f},7,4);
            fx_flash(hit.projectile.position,{1,.55f,.2f},.15f,5);
            if(hit.target) damage_enemy(enemies.at(size_t(hit.target-1)),1,hit.projectile.velocity.x>=0?1.f:-1.f);
        }
        for(size_t i=0;i<bolt_visuals.size();++i) {
            auto ref=bolt_visuals[i];
            if(!rt.object_alive(ref)) continue;
            rt.object(ref).visible=i<projectiles.alive();
            if(i<projectiles.alive()) rt.object(ref).position=projectiles.items()[i].position;
        }
    }

    void update_hook(float dt, Vec3 pp) {
        hook_cd = std::max(0.f, hook_cd - dt);
        rope_t = std::max(0.f, rope_t - dt);
        if (hook != Hook::Flying)
            return;
        // ONE impulse toward the grabbed point, then momentum + gravity.
        // Gold anchors: full yank, refund double + air-hook (designed lines).
        // Rock bites: vy capped at +8, so even jump+double+rock apexes ~5.5,
        // short of the 7 m gate. No chains: one hook per airtime.
        auto yank = [&](Vec3 target, bool gold) {
            jump_cut = false; // hook impulse is independent of the jump button
            Vec3 imp = normalized(target - pp) * kHookImpulse;
            if (!gold)
                imp.y = std::min(imp.y, 8.f);
            vel.x = imp.x;
            vel.y = imp.y;
            boost_x = imp.x * 0.85f; // keep most of the horizontal kick
            if (grounded)
                vel.y = std::max(vel.y, 3.f);
            grounded = false;
            if (gold) {
                used_double = false;
                hook_air = false;
            }
        };
        // Substepped flight: catches thin faces, no tunneling at 22 m/s.
        const int sub = 4;
        float piece = kHookSpeed * dt / sub;
        bool flying = true;
        for (int i = 0; i < sub && flying; i++) {
            hook_pos = hook_pos + hook_dir * piece;
            hook_traveled += piece;
            // 1. Gold anchors: generous radius, refresh double jump.
            for (auto &a : anchors) {
                if (length(a.pos - hook_pos) < 0.75f) {
                    yank(a.pos, true);
                    anchors_hit++;
                    burst(a.pos, {1.0f, .85f, .3f}, 10, 3);
                    fx_flash(a.pos, {1, .85f, .3f}, 0.12f);
                    rope_b = a.pos;
                    rope_t = 0.15f;
                    flying = false;
                    break;
                }
            }
            if (!flying)
                break;
            // 2. Enemies: mutual yank — you slam toward it (13 m/s), it gets
            // dragged toward you, it bleeds. Grab grace covers the impact.
            for (auto &e : enemies) {
                if (!e.alive || !rt.object_alive(e.ref))
                    continue;
                Vec3 ep = rt.object(e.ref).position;
                if (overlap(hook_pos, {.3f, .3f, 0}, ep, e.half)) {
                    Vec3 to_foe = ep - pp;
                    float d = std::max(0.6f, length(to_foe));
                    Vec3 dir = to_foe / d;
                    vel.x = dir.x * 13.f;
                    vel.y = std::min(dir.y * 13.f, 8.f); // same cap as rock
                    jump_cut = false;
                    boost_x = dir.x * 13.f * 0.85f;
                    grounded = false;
                    used_double = true; // spent like a rock bite
                    Vec3 drag{-dir.x, 0, 0}; // toward you, ground plane
                    Vec3 nep = ep + drag * std::min(e.kind == 2 ? 0.5f : 1.2f, d * 0.5f);
                    if (e.kind == 0) { // crawlers keep their patrol shape
                        float sh = nep.x - ep.x;
                        e.x0 += sh;
                        e.x1 += sh;
                    }
                    if (e.kind == 1)
                        e.home = e.home + (nep - ep);
                    rt.object(e.ref).position = nep;
                    damage_enemy(e, 1, pp.x < ep.x ? -1.f : 1.f);
                    iframes = std::max(iframes, 0.4f); // grab grace
                    enemies_yanked++;
                    burst(ep, {1.0f, .9f, .4f}, 10, 3.5f);
                    rope_b = ep;
                    rope_t = 0.15f;
                    flying = false;
                    break;
                }
            }
            if (!flying)
                break;
            // 3. Any structure: bite the surface, no double refresh (gates stay gates).
            if (point_in_solids(hook_pos, 0.1f)) {
                Vec3 bite = hook_pos - hook_dir * piece; // back off to the face
                yank(bite, false);
                burst(bite, {1.0f, .85f, .3f}, 10, 3);
                rope_b = bite;
                rope_t = 0.15f;
                flying = false;
                break;
            }
            if (hook_traveled >= kHookRange) {
                burst(hook_pos, {.8f, .8f, .8f}, 3, 1.5f);
                if (rope_t <= 0) {
                    rope_b = hook_pos;
                    rope_t = 0.1f;
                }
                flying = false;
                break;
            }
        }
        if (!flying)
            hook = Hook::Idle;
    }

    // Crescent slash via the engine fan builder (mirrored by facing).
    Mesh slash_mesh(bool mirror, float girth, Vec3 hot, Vec3 edge) {
        Mesh m = mesh::disc(0.6f * girth, 1.3f * girth, -0.6f, 2.0f, 12, hot, edge, 0.15f, true);
        if (mirror)
            for (auto &t : m.triangles)
                for (auto &v : t.v)
                    v.position.x *= -1;
        return m;
    }

    // One reusable dynamic combat light (the scene budget is 2 local lights:
    // heart + this). Parked far away when idle.
    void fx_flash(Vec3 pos, Vec3 color, float time, float radius = 7) {
#ifdef EMBER_HAS_VFX
        if (fx_idx < 0)
            fx_idx = light_pool.acquire(rt.scene);
        light_pool.set(rt.scene, fx_idx, pos, color, radius);
        fx_light_t = time;
#else
        (void)pos; (void)color; (void)time; (void)radius;
#endif
    }

    void park_light_near(Vec3 p) {
        for (auto &l : rt.scene.lights)
            if (length(l.position - p) < 1.0f)
                l.position = {0, -99, 0};
    }

    // -- per-fixed-step update --------------------------------------------------
    void update(float dt) {
        fx_t += dt;
#ifdef EMBER_HAS_VFX
        timed_fx.update(dt);
#endif
        if (state != State::Play) {
            update_particles(dt);
            return;
        }
        iframes = std::max(0.f, iframes - dt);
        atk_cd = std::max(0.f, atk_cd - dt);
        slash_t -= dt;
        if (slide_t > 0) {
            slide_t -= dt;
            if (slide_t <= 0) { // slide ends: keep half the momentum, decaying
                slide_t = 0;
                boost_x += slide_vx * 0.5f;
            }
        }
        slide_cd -= dt;
        if (fx_light_t > 0) {
            fx_light_t -= dt;
            if (fx_light_t <= 0 && fx_idx >= 0)
                light_pool.park(rt.scene, fx_idx);
        }
        if (slash_t <= 0) {
            slash_ref = {};
        } else if (rt.object_alive(slash_ref)) {
            // 3 phases over kSlashT: charge (small+dim) -> sweep (big+bright,
            // gliding along the swing) -> fade. Plus bluish trail ghosts.
            float age = kSlashT - slash_t;
            int phase = age < 0.05f ? 0 : age < 0.18f ? 1 : 2;
            int key = phase * 2 + (slash_fx < 0 ? 1 : 0);
            auto &so = rt.object(slash_ref);
            Vec3 base = rt.object(player).position;
            so.position = phase == 0 ? base + Vec3{slash_fx * .5f, .8f, 0}
                          : phase == 1 ? base + Vec3{slash_fx * 1.5f, .1f, 0}
                                       : base + Vec3{slash_fx * 1.1f, -.6f, 0};
            if (key != slash_look) {
                slash_look = key;
                Vec3 hot = phase == 1 ? Vec3{.9f, .97f, 1} : Vec3{.55f, .65f, .8f};
                Vec3 edge = phase == 2 ? Vec3{.3f, .4f, .7f} : Vec3{.25f, .55f, 1};
                float girth = phase == 0 ? 0.7f : phase == 1 ? 1.15f : 0.85f;
                so.mesh = slash_mesh(slash_fx < 0, girth, hot, edge);
            }
#ifdef EMBER_HAS_VFX
            if (phase == 1)
                slash_trail.emit(particles, so.position, dt);
#endif
        }

        auto &po = rt.object(player);
        // Input.
        float dir = 0;
        bool want_jump = false, want_attack = false, want_grapple = false, want_sprint = false,
             want_slide = false;
        if (auto_pilot) {
            dir = 1;
            facing_right = true;
            want_sprint = auto_sprint;
            // Stand ground against the Warden so the smoke test fights it.
            for (auto &e : enemies) {
                if (e.kind == 2 && e.alive && rt.object_alive(e.ref)) {
                    Vec3 ep = rt.object(e.ref).position;
                    if (std::fabs(ep.x - po.position.x) < 7 && std::fabs(ep.y - po.position.y) < 4) {
                        dir = 0;
                        facing_right = ep.x >= po.position.x;
                        break;
                    }
                }
            }
            auto_jump_t += dt;
            if (std::fabs(po.position.x - last_px) < 0.02f)
                auto_stuck_t += dt;
            else {
                auto_stuck_t = 0;
                last_px = po.position.x;
            }
            if ((auto_stuck_t > 0.35f && grounded) || (grounded && auto_jump_t > 0.9f)) {
                auto_hold = 0.28f; // hold for full height (release-cut)
                prev_jump = false; // force an edge: a topping-up hold must still jump
                auto_jump_t = 0;
                auto_stuck_t = 0;
            }
            // Mid-air second jump when glued to a tall face.
            if (!grounded) {
                if (std::fabs(po.position.x - last_air_x) < 0.012f)
                    air_stuck += dt;
                else {
                    air_stuck = 0;
                    last_air_x = po.position.x;
                }
                if (has_double && !used_double && air_stuck > 0.2f) {
                    auto_hold = 0.22f;
                    prev_jump = false; // force an edge (ground hold may still be up)
                    air_stuck = -1.0f; // one kick per flight; reset on landing
                }
            } else {
                air_stuck = 0;
                last_air_x = po.position.x;
            }
            // Swing at anything ahead: lets the smoke test clear enemies + boss.
            for (auto &e : enemies) {
                if (!e.alive || !rt.object_alive(e.ref))
                    continue;
                Vec3 ep = rt.object(e.ref).position;
                float ahead = (ep.x - po.position.x) * (facing_right ? 1.f : -1.f);
                if (ahead > 0 && ahead < 2.4f && std::fabs(ep.y - po.position.y) < 1.7f)
                    want_attack = true;
            }
            if (auto_hold > 0) {
                want_jump = true;
                auto_hold -= dt;
            }
            // Slide test on safe ground only (never over the lava pit).
            if (auto_sprint && grounded && !auto_was_ground && slide_cd <= 0 &&
                (po.position.x < 34 || po.position.x > 48))
                want_slide = true;
            auto_was_ground = grounded;
            // Fire the hook when glued or when prey is near but out of melee.
            if (has_grapple && hook == Hook::Idle && hook_cd <= 0) {
                bool stall = auto_stuck_t > 0.6f || air_stuck > 0.4f;
                bool prey = false;
                for (auto &e : enemies) {
                    if (!e.alive || !rt.object_alive(e.ref))
                        continue;
                    Vec3 ep = rt.object(e.ref).position;
                    if (std::fabs(ep.x - po.position.x) < kHookRange &&
                        std::fabs(ep.y - po.position.y) < 4) {
                        prey = true;
                        break;
                    }
                }
                if (stall || prey) {
                    want_grapple = true;
                    // Stuck on geometry: aim up-forward for a mounting line.
                    // Prey: flat toward it for the yank.
                    pilot_aim = (stall && !prey)
                                    ? normalized(Vec3{facing_right ? 1.f : -1.f, 0.8f, 0})
                                    : Vec3{facing_right ? 1.f : -1.f, 0, 0};
                }
            }
            if (grabbing) {
                auto_hold = 0.2f;
                prev_jump = false; // force the mantle edge
            }
            // Hunt the Warden once provoked instead of face-planting east.
            for (auto &e : enemies) {
                if (e.kind == 2 && e.alive && rt.object_alive(e.ref) && warden_announced) {
                    Vec3 ep = rt.object(e.ref).position;
                    if (std::fabs(ep.x - po.position.x) >= 2.5f)
                        dir = ep.x > po.position.x ? 1 : -1;
                    break;
                }
            }
        } else {
            if (actions.held(A_LEFT))
                dir -= 1;
            if (actions.held(A_RIGHT))
                dir += 1;
            want_jump = actions.held(A_JUMP);
            want_attack = actions.held(A_ATTACK) || actions.pressed(A_ATTACK);
            want_grapple = actions.held(A_GRAPPLE);
            want_sprint = actions.held(A_SPRINT);
            want_slide = actions.held(A_DOWN); // S doubles as aim-down
        }
        if (dir != 0)
            facing_right = dir > 0;
        // 8-way aim: held direction + G fires there. Neutral = facing ahead.
        // Jump is Space-only, so skyward aim never jumps by itself.
        Vec3 aim{facing_right ? 1.f : -1.f, 0, 0};
        if (auto_pilot) {
            aim = pilot_aim;
        } else {
            float ax = 0, ay = 0;
            if (actions.held(A_RIGHT))
                ax += 1;
            if (actions.held(A_LEFT))
                ax -= 1;
            if (actions.held(A_UP))
                ay += 1;
            if (actions.held(A_DOWN))
                ay -= 1;
            if (ax != 0 || ay != 0)
                aim = normalized({ax, ay, 0});
        }
        aim_now = aim;

        // Jump on rising edge only (holding space won't auto-bunny-hop);
        // release while rising cuts the jump short: variable height.
        bool jump_edge = auto_pilot ? want_jump && !prev_jump : actions.pressed(A_JUMP);
        prev_jump = want_jump;
        bool grapple_edge = auto_pilot ? want_grapple && !prev_grapple : actions.pressed(A_GRAPPLE);
        prev_grapple = want_grapple;
        bool slide_edge = auto_pilot ? want_slide && !prev_slide : actions.pressed(A_DOWN);
        prev_slide = want_slide;
        // Slide: tap S while running on the ground. Locks momentum briefly.
        if (slide_edge && slide_cd <= 0 && grounded && std::fabs(vel.x) > 3) {
            slide_t = 0.55f;
            slide_cd = 0.9f;
            slide_vx = (vel.x >= 0 ? 1.f : -1.f) * std::max(std::fabs(vel.x) + 1.f, 11.f);
            facing_right = slide_vx > 0;
            dbg_slides++;
            burst(po.position + Vec3{0, -.3f, 0}, {.8f, .75f, .7f}, 6, 2.f);
        }
        bool sliding = slide_t > 0;
        if (jump_edge)
            body.queue_jump(kBuffer);
        boost_x *= std::max(0.f, 1 - 2.5f * dt);
        lunge_t -= dt;
        float speed = want_sprint ? kSprintSpeed : kMoveSpeed;
        float vx = sliding ? slide_vx : dir * speed + boost_x + (lunge_t > 0 ? (facing_right ? 3.f : -3.f) : 0);
        // Sprint/slide dust while running on the ground.
        sprint_fx_t -= dt;
        if (((want_sprint && dir != 0) || sliding) && grounded && sprint_fx_t <= 0) {
            sprint_fx_t = sliding ? 0.12f : 0.22f;
#ifdef EMBER_HAS_VFX
            auto dust = vfx::recipes::running_dust({0,1,0}, {vx,0,0});
            dust.particle.plane_normal = {0,0,1}; // EMBERFALL policy, not an engine restriction
            particles.burst(po.position + Vec3{-dir * .3f, -.3f, 0}, dust);
#endif
        }
        if (grapple_edge && has_grapple && hook == Hook::Idle && hook_cd <= 0 && (!hook_air || grounded))
            fire_hook(po.position, aim);
        bool was_grounded = grounded;
        Vec3 p = po.position;
        if (!grabbing) {
            if (body.jump_buffer > 0 && has_double && !used_double && !grounded) {
                body.air_jump(kDoubleV);
                vel.y = kDoubleV; // mirror: the sync below would clobber it
                jump_cut = true;
                used_double = true;
                body.jump_buffer = 0;
                dbg_air++;
                burst(po.position, {.5f, .9f, 1.0f}, 6, 2.5f);
            }
            body.position = po.position;
            body.position.z = 0; // XY-plane game: locked axis stays pinned
            body.velocity = vel;
            body.velocity.x = vx; // custom horizontal: input + boost + slide + lunge
            if (body.update(solids, dt, kGravity, kJumpV, kCoyote, 18)) {
                jump_cut = true;
                used_double = false;
                dbg_ground++;
                if (sliding) { // slide-cancelled leap keeps the momentum
                    boost_x = slide_vx - dir * speed;
                    slide_t = 0;
                    sliding = false;
                }
            }
            grounded = body.grounded;
            vel = body.velocity;
            if (grounded)
                jump_cut = false;
            if (jump_cut && !want_jump) {
                vel.y = std::min(vel.y, 5.f); // also handles a tap completed between fixed steps
                jump_cut = false;
            }
            p = body.position;
            float wall_nx = 0;
            if (body.hit_wall)
                wall_nx = body.last_wall_normal.x >= 0 ? 1.f : -1.f;
            if (!was_grounded && grounded && vel.y == 0) {
                burst(p + Vec3{0, -.3f, 0}, {.7f, .7f, .7f}, fall_v < -13 ? 8 : 3, 1.5f);
                fall_v = 0;
            } else if (!grounded) {
                fall_v = std::min(fall_v, vel.y);
            }
            // Ledge grab: falling into a face with chest on rock, headroom
            // above, and a top edge within reach. (Claws-gated: ungated grab
            // would out-reach double jump and break its wall.)
            if (has_climb && !grounded && wall_nx != 0 && vel.y < 1.0f) {
                float toward = -wall_nx;
                float top = 1e30f;
                for (float y = p.y + 0.3f; y < p.y + 2.5f; y += 0.15f)
                    if (!point_in_solids({p.x + toward * 0.5f, y, 0}, 0.05f)) {
                        top = y;
                        break;
                    }
                Vec3 head{p.x + toward * 0.55f, p.y + 1.05f, 0};
                Vec3 chest{p.x + toward * 0.55f, p.y + 0.2f, 0};
                if (top < 1e29f && !point_in_solids(head, 0.05f) && point_in_solids(chest, 0.05f)) {
                    grabbing = true;
                    grab_nx = wall_nx;
                    grab_tx = p.x + toward * 0.7f;
                    grab_ty = top + 0.4f;
                    vel = {};
                    facing_right = toward > 0;
                    burst(p, {.8f, .75f, .7f}, 4, 1.5f);
                }
            }
        }
        if (grabbing) {
            // Hanging: Space mantles onto the edge, S (or pushing away) drops.
            if (jump_edge) {
                po.position = {grab_tx, grab_ty, 0};
                p = po.position;
                body.jump_buffer = 0;
                used_double = false;
                vel = {};
                grabbing = false;
                dbg_mantles++;
                burst(p, {.8f, .75f, .7f}, 6, 2);
            } else if (want_slide || (dir != 0 && (dir > 0 ? 1.f : -1.f) == grab_nx)) {
                grabbing = false;
                vel.x = grab_nx * 2;
                vel.y = 0;
            } else {
                vel = {};
            }
        }
        if (grounded)
            hook_air = false; // touchdown refunds the air-hook
        p.z = 0;
        po.position = p;

        // Lava + kill plane (lava hurts; the void only returns you).
        if (p.x > 36 && p.x < 46 && p.y < -1.0f) {
            say("The molten veins bite.");
            if (!god) {
                hp--;
                if (hp <= 0) {
                    deaths++;
                    hp = kMaxHp;
                    say("You fall... (deaths: " + std::to_string(deaths) + ")");
                }
            }
            iframes = 1.0f;
            respawn();
            return;
        }
        if (p.y < -6) {
            say("The void claims you.");
            iframes = 1.0f;
            respawn();
            return;
        }

        // Attack: instant hit, blue crescent VFX + dynamic light follow.
        if (want_attack && atk_cd <= 0) {
            atk_cd = 0.35f;
            slash_t = kSlashT;
#ifdef EMBER_HAS_VFX
            slash_trail.reset();
#endif
            lunge_t = 0.12f;
            float fx = facing_right ? 1 : -1;
            slash_fx = fx;
            slash_look = -1;
            if (!rt.object_alive(slash_ref))
#ifdef EMBER_HAS_VFX
                slash_ref = timed_fx.spawn(slash_mesh(fx < 0, 0.7f, {.55f, .65f, .8f}, {.25f, .55f, 1}),
                                          p + Vec3{fx * .5f, .8f, 0}, kSlashT);
#else
                slash_ref = {};
#endif
            fx_flash(p + Vec3{fx * 1.2f, .2f, 0}, {.45f, .65f, 1.0f}, kSlashT, 9);
            for (auto &e : enemies) {
                if (!e.alive || !rt.object_alive(e.ref))
                    continue;
                Vec3 ep = rt.object(e.ref).position;
                if ((ep.x - p.x) * fx > 0 && std::fabs(ep.x - p.x) < 2.1f && std::fabs(ep.y - p.y) < 1.6f)
                    damage_enemy(e, 1, fx);
            }
        }

        update_bullets(dt,p,aim_now,auto_pilot || actions.held(A_SHOOT));
        update_hook(dt, p);
        update_enemies(dt, p);
        update_pickups(p);
        update_door(p);
        update_enemy_bars();
        update_zones(p);
        update_particles(dt);
        update_visuals(dt, p);
    }

    void damage_enemy(Enemy &e, int dmg, float kx) {
        if (!e.alive)
            return;
        e.hp -= dmg;
        e.flash = 0.12f;
        Vec3 ep = rt.object(e.ref).position;
        burst(ep, {1, .8f, .3f}, 8, 3.5f);
        fx_flash(ep, {1, .6f, .2f}, 0.15f); // dynamic hit light
        if (e.kind == 2) {
            e.vel.x = kx * 5;
            e.vel.y = 4;
            if (e.hp == kWardenHp - 3)
                say("The Warden staggers! Keep striking!");
            if (e.hp == 2)
                say("The Warden is nearly broken!");
        }
        if (e.hp <= 0) {
            e.alive = false;
            burst(ep, e.kind == 2 ? Vec3{1, .5f, .1f} : Vec3{.8f, .2f, .4f}, e.kind == 2 ? 40 : 12,
                  e.kind == 2 ? 7 : 4);
            rt.destroy_object(e.ref);
            if (rt.object_alive(e.bar_bg))
                rt.destroy_object(e.bar_bg);
            if (rt.object_alive(e.bar_fg))
                rt.destroy_object(e.bar_fg);
            if (e.kind == 2) {
                state = State::Win;
                win_time = rt.time();
                fx_flash(ep, {1, .8f, .3f}, 0.5f, 10);
                cam.add_shake(0.6f);
                say("VICTORY! The Warden crumbles. Time: " + std::to_string(int(win_time)) +
                    "s, deaths: " + std::to_string(deaths) + ". The Heart is yours.");
            } else {
                say(e.kind == 1 ? "Bat down!" : "Crawler crushed!");
            }
        }
    }

    void update_enemies(float dt, Vec3 pp) {
        for (auto &e : enemies) {
            if (!e.alive || !rt.object_alive(e.ref))
                continue;
            Vec3 ep = rt.object(e.ref).position;
            if (e.kind == 0) { // crawler patrol
                ep.x += e.dir * 2.2f * dt;
                if (ep.x < e.x0) {
                    ep.x = e.x0;
                    e.dir = 1;
                }
                if (ep.x > e.x1) {
                    ep.x = e.x1;
                    e.dir = -1;
                }
                rt.object(e.ref).position = ep;
                // Stomp vs hurt.
                if (overlap(pp, {.35f, .35f, 0}, ep, {.45f, .3f, 0})) {
                    if (vel.y < -1 && (pp.y - kPlayerR) > ep.y + 0.1f) {
                        damage_enemy(e, 1, 0);
                        vel.y = 8;
                        jump_cut = false;
                        grounded = false;
                    } else {
                        hurt(ep);
                    }
                }
            } else if (e.kind == 1) { // bat: sine + home
                e.t += dt;
                float dx = pp.x - ep.x, dy = (pp.y + .5f) - ep.y;
                float d = std::sqrt(dx * dx + dy * dy);
                Vec3 np = e.home;
                if (d < 8 && state == State::Play)
                    np = {ep.x + dx / (d + .01f) * 3.2f * dt, ep.y + dy / (d + .01f) * 3.2f * dt, 0};
                np.y += std::sin(e.t * 5) * .6f * dt;
                ep = {np.x, std::max(1.2f, np.y), 0};
                rt.object(e.ref).position = ep;
                if (overlap(pp, {.35f, .35f, 0}, ep, {.35f, .35f, 0})) {
                    if (vel.y < -1 && (pp.y - kPlayerR) > ep.y + 0.1f) {
                        damage_enemy(e, 1, 0);
                        vel.y = 8;
                        jump_cut = false;
                        grounded = false;
                    } else {
                        hurt(ep);
                    }
                }
            } else { // warden
                if (pp.x < 62 && !warden_announced)
                    continue; // dormant
                if (!warden_announced) {
                    warden_announced = true;
                    say("The WARDEN OF THE CORE rises! Strike it (F) or stomp it!");
                }
                e.flash = std::max(0.f, e.flash - dt);
                e.t += dt;
                float hop_cd = e.hp > kWardenHp * .5f ? 1.1f : 0.7f; // enrage
                float hop_sp = e.hp > kWardenHp * .5f ? 4.5f : 5.5f;
                // Telegraph the hop (squash) and flash white on hit. Mesh swaps
                // are gated on state change: no per-frame allocation.
                int look = e.flash > 0 ? 2 : (e.wground && e.t > hop_cd - 0.3f ? 1 : 0);
                if (look != e.look) {
                    e.look = look;
                    auto &wo = rt.object(e.ref);
                    if (look == 2)
                        wo.mesh = box({1.6f, 1.6f, 1.6f}, {1, 1, 1});
                    else if (look == 1)
                        wo.mesh = box({1.9f, 1.0f, 1.9f}, {.65f, .12f, .14f});
                    else
                        wo.mesh = box({1.6f, 1.6f, 1.6f}, {.55f, .1f, .12f});
                }
                e.vel.y = std::max(-18.f, e.vel.y - kGravity * dt);
                if (e.wground && e.t > hop_cd) {
                    e.t = 0;
                    float to = pp.x > ep.x ? 1 : -1;
                    e.vel.x = to * hop_sp;
                    e.vel.y = 7.5f;
                    e.wground = false;
                }
                auto s = slide_sphere_boxes(ep, e.vel * dt, 0.8f, solids, 4);
                ep = s.position;
                e.wground = s.contacts > 0 && s.normal.y > 0.5f;
                if (e.wground)
                    e.vel.y = 0;
                if (s.contacts > 0 && std::fabs(s.normal.x) > 0.5f)
                    e.vel.x = 0; // don't grind into pillars forever
                ep.z = 0;
                rt.object(e.ref).position = ep;
                if (overlap(pp, {.35f, .35f, 0}, ep, {.85f, .85f, 0})) {
                    if (vel.y < -1 && (pp.y - kPlayerR) > ep.y + 0.55f) {
                        damage_enemy(e, 1, pp.x < ep.x ? 1 : -1);
                        vel.y = 9;
                        jump_cut = false;
                        grounded = false;
                    } else {
                        hurt(ep);
                    }
                }
            }
        }
    }

    void update_pickups(Vec3 pp) {
        for (auto &k : pickups) {
            if (k.taken || !rt.object_alive(k.ref))
                continue;
            auto &o = rt.object(k.ref);
            o.yaw += 0.03f;
            o.position = k.base + Vec3{0, std::sin(float(rt.time()) * 3) * .15f, 0};
            // Generous magnet: fly-over grabs count (double jump era).
            if (overlap(pp, {.4f, .4f, 0}, o.position, {.6f, .6f, 0})) {
                k.taken = true;
                burst(o.position, {1, 1, .5f}, 14, 4);
                park_light_near(o.position); // recycle its glow into the fx pool
                rt.destroy_object(k.ref);
                if (k.kind == 0) {
                    has_double = true;
                    say("MOON SIGIL acquired! Jump again in mid-air. The east wall can be climbed!");
                } else if (k.kind == 1) {
                    has_key = true;
                    say("BRASS KEY acquired! The foundry gate will know it.");
                } else if (k.kind == 3) {
                    has_climb = true;
                    say("CLIMBING CLAWS acquired! Touch walls while falling to grab, Space to mantle.");
                } else {
                    has_grapple = true;
                    say("GRAPNEL MOSS acquired! Press G: anchors yank you, hooked enemies bleed.");
                }
            }
        }
    }

    void update_door(Vec3 pp) {
        if (door_open || !has_key)
            return;
        if (std::fabs(pp.x - door_pos.x) < 3.5f && pp.y < 7) {
            door_open = true;
            solids.erase(solids.begin() + ptrdiff_t(door_solid));
            auto &g = rt.object(door_ref); // gate sinks to rubble instead of vanishing
            g.mesh = box({1.0f, .4f, 2.4f}, {.35f, .35f, .4f});
            g.position = {door_pos.x, .2f, 0};
            burst(door_pos, {1, .8f, .2f}, 16, 4);
            say("The Brass Key turns on its own... the gate sinks. East lies the Core.");
        }
    }

    void update_enemy_bars() {
        for (auto &e : enemies) {
            if (!e.alive)
                continue;
            // Shown once damaged (1-HP foes die outright, so only the
            // Warden really wears one; the Warden's hides until provoked).
            bool show = e.hp < e.maxhp && (e.kind != 2 || warden_announced);
            Vec3 top =
                show ? rt.object(e.ref).position + Vec3{0, e.half.y + 0.45f, 0} : Vec3{0, -99, 0};
            rt.object(e.bar_bg).position = top;
            if (!show) {
                rt.object(e.bar_fg).position = {0, -99, 0};
                continue;
            }
            int h = std::max(0, int(e.hp));
            if (h != e.bar_shown) {
                e.bar_shown = h;
                float frac = h / float(e.maxhp);
                Vec3 col = frac > 0.5f ? Vec3{.25f, .9f, .35f}
                                       : frac > 0.25f ? Vec3{.95f, .8f, .25f} : Vec3{.9f, .2f, .2f};
                rt.object(e.bar_fg).mesh = box({(e.bar_w - .2f) * frac + 0.001f, .1f, .1f}, col);
            }
            float w = (e.bar_w - .2f) * std::max(0.f, e.hp / float(e.maxhp));
            rt.object(e.bar_fg).position = top + Vec3{-e.bar_w / 2 + w / 2, 0, 0};
        }
    }

    void update_zones(Vec3 pp) {
        if (pp.x > 60 && zone < 2) {
            zone = 2;
            checkpoint = {60, 1, 0};
            say("--- Warden's Core --- (checkpoint)");
        } else if (pp.x > 27 && zone < 1) {
            zone = 1;
            checkpoint = {28, 1, 0};
            say("--- Sunken Foundry --- (checkpoint) Mind the molten veins.");
        }
    }

    void update_visuals(float dt, Vec3 pp) {
        (void)dt;
        // Player look: blink on iframes (mesh swap: active=false would mean
        // "destroyed slot" to the Runtime) + squash while sliding. Gated.
        blink_t += dt;
        bool show = !(iframes > 0 && int(blink_t * 12) % 2 == 0);
        auto &pl = rt.object(player);
        pl.active = true;
        const Vec3 pcol{1.0f, .55f, .15f};
        int pkey = (show ? 4 : 0) + (slide_t > 0 ? 2 : 0) + (grabbing ? 1 : 0);
        if (pkey != last_pmesh) {
            last_pmesh = pkey;
            pl.mesh = !show ? box({.7f, .06f, .7f}, pcol)
                    : (slide_t > 0 || grabbing) ? box({.85f, .35f, .7f}, pcol)
                                                : box({.7f, .7f, .7f}, pcol);
        }
        // Camera follows with lookahead (engine FollowCam: smoothed + bounds).
        float look = facing_right ? 1.5f : -1.5f;
        cam.update({pp.x + look, pp.y + .5f, 0}, dt);
        rt.camera = cam.camera({0, 1.6f, -9.5f}, float(rt.time()));
        // HUD rides in front of the camera. Positions update every frame
        // (cheap); meshes rebuild only on value change (no per-frame alloc).
        Vec3 dir = normalized(rt.camera.target - rt.camera.position);
        Vec3 right = normalized(cross(dir, {0, 1, 0}));
        Vec3 up = cross(right, dir);
        Vec3 anchor = rt.camera.target + dir * 2.0f + up * 1.5f;
        rt.object(bar_bg).position = anchor;
        if (hp != bar_shown) {
            bar_shown = hp;
            float frac = std::max(0.f, hp / float(kMaxHp));
            Vec3 col = frac > 0.5f ? Vec3{.25f, .9f, .35f}
                                   : frac > 0.25f ? Vec3{.95f, .8f, .25f} : Vec3{.9f, .2f, .2f};
            rt.object(bar_fg).mesh = box({2.2f * frac + 0.001f, .2f, .1f}, col);
        }
        float frac = std::max(0.f, hp / float(kMaxHp));
        rt.object(bar_fg).position = anchor + right * (-1.1f + (2.2f * frac) * .5f);
        bool want_icons[4] = {has_double, has_key, has_grapple, has_climb};
        Vec3 icon_on[4] = {{.2f, .9f, 1}, {1, .8f, .2f}, {.45f, 1, .4f}, {.75f, .35f, .95f}};
        ObjectRef icon_ref[4] = {icon_double, icon_key, icon_hook, icon_climb};
        for (int i = 0; i < 4; i++) {
            rt.object(icon_ref[i]).position = anchor + right * (1.55f + i * .35f);
            if (want_icons[i] != icons_shown[i]) {
                icons_shown[i] = want_icons[i];
                rt.object(icon_ref[i]).mesh =
                    box({.24f, .24f, .24f}, want_icons[i] ? icon_on[i] : Vec3{.2f, .2f, .25f});
            }
        }
        // Rope: segments strung player -> hook (flying) or fading at last hit.
        bool rope_on = hook == Hook::Flying || rope_t > 0;
        Vec3 far = hook == Hook::Flying ? hook_pos : rope_b;
        for (size_t i = 0; i < rope_seg.size(); i++) {
            auto &s = rt.object(rope_seg[i]);
            if (!rope_on) {
                s.position = {0, -99, 0};
                continue;
            }
            float t = (i + .5f) / rope_seg.size();
            s.position = {pp.x + (far.x - pp.x) * t, pp.y + (far.y - pp.y) * t, 0};
        }
        rt.object(hook_ref).position = hook == Hook::Flying ? hook_pos : Vec3{0, -99, 0};
        // Aim guide: live dots along the hook line, colored by target.
        // Grey = open rock line, gold = anchor lock, red = foe lock.
        bool flying = hook == Hook::Flying;
        int kind = 0;
        Vec3 hdir = flying ? hook_dir : aim_assist(pp, aim_now, kind);
        float reach = kHookRange;
        if (!flying && kind == 0) {
            for (float d = 1.0f; d < kHookRange; d += 0.6f) {
                if (point_in_solids(pp + hdir * d, 0.1f)) {
                    reach = d;
                    break;
                }
            }
        }
        int dlook = flying ? 3 : kind;
        if (dlook != aim_look) {
            aim_look = dlook;
            Vec3 col =
                dlook == 1 ? Vec3{1.0f, .85f, .3f} : dlook == 2 ? Vec3{1.0f, .3f, .3f} : Vec3{.5f, .5f, .55f};
            for (auto &s : aim_dots)
                rt.object(s).mesh = box({.12f, .12f, .12f}, col);
        }
        float gap = std::min(1.2f, reach / 6);
        for (size_t i = 0; i < aim_dots.size(); i++) {
            auto &s = rt.object(aim_dots[i]);
            s.position = flying ? Vec3{0, -99, 0} : pp + hdir * (0.9f + i * gap);
        }
    }
};
} // namespace

#ifndef EMBERFALL_TESTING
int main(int argc, char **argv) {
#ifdef EMBER_VIEWER
    SDL_SetMainReady();
#endif
    try {
        std::string config = "config/engine.toml", output, profile = "ps1", backend = "auto";
        int frames = 0, shot_every = 0, trace_every = 0, bench = 0;
        bool window = false, auto_pilot = false, god = false, give_dbl = false, give_key = false,
             give_hook = false, give_climb = false, force_sprint = false;
        bool expect_win = false;
        float expect_min_x = -std::numeric_limits<float>::infinity();
        float expect_max_x = std::numeric_limits<float>::infinity();
        bool has_tp = false;
        Vec3 tp{};
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
            else if (arg == "--out")
                output = value();
            else if (arg == "--frames")
                frames = std::stoi(value());
            else if (arg == "--shot-every")
                shot_every = std::stoi(value());
            else if (arg == "--bench")
                bench = std::stoi(value());
            else if (arg == "--trace")
                trace_every = std::stoi(value());
            else if (arg == "--window")
                window = true;
            else if (arg == "--auto")
                auto_pilot = true;
            else if (arg == "--sprint")
                force_sprint = true; // autopilot sprints (stability test at speed)
            else if (arg == "--god")
                god = true;
            else if (arg == "--give-dbl")
                give_dbl = true;
            else if (arg == "--give-key")
                give_key = true;
            else if (arg == "--give-grapple")
                give_hook = true;
            else if (arg == "--give-climb")
                give_climb = true;
            else if (arg == "--expect-win")
                expect_win = true;
            else if (arg == "--expect-min-x")
                expect_min_x = std::stof(value());
            else if (arg == "--expect-max-x")
                expect_max_x = std::stof(value());
            else if (arg == "--teleport") {
                std::string v = value();
                auto comma = v.find(',');
                if (comma == std::string::npos)
                    throw std::runtime_error("--teleport needs x,y");
                tp = {std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1)), 0};
                has_tp = true;
            }
            else if (arg == "--help") {
                std::cout << "EMBERFALL — a tiny metroidvania (PS1 profile)\n--window | --frames N --out shot.ppm "
                             "[--shot-every K] [--auto] [--sprint] [--god] [--profile ps1|pixel|n64|normal]\n"
                             "[--backend auto|cpu|opengl] [--give-dbl] [--give-key] [--give-climb] [--teleport x,y] [--trace N]\n"
                             "CI assertions: [--expect-win] [--expect-min-x X] [--expect-max-x X]\n"
                             "Keys: A/D/arrows move, Shift sprint, Space jump (hold = higher), F/J attack,\n"
                             "      S slide (momentum; Space cancels into a leap), G grapple + direction,\n"
                             "      G grapple + held direction to aim (8-way; neutral = ahead).\n"
                             "      Dots preview the line: grey rock, gold anchor (full yank),\n"
                             "      red foe (mutual yank + damage). One hook per airtime.\n"
                             "      P pause, R respawn, Esc quit\n";
                return 0;
            } else
                throw std::runtime_error("Unknown argument: " + arg);
        }
        Config cfg = Config::load(config);
        if (!backend.empty())
            cfg.backend = backend; // auto = Vulkan, then OpenGL, then CPU
        cfg.profile = parse_profile(profile); // default ps1: the retro look
        Runtime runtime(cfg);
        Game game(runtime);
        game.auto_pilot = auto_pilot;
        game.auto_sprint = force_sprint;
        game.god = god;
        game.build_world();
        if (give_dbl) {
            game.has_double = true;
            game.say("(debug) Moon Sigil granted.");
        }
        if (give_key) {
            game.has_key = true;
            game.say("(debug) Brass Key granted.");
        }
        if (give_hook) {
            game.has_grapple = true;
            game.say("(debug) Grapnel granted.");
        }
        if (give_climb) {
            game.has_climb = true;
            game.say("(debug) Climbing claws granted.");
        }
        if (has_tp) {
            runtime.object(game.player).position = tp;
            game.checkpoint = tp;
            game.say("(debug) Teleported.");
        }
        runtime.on_event = [&](Runtime &r, const Event &e) {
            if (e.type == Event::Type::FixedUpdate && !game.paused) {
                game.update(float(r.config.fixed_delta()));
                game.actions.next_frame(); // Consume edges exactly once per simulation step.
            }
            (void)r;
        };
        runtime.start();
        auto renderer = make_backend(runtime.config.backend, runtime.config.worker_threads);
        SceneRenderer prepare;
        auto render_once = [&]() {
            game.sync_vfx();
            prepare.prepare(runtime.scene, runtime.camera, runtime.config, runtime.frame);
            renderer->draw(runtime.frame);
            return &renderer->readback();
        };
        auto scene_stats = [&]() {
            size_t objs = 0, tris = 0;
            for (auto &o : runtime.scene.objects)
                if (o.active) {
                    objs++;
                    tris += o.mesh.triangles.size();
                }
            return std::pair(objs, tris);
        };
        if (bench > 0) {
            if (bench < 1 || bench > 100000)
                throw std::runtime_error("--bench N needs 1..100000");
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < bench; i++)
                runtime.tick(1.0 / runtime.config.fixed_hz);
            double sim_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
                bench;
            std::vector<double> ms;
            ms.reserve(size_t(bench));
            for (int i = 0; i < bench; i++) {
                auto f0 = std::chrono::steady_clock::now();
                runtime.tick(1.0 / runtime.config.fixed_hz);
                render_once();
                ms.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count());
            }
            std::sort(ms.begin(), ms.end());
            double mean = 0;
            for (double v : ms)
                mean += v;
            mean /= ms.size();
            auto [objs, tris] = scene_stats();
            std::cout << "bench: frames=" << bench << " sim_ms=" << sim_ms << " frame_mean_ms=" << mean
                      << " frame_p95_ms=" << ms[size_t(ms.size() * .95)] << " objects=" << objs
                      << " triangles=" << tris << "\n";
            return 0;
        }
        if (window) {
#ifdef EMBER_VIEWER
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                throw std::runtime_error(SDL_GetError());
            int vw = 960, vh = 540;
            SDL_Window *win =
                SDL_CreateWindow("EMBERFALL — a tiny metroidvania", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, vw, vh, SDL_WINDOW_RESIZABLE);
            if (!win)
                throw std::runtime_error(SDL_GetError());
            SDL_RaiseWindow(win); // measure/paint at full rate, not background-throttled
            {
                SDL_DisplayMode dm{};
                int di = SDL_GetWindowDisplayIndex(win);
                if (SDL_GetCurrentDisplayMode(di, &dm) == 0)
                    std::cout << "video=" << SDL_GetCurrentVideoDriver() << " display=" << dm.w << "x"
                              << dm.h << "@" << dm.refresh_rate << "Hz\n";
            }
            // GPU texture upload. NOTE: no PRESENTVSYNC on purpose — on a
            // Wayland compositor there is no tearing anyway, and a blocking
            // vsync present here measured a steady ~2 vblanks of latency
            // (metronomic 32 ms on 74 Hz). Manual pacing below is exact.
            SDL_Renderer *vr = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
            if (vr) {
                SDL_RendererInfo info{};
                SDL_GetRendererInfo(vr, &info);
                std::cout << "present: " << info.name
                          << ((info.flags & SDL_RENDERER_PRESENTVSYNC) ? " vsync\n" : " no-vsync\n");
            } else {
                std::cout << "present: software blit fallback (" << SDL_GetError() << ")\n";
            }
            SDL_Texture *vtex = nullptr;
            int texw = 0, texh = 0;
            std::vector<uint32_t> composite;
            bool running = true;
            auto previous = std::chrono::steady_clock::now();
            // Locked 60 Hz render cadence on GPU (~6 ms/frame); the sim below
            // keeps its own 60 Hz fixed steps via tick(). Slow machines drop
            // to ~30 automatically via the backlog clamp (no spiral).
            constexpr double kRenderDt = 1.0 / 60.0;
            LoopTimer loop;
            loop.fixed_dt = 1.0 / runtime.config.fixed_hz;
            loop.render_dt = kRenderDt;
            double fps_t = 0;
            int fps_n = 0;
            double stat_t = 0, stat_sum = 0, stat_max = 0; // present pacing log
            int stat_n = 0;
            SDL_Event e{};
            std::cout << "A/D move, Shift sprint, S slide, Space jump (hold = higher), F attack, K/X shoot, G grapple, P pause, R respawn, Esc quit.\n"
                         "profile=" << profile_name(cfg.profile) << "\n";
            while (running) {
                auto fs = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(fs - previous).count();
                previous = fs;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT)
                        running = false;
                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                        game.actions.release_all();
                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        vw = std::max(1, e.window.data1);
                        vh = std::max(1, e.window.data2);
#ifdef EMBER_RMLUI
                        if (game.ui)
                            game.ui->resize(vw, vh);
#endif
                    }
                    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                        int k = e.key.keysym.sym;
                        game.actions.key_down(k);
                        if (k == SDLK_ESCAPE)
                            running = false;
                        if (k == SDLK_p) {
                            game.paused = !game.paused;
                            game.actions.release_all();
                            game.actions.next_frame();
                            std::cout << (game.paused ? "Paused.\n" : "Resumed.\n");
                        }
                        if (k == SDLK_r) {
                            game.respawn();
                            std::cout << "Returned to checkpoint.\n";
                        }
                        if (k == SDLK_F3)
                            game.toggle_fps(vw, vh);
                    }
                    if (e.type == SDL_KEYUP)
                        game.actions.key_up(e.key.keysym.sym);
                }
                LoopTimer::Tick step = loop.update(dt);
                if (!game.paused)
                    for (int i = 0; i < step.steps; i++)
                        runtime.tick(1.0 / runtime.config.fixed_hz);
                fps_t += dt;
                bool do_render = step.render;
                if (do_render) {
                    const auto *px = render_once();
                    int w = runtime.frame.width, h = runtime.frame.height;
                    composite.resize(size_t(vw) * vh);
                    float sc = std::min(float(vw) / w, float(vh) / h);
                    int dw = std::max(1, int(w * sc)), dh = std::max(1, int(h * sc));
                    int ox = (vw - dw) / 2, oy = (vh - dh) / 2;
                    std::fill(composite.begin(), composite.end(), 0xff100c09u);
                    for (int y = 0; y < dh; y++)
                        for (int x = 0; x < dw; x++)
                            composite[size_t(y + oy) * vw + x + ox] =
                                (*px)[size_t(std::min(h - 1, int(y / sc))) * w + std::min(w - 1, int(x / sc))];
#ifdef EMBER_RMLUI
                    if (game.fps_visible && game.ui) {
                        const auto &ov = game.ui->pixels();
                        if (ov.size() == composite.size()) {
                            for (size_t i = 0; i < ov.size(); i++) {
                                uint32_t p = ov[i], a = p >> 24;
                                if (!a)
                                    continue;
                                uint32_t dest = composite[i], out = 0xff000000u;
                                for (int k = 0; k < 3; k++) {
                                    uint32_t v = ((p >> (8 * k)) & 255) +
                                                 (((dest >> (8 * k)) & 255) * (255 - a) + 127) / 255;
                                    out |= std::min(255u, v) << (8 * k);
                                }
                                composite[i] = out;
                            }
                        }
                    }
#endif
                    if (vr) {
                        if (!vtex || texw != vw || texh != vh) {
                            if (vtex)
                                SDL_DestroyTexture(vtex);
                            vtex = SDL_CreateTexture(vr, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                                     vw, vh);
                            texw = vw;
                            texh = vh;
                        }
                        if (vtex) {
                            SDL_UpdateTexture(vtex, nullptr, composite.data(), vw * 4);
                            SDL_RenderClear(vr);
                            SDL_RenderCopy(vr, vtex, nullptr, nullptr);
                            SDL_RenderPresent(vr);
                        }
                    } else {
                        SDL_Surface *surface = SDL_GetWindowSurface(win);
                        if (surface) {
                            SDL_Surface *image = SDL_CreateRGBSurfaceWithFormatFrom(
                                composite.data(), vw, vh, 32, vw * 4, SDL_PIXELFORMAT_RGBA32);
                            SDL_BlitSurface(image, nullptr, surface, nullptr);
                            SDL_FreeSurface(image);
                            SDL_UpdateWindowSurface(win);
                        }
                    }
                    if (++fps_n >= 30) {
                        int fps = int(fps_n / fps_t + .5);
                        char title[64];
                        std::snprintf(title, sizeof title, "EMBERFALL — %d fps", fps);
                        SDL_SetWindowTitle(win, title);
                        game.push_fps(fps);
                        fps_t = 0;
                        fps_n = 0;
                    }
                }
                // Manual pacing at the cadence (presents don't block here).
                if (step.sleep_ms > 0)
                    SDL_Delay(Uint32(step.sleep_ms));
                stat_n++;
                stat_sum += dt * 1000;
                if (dt * 1000 > stat_max)
                    stat_max = dt * 1000;
                stat_t += dt;
                if (stat_t >= 5) {
                    std::cout << "present: avg=" << stat_sum / stat_n << "ms max=" << stat_max << "ms n="
                              << stat_n << "\n";
                    stat_t = 0;
                    stat_n = 0;
                    stat_sum = 0;
                    stat_max = 0;
                }
            }
            if (vtex)
                SDL_DestroyTexture(vtex);
            if (vr)
                SDL_DestroyRenderer(vr);
            SDL_DestroyWindow(win);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
#else
            throw std::runtime_error("Viewer was not compiled");
#endif
        } else {
            if (frames < 1 || frames > 100000)
                throw std::runtime_error("--frames N is required in headless mode");
            float maximum_x = runtime.object(game.player).position.x;
            for (int i = 0; i < frames; i++) {
                runtime.tick(1.0 / runtime.config.fixed_hz);
                const float player_x = runtime.object(game.player).position.x;
                maximum_x = std::max(maximum_x, player_x);
                if (trace_every > 0 && i % trace_every == 0) {
                    const auto &tp = runtime.object(game.player).position;
                    std::cout << "trace t=" << (i / 60) << "s pos=(" << tp.x << "," << tp.y << ") g="
                              << game.grounded << " vy=" << game.vel.y << " hp=" << game.hp
                              << " air_stuck=" << game.air_stuck << " used_dbl=" << game.used_double
                              << " hold=" << game.auto_hold << "\n";
                }
                if (shot_every > 0 && i % shot_every == 0) {
                    const auto *px = render_once();
                    std::ostringstream s;
                    s << output << '-' << std::setw(5) << std::setfill('0') << i << ".ppm";
                    write_ppm(s.str(), runtime.frame.width, runtime.frame.height, *px);
                }
            }
            if (!output.empty()) {
                const auto *px = render_once();
                write_ppm(output, runtime.frame.width, runtime.frame.height, *px);
            }
            const auto &pp = runtime.object(game.player).position;
            std::cout << "headless: frames=" << frames << " player=(" << pp.x << "," << pp.y << ") hp=" << game.hp
                      << " double=" << game.has_double << " key=" << game.has_key << " grapple=" << game.has_grapple
                      << " zone=" << game.zone << " deaths=" << game.deaths << " win=" << (game.state == Game::State::Win)
                      << " hooks=" << game.hooks_fired << " anchors=" << game.anchors_hit
                      << " yanks=" << game.enemies_yanked
                      << " jumps g=" << game.dbg_ground << " air=" << game.dbg_air
                      << " slides=" << game.dbg_slides << " mantles=" << game.dbg_mantles << "\n";
            if (expect_win && game.state != Game::State::Win)
                throw std::runtime_error("EMBERFALL assertion failed: campaign did not reach victory");
            if (maximum_x < expect_min_x)
                throw std::runtime_error("EMBERFALL assertion failed: expected forward progress was not reached");
            if (maximum_x > expect_max_x)
                throw std::runtime_error("EMBERFALL assertion failed: a progression gate was crossed");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
#endif
