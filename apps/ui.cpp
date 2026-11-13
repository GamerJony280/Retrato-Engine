#include "ui.hpp"
#include <SDL.h>

DemoUi::DemoUi(ember::Config &config, const char *rml, const char *font, int width, int height,
               bool &paused)
    : config_(config), paused_(paused), overlay_(rml, font, width, height) {
    overlay_.set_click_handler([this](const std::string &id) {
        if (id == "ps1")
            config_.profile = ember::Profile::PS1;
        else if (id == "n64")
            config_.profile = ember::Profile::N64;
        else if (id == "pixel")
            config_.profile = ember::Profile::Pixel;
        else if (id == "normal")
            config_.profile = ember::Profile::Normal;
        else if (id == "crt")
            config_.filter = config_.filter == "crt" ? "normal" : "crt";
        else if (id == "pause")
            paused_ = !paused_;
        overlay_.invalidate();
    });
}
DemoUi::~DemoUi() = default;
void DemoUi::resize(int width, int height) { overlay_.resize(width, height); }
void DemoUi::invalidate() { overlay_.invalidate(); }
void DemoUi::set_text(const char *id, const std::string &text) { overlay_.set_text(id, text); }
void DemoUi::set_visible(bool visible) { overlay_.set_visible(visible); }
void DemoUi::event(const SDL_Event &event) {
    switch (event.type) {
    case SDL_MOUSEMOTION:
        overlay_.mouse_move(event.motion.x, event.motion.y);
        break;
    case SDL_MOUSEBUTTONDOWN:
        overlay_.mouse_button_down(event.button.button == SDL_BUTTON_LEFT ? 0 : 1);
        break;
    case SDL_MOUSEBUTTONUP:
        overlay_.mouse_button_up(event.button.button == SDL_BUTTON_LEFT ? 0 : 1);
        break;
    default:
        break;
    }
}
const std::vector<uint32_t> &DemoUi::pixels() {
    const std::string label = std::string(ember::profile_name(config_.profile)) + " / " + config_.filter +
                              (paused_ ? " / paused" : " / live");
    if (label != last_label_) {
        overlay_.set_text("status", label);
        last_label_ = label;
    }
    return overlay_.pixels();
}
size_t DemoUi::redraws() const { return overlay_.redraws(); }
