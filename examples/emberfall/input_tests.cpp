// Exercise the example's actual game policy without a window or autopilot.
#define EMBERFALL_TESTING
#include "main.cpp"

int main() {
    try {
        Runtime runtime{Config{}};
        Game game(runtime);
        game.build_world();
        game.solids.clear();
        game.enemies.clear();
        game.pickups.clear();
        game.actions.bind(Game::A_JUMP, 1);
        const float dt = float(runtime.config.fixed_delta());
        auto check = [](bool valid, const char *message) {
            if (!valid) throw std::runtime_error(message);
        };
        auto reset = [&] {
            runtime.object(game.player).position = {0, 3, 0};
            game.vel = {};
            game.body = CharacterBody{};
            game.body.radius = kPlayerR;
            game.grounded = false;
            game.jump_cut = false;
            game.used_double = false;
            game.boost_x = 0;
            game.hook = Game::Hook::Idle;
            game.actions.release_all();
            game.actions.next_frame();
        };
        reset();
        game.anchors.resize(1);
        game.anchors[0].pos = {0, 7, 0};
        game.jump_cut = true; // A previous jump must not claim the new hook impulse.
        game.fire_hook(runtime.object(game.player).position, {0, 1, 0});
        for (int i = 0; i < 60 && game.hook == Game::Hook::Flying; ++i)
            game.update_hook(dt, runtime.object(game.player).position);
        check(game.anchors_hit == 1 && game.vel.y == kHookImpulse, "gold hook retains original impulse");
        game.update(dt); // Space is not held.
        check(std::abs(game.vel.y - (kHookImpulse - kGravity * dt)) < .001f,
              "gold impulse is only affected by gravity, not jump release");

        reset();
        game.anchors.clear();
        game.solids = {aabb_of(-1, 7, 1, 8, 2)};
        game.fire_hook(runtime.object(game.player).position, {0, 1, 0});
        for (int i = 0; i < 60 && game.hook == Game::Hook::Flying; ++i)
            game.update_hook(dt, runtime.object(game.player).position);
        check(game.vel.y == 8.f, "rock keeps its progression-safe impulse cap");
        game.update(dt);
        check(std::abs(game.vel.y - (8.f - kGravity * dt)) < .001f, "rock impulse survives without Space");

        reset();
        game.solids.clear();
        game.grounded = game.body.grounded = true;
        game.actions.key_down(1);
        game.update(dt);
        check(game.vel.y > 5.f, "held jump retains full ascent");
        game.actions.next_frame();
        game.actions.key_up(1);
        game.update(dt);
        check(game.vel.y == 5.f && !game.jump_cut, "releasing Space still shortens the normal jump");

        reset();
        game.grounded = game.body.grounded = true;
        game.actions.key_down(1);
        game.actions.key_up(1);
        game.update(dt);
        check(game.vel.y == 5.f, "a complete tap between steps remains a short jump");

        game.actions.next_frame();
        game.actions.bind(Game::A_ATTACK, 2);
        game.actions.key_down(2);
        game.update(dt);
        check(game.slash_t == kSlashT, "attack starts the complete slash duration");
        game.actions.next_frame();
        game.actions.key_up(2);
        game.update(dt);
        check(std::abs(game.slash_t - (kSlashT - dt)) < .00001f,
              "slash lifetime advances once per fixed step");
#ifdef EMBER_HAS_VFX
        check(runtime.object_alive(game.slash_ref), "the VFX module owns a live slash");
        for (int i = 0; i < 20; ++i) game.update(dt);
        check(game.timed_fx.alive() == 0, "the VFX module retires the slash");
#endif
        std::cout << "EMBERFALL native input regressions passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
