#pragma once
#include "ember/render.hpp"
struct SDL_Window;
namespace ember::detail {
// Platform-private factories. The caller owns the window and outlives Backend.
std::unique_ptr<Backend> make_opengl_window(SDL_Window *, bool vsync, bool nearest);
}
