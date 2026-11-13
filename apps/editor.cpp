// Small native scene preview built on the public game::Host API.
// SDL2 is already needed by EMBER_VIEWER. The CPU viewport and 8x8
// font keep this standalone; generated Nova modules need native linking.
//
// Build with EMBER_VIEWER=ON; no GPU, Lua or RmlUi is needed by this tool.
// A headless screenshot only needs --out path.ppm and --headless.
#ifdef EMBER_VIEWER
#include <SDL.h>
#endif
#include "ember/config.hpp"
#include "ember/game.hpp"
#include "ember/render.hpp"
#include "ember/save.hpp"
#include "ember/scene.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
// --- Embedded 8x8 font (public-domain style block glyphs, legible subset) ---
// Each glyph: 8 rows, bit 7 = left pixel. Only printable ASCII is covered;
// unknown chars render as blank.
struct Glyph {
    char ch;
    uint8_t rows[8];
};
const Glyph kFont[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0, 0}}, {'!', {0x18, 0x18, 0x18, 0x18, 0x18, 0, 0x18, 0}},
    {'"', {0x66, 0x66, 0x66, 0, 0, 0, 0, 0}}, {'#', {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0}},
    {'%', {0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0, 0}}, {'(', {0x0E, 0x1C, 0x38, 0x38, 0x38, 0x1C, 0x0E, 0}},
    {')', {0x70, 0x38, 0x1C, 0x1C, 0x1C, 0x38, 0x70, 0}}, {'*', {0, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0, 0}},
    {'+', {0, 0x18, 0x18, 0x7E, 0x18, 0x18, 0, 0}}, {',', {0, 0, 0, 0, 0x18, 0x18, 0x30, 0}},
    {'-', {0, 0, 0, 0x7E, 0, 0, 0, 0}}, {'.', {0, 0, 0, 0, 0, 0x18, 0x18, 0}},
    {'/', {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0}}, {'0', {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0}},
    {'1', {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0}}, {'2', {0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0}},
    {'3', {0x7E, 0x0C, 0x18, 0x0C, 0x06, 0x66, 0x3C, 0}}, {'4', {0x0C, 0x1C, 0x3C, 0x6C, 0xFE, 0x0C, 0x0C, 0}},
    {'5', {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0}}, {'6', {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0}},
    {'7', {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0}}, {'8', {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0}},
    {'9', {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0}}, {':', {0, 0x18, 0x18, 0, 0, 0x18, 0x18, 0}},
    {'<', {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0}}, {'=', {0, 0, 0x7E, 0, 0x7E, 0, 0, 0}},
    {'>', {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0}}, {'?', {0x3C, 0x66, 0x06, 0x0C, 0x18, 0, 0x18, 0}},
    {'A', {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0}}, {'B', {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0}},
    {'C', {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0}}, {'D', {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0}},
    {'E', {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0}}, {'F', {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0}},
    {'G', {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0}}, {'H', {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0}},
    {'I', {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0}}, {'J', {0x0E, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C, 0}},
    {'K', {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0}}, {'L', {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0}},
    {'M', {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0}}, {'N', {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0}},
    {'O', {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0}}, {'P', {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0}},
    {'Q', {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0}}, {'R', {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0}},
    {'S', {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0}}, {'T', {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0}},
    {'U', {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0}}, {'V', {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0}},
    {'W', {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0}}, {'X', {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0}},
    {'Y', {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0}}, {'Z', {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0}},
    {'[', {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0}}, {']', {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 0, 0xFF}}, {'|', {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0}},
};
const uint8_t *glyph_rows(char c) {
    if (c >= 'a' && c <= 'z')
        c = char(c - 'a' + 'A');
    for (const auto &g : kFont)
        if (g.ch == c)
            return g.rows;
    return nullptr;
}

using ember::game::Entity;
using ember::game::Host;

struct Editor {
    Host host;
    std::vector<std::string> names; // registry; Host has lookup but no enumeration
    int selected = -1;
    bool playing = false;
    std::string log = "Ready. N=box F=floor Del=delete Space=play";
    int box_seq = 0;
    std::unordered_map<std::string, std::string> model_paths;
    explicit Editor(const ember::Config &c) : host(c) {}

    Entity entity_at(int i) { return host.entity(names[size_t(i)]); }

