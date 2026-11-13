#pragma once
#include "config.hpp"
#include "game.hpp"
#include "platform.hpp"
#include <functional>
namespace ember::app {
struct Options {
    Config graphics;
    std::string organization="Retrato",name="Game";
    std::filesystem::path content;
    bool headless=false,uncapped=false;
    int frames=0; // simulation steps, 0 = until close (requires a window)
};
struct Callbacks {
    std::function<void(game::Host &,const platform::Paths &)> start;
    std::function<void(game::Host &,float)> update;
    // phase: 0 down, 1 motion, 2 up; coordinates normalized to the view.
    // Multitouch IDs are preserved; touch mapping belongs to the game.
    std::function<void(game::Host &,int64_t,int,float,float)> touch;
    std::function<void(game::Host &)> suspend; // clear game-owned gestures on focus loss
    std::function<bool()> running; // optional game-controlled exit/menu policy
};
// Owns platform/window/pacing only; simulation and assets remain in Host.
// Errors propagate as C++ exceptions; the executable entry owns reporting.
// Native C++ may provide its existing world (VFX/UI/custom engine services).
// Its Config is authoritative; Nova/Lua still receive only their public APIs.
int run(const Options &,const Callbacks &,std::shared_ptr<Runtime> native_world = {});
}
