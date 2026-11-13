#include "ui.hpp"
#include <SDL.h>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
#ifdef EMBER_VIEWER
    SDL_SetMainReady();
#endif
    try {
        if (argc != 2)
            throw std::runtime_error("Supply source path");
        std::string root = argv[1];
        ember::Config config;
        bool paused = false;
        DemoUi ui(config, (root + "/assets/menu.rml").c_str(), (root + "/assets/ui.ttf").c_str(), 960, 540,
                  paused);
        const auto &image = ui.pixels();
        if (image.size() != 960 * 540)
            throw std::runtime_error("Wrong UI dimensions");
        auto redraws = ui.redraws();
        for (int i = 0; i < 10; i++)
            ui.pixels();
        if (ui.redraws() != redraws)
            throw std::runtime_error("Static UI redrawn unnecessarily");
        ui.set_text("status", "safe <text> & value");
        ui.pixels();
        if (ui.redraws() == redraws)
            throw std::runtime_error("Text update did not invalidate UI");
        ui.set_visible(false);
        const auto &hidden = ui.pixels();
        for (auto pixel : hidden)
            if (pixel != 0)
                throw std::runtime_error("Hidden UI still paints pixels");
        ui.set_visible(true);
        SDL_Event event{};
        event.type = SDL_MOUSEMOTION;
        event.motion.x = 90;
        event.motion.y = 181;
        ui.event(event);
        ui.pixels();
        event.type = SDL_MOUSEBUTTONDOWN;
        event.button.button = SDL_BUTTON_LEFT;
        ui.event(event);
        ui.pixels();
        event.type = SDL_MOUSEBUTTONUP;
        ui.event(event);
        ui.pixels();
        if (config.profile != ember::Profile::PS1)
            throw std::runtime_error("PS1 button failed");
        ui.resize(800, 600);
        if (ui.pixels().size() != 800 * 600)
            throw std::runtime_error("Resize failed");
        bool second_rejected = false;
        try {
            ember::UiOverlay second(root + "/assets/menu.rml", root + "/assets/ui.ttf", 32, 32);
        } catch (const std::runtime_error &) {
            second_rejected = true;
        }
        if (!second_rejected)
            throw std::runtime_error("Second process-global RmlUi overlay was not rejected");
        std::cout << "PASS: RML ownership, layout, cache, text, visibility, click, resize\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