    void prune_dead() {
        for (int i = int(names.size()) - 1; i >= 0; --i) {
            try {
                if (!host.alive(host.entity(names[size_t(i)]))) {
                    model_paths.erase(names[size_t(i)]);
                    names.erase(names.begin() + i);
                    if (selected >= i)
                        --selected;
                    if (selected >= int(names.size()))
                        selected = int(names.size()) - 1;
                }
            } catch (...) {
                model_paths.erase(names[size_t(i)]);
                names.erase(names.begin() + i);
                if (selected >= i)
                    --selected;
                if (selected >= int(names.size()))
                    selected = int(names.size()) - 1;
            }
        }
    }

    void add_box(bool fixed, std::string name = {}) {
        if (name.empty()) {
            do {
                name = (fixed ? "floor_" : "box_") + std::to_string(box_seq++);
            } while (std::find(names.begin(), names.end(), name) != names.end());
        }
        const ember::Vec3 size = fixed ? ember::Vec3{8, .25f, 8} : ember::Vec3{1, 1, 1};
        Entity e = host.create_box(name, size, fixed ? ember::Vec3{.34f, .36f, .42f} :
                                                    ember::Vec3{.8f, .4f, .2f});
        names.push_back(name);
        selected = int(names.size()) - 1;
        // The visual transform must be correct with and without Jolt.
        host.set_position(e, fixed ? ember::Vec3{0, -1, 0} : ember::Vec3{0, 2, 0});
        ember::BodyDesc d;
        d.shape = ember::BodyShape::box(fixed ? ember::Vec3{4, .125f, 4} :
                                                ember::Vec3{.5f, .5f, .5f});
        d.motion = fixed ? ember::BodyMotion::Static : ember::BodyMotion::Dynamic;
        d.allow_rotation = false;
        try {
            if (host.advanced_physics_enabled()) {
                host.add_rigid_body(e, d);
                log = fixed ? "Static floor added" : "Dynamic box added";
            } else {
                log = fixed ? "Visual floor (physics off)" : "Visual box (physics off)";
            }
        } catch (const std::exception &ex) {
            log = std::string("Visual only: ") + ex.what();
        }
    }

    void add_model(const std::string &path, const std::string &name = "prop") {
        auto h = host.load_asset(path);
        host.spawn(h, name);
        model_paths[name] = path;
        names.push_back(name);
        selected = int(names.size()) - 1;
        log = "Model spawned as " + name;
    }

    void destroy_selected() {
        if (selected < 0 || selected >= int(names.size()))
            return;
        try {
            host.destroy(host.entity(names[size_t(selected)]));
            log = "Destroyed " + names[size_t(selected)];
        } catch (const std::exception &e) {
            log = e.what();
            return;
        }
        model_paths.erase(names[size_t(selected)]);
        names.erase(names.begin() + selected);
        if (selected >= int(names.size()))
            selected = int(names.size()) - 1;
    }

    void move_selected(float dx, float dy, float dz) {
        if (selected < 0 || selected >= int(names.size()))
            return;
        try {
            Entity e = host.entity(names[size_t(selected)]);
            auto t = host.transform(e);
            t.position.x += dx;
            t.position.y += dy;
            t.position.z += dz;
            host.set_transform(e, t);
        } catch (const std::exception &e) {
            log = e.what();
        }
    }

    void save_scene(const std::string &path) const {
        ember::SaveData data;
        data.set("count", int64_t(names.size()));
        data.set("profile", int64_t(host.profile()));
        for (size_t i = 0; i < names.size(); ++i) {
            std::string key = "object." + std::to_string(i) + ".";
            const auto &name = names[i];
            data.set(key + "name", name);
            auto model = model_paths.find(name);
            data.set(key + "kind", model != model_paths.end() ? std::string("model") :
                                      name.compare(0, 6, "floor_") == 0 ? std::string("floor") : std::string("box"));
            if (model != model_paths.end())
                data.set(key + "asset", model->second);
            auto t = host.transform(host.entity(name));
            data.set(key + "position", t.position);
            data.set(key + "yaw", double(t.yaw));
        }
        data.write(path);
    }

