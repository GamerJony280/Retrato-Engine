#pragma once
#include "ember/config.hpp"
#include "ember/ui.hpp"
#include <cstdint>
#include <memory>
#include <vector>
union SDL_Event;
class DemoUi {
    ember::Config &config_;
    bool &paused_;
    ember::UiOverlay overlay_;
    std::string last_label_;

  public:
    DemoUi(ember::Config &, const char *rml, const char *font, int width, int height, bool &paused);
    ~DemoUi();
    void event(const SDL_Event &);
    void resize(int width, int height);
    void invalidate();
    void set_text(const char *id, const std::string &text);
    void set_visible(bool visible);
    const std::vector<uint32_t> &pixels();
    size_t redraws() const;
};
