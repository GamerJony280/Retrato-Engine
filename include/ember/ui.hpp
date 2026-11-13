#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ember {

// Optional retained UI surface backed by RmlUi and Ember's small CPU painter.
// It has no SDL types in its public API: a platform adapter only forwards mouse
// coordinates and buttons. RmlUi currently uses process-global interfaces, so
// Ember intentionally permits one live UiOverlay at a time.
class UiOverlay {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    using ClickHandler = std::function<void(const std::string &element_id)>;

    UiOverlay(const std::string &rml_file, const std::string &font_file, int width, int height);
    ~UiOverlay();
    UiOverlay(const UiOverlay &) = delete;
    UiOverlay &operator=(const UiOverlay &) = delete;

    void resize(int width, int height);
    void invalidate();
    void set_click_handler(ClickHandler handler);
    void mouse_move(int x, int y);
    void mouse_button_down(int button);
    void mouse_button_up(int button);
    void set_text(const std::string &id, const std::string &text);
    void set_markup(const std::string &id, const std::string &rml);
    void set_visible(bool visible);
    bool visible() const;
    const std::vector<uint32_t> &pixels();
    size_t redraws() const;
};

} // namespace ember