    static Editor load_scene(const ember::Config &cfg, const std::string &path) {
        auto data = ember::SaveData::read(path);
        int64_t count = data.integer("count");
        if (count < 0 || count > 1000)
            throw std::runtime_error("Editor scene object count exceeds 1000");
        Editor restored(cfg);
        for (int64_t i = 0; i < count; ++i) {
            std::string key = "object." + std::to_string(i) + ".";
            auto name = data.string(key + "name");
            auto kind = data.string(key + "kind");
            if (kind == "model")
                restored.add_model(data.string(key + "asset"), name);
            else if (kind == "floor" || kind == "box")
                restored.add_box(kind == "floor", name);
            else
                throw std::runtime_error("Unsupported editor object kind: " + kind);
            ember::game::Transform t;
            t.position = data.vec3(key + "position");
            t.yaw = float(data.number(key + "yaw"));
            restored.host.set_transform(restored.host.entity(name), t);
        }
        int64_t profile = data.integer("profile");
        if (profile < int64_t(ember::Profile::Normal) || profile > int64_t(ember::Profile::Pixel))
            throw std::runtime_error("Invalid editor profile");
        restored.host.set_profile(ember::Profile(profile));
        restored.log = "Loaded " + path;
        return restored;
    }
};

void blit_text(std::vector<uint32_t> &px, int W, int H, int x0, int y0, const std::string &s, uint32_t col) {
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t *rows = glyph_rows(s[i]);
        if (!rows)
            continue;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                if (rows[r] & (0x80u >> c)) {
                    int x = x0 + int(i) * 8 + c, y = y0 + r;
                    if (x >= 0 && y >= 0 && x < W && y < H)
                        px[size_t(y) * W + x] = col;
                }
    }
}
void fill_rect(std::vector<uint32_t> &px, int W, int H, int x0, int y0, int w, int h, uint32_t col) {
    for (int y = std::max(0, y0); y < std::min(H, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x0 + w); ++x)
            px[size_t(y) * W + x] = col;
}
void compose_editor(Editor &ed, const ember::Frame &frame, const std::vector<uint32_t> &src,
                    int win_w, int win_h, std::vector<uint32_t> &composite) {
    constexpr int kLeft = 300, kRight = 320, kBottom = 96;
            composite.assign(size_t(win_w) * win_h, 0xff14100cu);
            int vx = kLeft, vw = std::max(1, win_w - kLeft - kRight), vh = std::max(1, win_h - kBottom);
            int sw = frame.width, sh = frame.height;
            float sc = std::min(float(vw) / sw, float(vh) / sh);
            int dw = std::max(1, int(sw * sc)), dh = std::max(1, int(sh * sc));
            int ox = vx + (vw - dw) / 2, oy = (vh - dh) / 2;
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++) {
                    int sx = std::min(sw - 1, int(x / sc)), sy = std::min(sh - 1, int(y / sc));
                    composite[size_t(oy + y) * win_w + ox + x] = src[size_t(sy) * sw + sx];
                }
            // Panels
            fill_rect(composite, win_w, win_h, 0, 0, kLeft, win_h, 0xff1d1d24u);
            fill_rect(composite, win_w, win_h, win_w - kRight, 0, kRight, win_h, 0xff1d1d24u);
            fill_rect(composite, win_w, win_h, 0, win_h - kBottom, win_w, kBottom, 0xff101014u);
            fill_rect(composite, win_w, win_h, vx, 0, vw, 2, ed.playing ? 0xff3fb950u : 0xffd29922u);
            blit_text(composite, win_w, win_h, 12, 10, "SCENE", 0xff9da4b4u);
            int y = 30;
            for (size_t i = 0; i < ed.names.size() && y + 10 < win_h - kBottom; ++i) {
                bool sel = int(i) == ed.selected;
                if (sel)
                    fill_rect(composite, win_w, win_h, 6, y - 2, kLeft - 12, 12, 0xff30363du);
                uint32_t col = sel ? 0xffffe08au : 0xffd7dae0u;
                std::string label = (sel ? "> " : "  ") + ed.names[i];
                if (label.size() > 34)
                    label = label.substr(0, 34);
                blit_text(composite, win_w, win_h, 10, y, label, col);
                y += 13;
            }
            if (ed.names.empty())
                blit_text(composite, win_w, win_h, 10, 30, "(empty: press N)", 0xff7d8590u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 10, "INSPECTOR", 0xff9da4b4u);
            if (ed.selected >= 0 && ed.selected < int(ed.names.size())) {
                try {
                    Entity e = ed.host.entity(ed.names[size_t(ed.selected)]);
                    auto t = ed.host.transform(e);
                    char b[128];
                    std::snprintf(b, sizeof b, "%.2f %.2f %.2f", t.position.x, t.position.y, t.position.z);
                    blit_text(composite, win_w, win_h, win_w - kRight + 12, 30, ed.names[size_t(ed.selected)],
                              0xffffe08au);
                    blit_text(composite, win_w, win_h, win_w - kRight + 12, 44, std::string("pos ") + b,
                              0xffd7dae0u);
                    std::snprintf(b, sizeof b, "yaw %.2f", t.yaw);
                    blit_text(composite, win_w, win_h, win_w - kRight + 12, 58, b, 0xffd7dae0u);
                    bool has = false;
                    try {
                        has = ed.host.has_rigid_body(e);
                    } catch (...) {
                    }
                    blit_text(composite, win_w, win_h, win_w - kRight + 12, 72,
                              has ? "body: yes" : "body: no/visual", 0xffd7dae0u);
                    if (has) {
                        try {
                            auto v = ed.host.velocity(e);
                            std::snprintf(b, sizeof b, "vel %.2f %.2f %.2f", v.x, v.y, v.z);
                            blit_text(composite, win_w, win_h, win_w - kRight + 12, 86, b, 0xffd7dae0u);
                        } catch (...) {
                        }
                    }
                } catch (const std::exception &e) {
                    blit_text(composite, win_w, win_h, win_w - kRight + 12, 30, e.what(), 0xfff47067u);
                }
            } else {
                blit_text(composite, win_w, win_h, win_w - kRight + 12, 30, "(nothing selected)",
                          0xff7d8590u);
            }
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 112, "[N] box  [F] floor", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 126, "[Del] delete body+node", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 140, "[Space] play/pause", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 154, "[S] image [Ctrl+S] save", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 168, "[Ctrl+O] load  R yaw", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, win_w - kRight + 12, 182, "WASD+QE move selected", 0xff9da4b4u);
            blit_text(composite, win_w, win_h, 12, win_h - kBottom + 12,
                        (ed.playing ? "[LIVE] " : "[PAUSED] ") + ed.log.substr(0, 120), 0xffd7dae0u);
            blit_text(composite, win_w, win_h, 12, win_h - kBottom + 30,
                        "cpu backend | profile " + std::string(ember::profile_name(ed.host.profile())) +
                            " | native scene preview",
                        0xff7d8590u);
}
} // namespace

int main(int argc, char **argv) {
#ifdef EMBER_VIEWER
    SDL_SetMainReady();
#endif
    try {
        std::string config_path = "config/engine.toml", model, output, scene_path, save_path = "editor.embsave";
        bool window = true, initial_box = false, initial_floor = false, save_scene_on_start = false;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto val = [&](const char *what) {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("Missing value: ") + what);
                return std::string(argv[++i]);
            };
            if (a == "--config")
                config_path = val("--config");
            else if (a == "--model")
                model = val("--model");
            else if (a == "--out")
                output = val("--out");
            else if (a == "--scene")
                scene_path = val("--scene");
            else if (a == "--save")
                save_path = val("--save");
            else if (a == "--save-scene")
                save_scene_on_start = true;
            else if (a == "--box")
                initial_box = true;
            else if (a == "--floor")
                initial_floor = true;
            else if (a == "--headless")
                window = false;
            else if (a == "--help") {
                std::cout << "ember_editor (native scene preview)\n"
                             "--config path  --model asset  --scene file  --save file  --save-scene\n"
                             "--box  --floor  --out shot.ppm  --headless\n"
                             "Keys: N box, F floor, Del destroy, Space play, WASD+QE move,\n"
                             "      R yaw, 1-4 profile, S snapshot with UI, Ctrl+S save, Ctrl+O load, Esc quit\n";
                return 0;
            } else
                throw std::runtime_error("Unknown argument: " + a);
        }
        if (!output.empty() && window && getenv("DISPLAY") == nullptr && getenv("WAYLAND_DISPLAY") == nullptr) {
            // Allow CI without a display: snapshot implies headless.
            const char *sdl_video = getenv("SDL_VIDEODRIVER");
            if (!sdl_video)
                window = false;
        }

        ember::Config cfg = ember::Config::load(config_path);
        cfg.backend = "cpu"; // editor viewport always uses the dependency-free CPU backend
        if (!model.empty() && !scene_path.empty())
            throw std::runtime_error("Use either --model or --scene");
        Editor ed(cfg);
        if (!scene_path.empty())
            ed = Editor::load_scene(cfg, scene_path);
        if (!model.empty()) {
            try {
                ed.add_model(model);
            } catch (const std::exception &e) {
                throw std::runtime_error(std::string("model: ") + e.what());
            }
        }
        if (initial_floor)
            ed.add_box(true);
        if (initial_box)
            ed.add_box(false);
        if (save_scene_on_start)
            ed.save_scene(save_path);

        auto snapshot_once = [&](const std::string &path) {
            ember::SceneRenderer renderer;
            ember::Frame frame;
            auto backend = ember::make_cpu(cfg.worker_threads);
            ed.host.render(renderer, *backend, frame);
            std::vector<uint32_t> image;
            compose_editor(ed, frame, backend->readback(), 1280, 720, image);
            ember::write_ppm(path, 1280, 720, image);
            std::cout << "snapshot: " << path << "\n";
        };
        if (!window) {
            if (output.empty())
                throw std::runtime_error("--headless requires --out shot.ppm");
            snapshot_once(output);
            return 0;
        }

#ifdef EMBER_VIEWER
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        int win_w = 1280, win_h = 720;
        SDL_Window *win =
            SDL_CreateWindow("Ember scene preview", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, win_w, win_h, SDL_WINDOW_RESIZABLE);
        if (!win)
            throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(win, 640, 400);

        ember::SceneRenderer renderer;
        ember::Frame frame;
        auto backend = ember::make_cpu(cfg.worker_threads);
        std::vector<uint32_t> composite;
        bool running = true, dirty = true, viewport_down = false;
        auto prev = std::chrono::steady_clock::now();
        const int kLeft = 300, kRight = 320, kBottom = 96;

        auto render_view = [&]() {
            ed.prune_dead();
            ed.host.render(renderer, *backend, frame);
            const auto &src = backend->readback();
            compose_editor(ed, frame, src, win_w, win_h, composite);
            SDL_Surface *ws = SDL_GetWindowSurface(win);
            if (ws) {
                SDL_Surface *img = SDL_CreateRGBSurfaceWithFormatFrom(
                    composite.data(), win_w, win_h, 32, win_w * 4, SDL_PIXELFORMAT_RGBA32);
                if (!img)
                    throw std::runtime_error(SDL_GetError());
                SDL_BlitSurface(img, nullptr, ws, nullptr);
                SDL_FreeSurface(img);
                SDL_UpdateWindowSurface(win);
            }
            dirty = false;
        };

        render_view();
        SDL_Event e{};
        while (running) {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - prev).count();
            prev = now;
            while (SDL_PollEvent(&e)) {
                dirty = true;
                if (e.type == SDL_QUIT)
                    running = false;
                else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    win_w = std::max(640, e.window.data1);
                    win_h = std::max(400, e.window.data2);
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x, my = e.button.y;
                    // Hierarchy click-to-select.
                    if (mx < kLeft && my >= 26 && my < win_h - kBottom) {
                        int row = (my - 30 + 6) / 13;
                        if (row >= 0 && row < int(ed.names.size())) {
                            ed.selected = row;
                            ed.log = "Selected " + ed.names[size_t(row)];
                        }
                    }
                    if (mx >= kLeft && mx < win_w - kRight && my < win_h - kBottom) {
                        viewport_down = true;
                        ember::game::InputEvent ie{ember::game::InputEvent::Type::MouseDown, ed.host.time(),
                                                   1, 0, float(mx), float(my)};
                        ed.host.dispatch_input(ie);
                    }
                } else if (e.type == SDL_MOUSEBUTTONUP) {
                    if (viewport_down) {
                        ember::game::InputEvent ie{ember::game::InputEvent::Type::MouseUp, ed.host.time(),
                                                   int(e.button.button), 0, float(e.button.x), float(e.button.y)};
                        ed.host.dispatch_input(ie);
                        viewport_down = false;
                    }
                } else if (e.type == SDL_MOUSEMOTION) {
                    if (viewport_down || (e.motion.x >= kLeft && e.motion.x < win_w - kRight &&
                                          e.motion.y < win_h - kBottom)) {
                        ember::game::InputEvent ie{ember::game::InputEvent::Type::MouseMove, ed.host.time(),
                                                   0, 0, float(e.motion.x), float(e.motion.y),
                                                   float(e.motion.xrel), float(e.motion.yrel)};
                        ed.host.dispatch_input(ie);
                    }
                } else if (e.type == SDL_KEYDOWN) {
                    int k = e.key.keysym.sym;
                    ember::game::InputEvent ie{ember::game::InputEvent::Type::KeyDown, ed.host.time(), k};
                    ed.host.dispatch_input(ie);
                    if (e.key.repeat)
                        continue;
                    try {
                    if (k == SDLK_ESCAPE)
                        running = false;
                    else if (k == SDLK_SPACE) {
                        ed.playing = !ed.playing;
                        ed.log = ed.playing ? "Playing" : "Paused";
                    } else if (k == SDLK_n)
                        ed.add_box(false);
                    else if (k == SDLK_f)
                        ed.add_box(true);
                    else if (k == SDLK_DELETE || k == SDLK_BACKSPACE)
                        ed.destroy_selected();
                    else if (k == SDLK_s && (e.key.keysym.mod & KMOD_CTRL)) {
                        ed.save_scene(save_path);
                        ed.log = "Saved " + save_path;
                    } else if (k == SDLK_o && (e.key.keysym.mod & KMOD_CTRL)) {
                        ed = Editor::load_scene(cfg, save_path);
                    } else if (k == SDLK_s) {
                        snapshot_once(output.empty() ? "editor.ppm" : output);
                        ed.log = "Snapshot saved";
                    } else if (k == SDLK_1)
                        ed.host.set_profile(ember::Profile::PS1);
                    else if (k == SDLK_2)
                        ed.host.set_profile(ember::Profile::N64);
                    else if (k == SDLK_3)
                        ed.host.set_profile(ember::Profile::Pixel);
                    else if (k == SDLK_4)
                        ed.host.set_profile(ember::Profile::Normal);
                    else if (k == SDLK_r) {
                        if (ed.selected >= 0 && ed.selected < int(ed.names.size())) {
                            try {
                                Entity en = ed.host.entity(ed.names[size_t(ed.selected)]);
                                auto t = ed.host.transform(en);
                                t.yaw += 0.2f;
                                ed.host.set_transform(en, t);
                            } catch (const std::exception &ex) {
                                ed.log = ex.what();
                            }
                        }
                    }
                    } catch (const std::exception &ex) {
                        ed.log = ex.what();
                    }
                } else if (e.type == SDL_KEYUP) {
                    ember::game::InputEvent ie{ember::game::InputEvent::Type::KeyUp, ed.host.time(),
                                               e.key.keysym.sym};
                    ed.host.dispatch_input(ie);
                }
            }
            const Uint8 *ks = SDL_GetKeyboardState(nullptr);
            float sp = float(3 * dt);
            if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP])
                ed.move_selected(0, 0, sp);
            if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN])
                ed.move_selected(0, 0, -sp);
            if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT])
                ed.move_selected(-sp, 0, 0);
            if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT])
                ed.move_selected(sp, 0, 0);
            if (ks[SDL_SCANCODE_Q])
                ed.move_selected(0, -sp, 0);
            if (ks[SDL_SCANCODE_E])
                ed.move_selected(0, sp, 0);
            if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_S] ||
                ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT] ||
                ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_Q] ||
                ks[SDL_SCANCODE_E])
                dirty = true;
            if (ed.playing) {
                try {
                    ed.host.tick(std::min(dt, 0.1));
                } catch (const std::exception &ex) {
                    ed.log = ex.what();
                    ed.playing = false;
                }
                dirty = true;
            }
            if (dirty)
                render_view();
            if (ed.playing)
                std::this_thread::sleep_until(now + std::chrono::milliseconds(16));
            else if (!dirty)
                SDL_Delay(16);
        }
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 0;
#else
        throw std::runtime_error("Viewer was not compiled (EMBER_VIEWER=OFF)");
#endif
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
