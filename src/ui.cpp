#include "ember/ui.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace ember {
namespace {
struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};
struct Texture {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
};

class Painter : public Rml::RenderInterface {
  public:
    int width = 0, height = 0;
    bool scissor = false;
    Rml::Rectanglei clip;
    std::vector<uint32_t> pixels;
    std::map<uintptr_t, Geometry> geometries;
    std::map<uintptr_t, Texture> textures;
    uintptr_t next = 1;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        if (indices.size() % 3 != 0)
            throw std::runtime_error("RmlUi geometry index count is not triangular");
        const auto id = next++;
        geometries.emplace(id, Geometry{{vertices.begin(), vertices.end()}, {indices.begin(), indices.end()}});
        return id;
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override { geometries.erase(geometry); }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override {
        SDL_Surface *loaded = SDL_LoadBMP(source.c_str());
        if (!loaded)
            return 0;
        SDL_Surface *surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if (!surface)
            return 0;
        dimensions = {surface->w, surface->h};
        std::vector<uint8_t> bytes(size_t(surface->w) * size_t(surface->h) * 4);
        for (int y = 0; y < surface->h; ++y)
            std::copy_n(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch, surface->w * 4,
                        bytes.data() + size_t(y) * size_t(surface->w) * 4);
        SDL_FreeSurface(surface);
        for (size_t i = 0; i < bytes.size(); i += 4)
            for (int channel = 0; channel < 3; ++channel)
                bytes[i + size_t(channel)] =
                    uint8_t(unsigned(bytes[i + size_t(channel)]) * bytes[i + 3] / 255);
        return GenerateTexture({bytes.data(), bytes.size()}, dimensions);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> data, Rml::Vector2i dimensions) override {
        if (dimensions.x <= 0 || dimensions.y <= 0 ||
            data.size() != size_t(dimensions.x) * size_t(dimensions.y) * 4)
            return 0;
        const auto id = next++;
        textures.emplace(id, Texture{dimensions.x, dimensions.y, {data.begin(), data.end()}});
        return id;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override { textures.erase(texture); }
    void EnableScissorRegion(bool enabled) override { scissor = enabled; }
    void SetScissorRegion(Rml::Rectanglei rectangle) override { clip = rectangle; }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture_handle) override {
        auto geometry_it = geometries.find(handle);
        if (geometry_it == geometries.end())
            return;
        const Geometry &geometry = geometry_it->second;
        const Texture *texture = nullptr;
        if (texture_handle) {
            auto texture_it = textures.find(texture_handle);
            if (texture_it == textures.end())
                return;
            texture = &texture_it->second;
        }
        auto edge = [](Rml::Vector2f a, Rml::Vector2f b, float x, float y) {
            return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        };
        auto top = [](Rml::Vector2f a, Rml::Vector2f b) {
            return b.y < a.y || (b.y == a.y && b.x > a.x);
        };
        for (size_t index = 0; index + 2 < geometry.indices.size(); index += 3) {
            const int i0 = geometry.indices[index], i1 = geometry.indices[index + 1],
                      i2 = geometry.indices[index + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || size_t(i0) >= geometry.vertices.size() ||
                size_t(i1) >= geometry.vertices.size() || size_t(i2) >= geometry.vertices.size())
                continue;
            Rml::Vertex vertices[3] = {geometry.vertices[size_t(i0)], geometry.vertices[size_t(i1)],
                                       geometry.vertices[size_t(i2)]};
            for (auto &vertex : vertices)
                vertex.position += translation;
            float area = edge(vertices[0].position, vertices[1].position, vertices[2].position.x,
                              vertices[2].position.y);
            if (area < 0) {
                std::swap(vertices[1], vertices[2]);
                area = -area;
            }
            if (area < 1e-7f)
                continue;
            int x0 = std::max(0, int(std::floor(std::min(
                                     {vertices[0].position.x, vertices[1].position.x, vertices[2].position.x}))));
            int x1 = std::min(width - 1, int(std::ceil(std::max(
                                             {vertices[0].position.x, vertices[1].position.x, vertices[2].position.x}))));
            int y0 = std::max(0, int(std::floor(std::min(
                                     {vertices[0].position.y, vertices[1].position.y, vertices[2].position.y}))));
            int y1 = std::min(height - 1, int(std::ceil(std::max(
                                              {vertices[0].position.y, vertices[1].position.y, vertices[2].position.y}))));
            if (scissor) {
                x0 = std::max(x0, clip.Left());
                x1 = std::min(x1, clip.Right() - 1);
                y0 = std::max(y0, clip.Top());
                y1 = std::min(y1, clip.Bottom() - 1);
            }
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float e0 = edge(vertices[1].position, vertices[2].position, x + .5f, y + .5f);
                    const float e1 = edge(vertices[2].position, vertices[0].position, x + .5f, y + .5f);
                    const float e2 = edge(vertices[0].position, vertices[1].position, x + .5f, y + .5f);
                    if (e0 < 0 || e1 < 0 || e2 < 0 ||
                        (e0 == 0 && !top(vertices[1].position, vertices[2].position)) ||
                        (e1 == 0 && !top(vertices[2].position, vertices[0].position)) ||
                        (e2 == 0 && !top(vertices[0].position, vertices[1].position)))
                        continue;
                    const float a = e0 / area, b = e1 / area, c = e2 / area;
                    float rgba[4];
                    for (int channel = 0; channel < 4; ++channel)
                        rgba[channel] = a * vertices[0].colour[channel] + b * vertices[1].colour[channel] +
                                        c * vertices[2].colour[channel];
                    if (texture) {
                        const auto uv = vertices[0].tex_coord * a + vertices[1].tex_coord * b +
                                        vertices[2].tex_coord * c;
                        const int u = std::clamp(int(uv.x * texture->width), 0, texture->width - 1);
                        const int v = std::clamp(int(uv.y * texture->height), 0, texture->height - 1);
                        const size_t base = (size_t(v) * size_t(texture->width) + size_t(u)) * 4;
                        for (int channel = 0; channel < 4; ++channel)
                            rgba[channel] *= texture->pixels[base + size_t(channel)] / 255.f;
                    }
                    auto &destination = pixels[size_t(y) * size_t(width) + size_t(x)];
                    uint32_t result = 0;
                    const float inverse_alpha = 1 - rgba[3] / 255;
                    for (int channel = 0; channel < 4; ++channel) {
                        const unsigned old = (destination >> (channel * 8)) & 255;
                        const unsigned value = unsigned(
                            std::clamp(rgba[channel] + old * inverse_alpha, 0.f, 255.f) + .5f);
                        result |= value << (channel * 8);
                    }
                    destination = result;
                }
        }
    }
};

class Clock : public Rml::SystemInterface {
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();

  public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }
};

std::atomic<bool> active_overlay{false};
} // namespace

struct UiOverlay::Impl : public Rml::EventListener {
    Painter painter;
    Clock clock;
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;
    ClickHandler click_handler;
    bool dirty = true;
    bool shown = true;
    bool owns_rml = false;
    double next_update = 0;
    size_t redraw_count = 0;

    Impl(const std::string &rml_file, const std::string &font_file, int width, int height) {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("UI dimensions must be positive");
        bool expected = false;
        if (!active_overlay.compare_exchange_strong(expected, true))
            throw std::runtime_error("Only one Ember UiOverlay may be active");
        painter.width = width;
        painter.height = height;
        Rml::SetSystemInterface(&clock);
        Rml::SetRenderInterface(&painter);
        if (!Rml::Initialise()) {
            active_overlay = false;
            throw std::runtime_error("RmlUi initialization failed");
        }
        owns_rml = true;
        try {
            if (!Rml::LoadFontFace(font_file))
                throw std::runtime_error("Cannot load UI font: " + font_file);
            context = Rml::CreateContext("ember-ui", {width, height});
            if (!context)
                throw std::runtime_error("Cannot create RmlUi context");
            document = context->LoadDocument(rml_file);
            if (!document)
                throw std::runtime_error("Cannot load RML: " + rml_file);
            document->AddEventListener("click", this);
            document->Show();
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~Impl() override { shutdown(); }

    void shutdown() {
        if (document)
            document->RemoveEventListener("click", this);
        document = nullptr;
        context = nullptr;
        if (owns_rml) {
            Rml::Shutdown();
            owns_rml = false;
            active_overlay = false;
        }
    }

    void ProcessEvent(Rml::Event &event) override {
        if (click_handler)
            click_handler(event.GetTargetElement()->GetId());
        dirty = true;
    }
};

UiOverlay::UiOverlay(const std::string &rml_file, const std::string &font_file, int width, int height)
    : impl_(std::make_unique<Impl>(rml_file, font_file, width, height)) {}
UiOverlay::~UiOverlay() = default;

void UiOverlay::resize(int width, int height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("UI dimensions must be positive");
    impl_->painter.width = width;
    impl_->painter.height = height;
    impl_->context->SetDimensions({width, height});
    impl_->dirty = true;
}
void UiOverlay::invalidate() { impl_->dirty = true; }
void UiOverlay::set_click_handler(ClickHandler handler) { impl_->click_handler = std::move(handler); }
void UiOverlay::mouse_move(int x, int y) {
    impl_->context->ProcessMouseMove(x, y, 0);
    impl_->dirty = true;
}
void UiOverlay::mouse_button_down(int button) {
    impl_->context->ProcessMouseButtonDown(button, 0);
    impl_->dirty = true;
}
void UiOverlay::mouse_button_up(int button) {
    impl_->context->ProcessMouseButtonUp(button, 0);
    impl_->dirty = true;
}
void UiOverlay::set_text(const std::string &id, const std::string &text) {
    set_markup(id, Rml::StringUtilities::EncodeRml(text));
}
void UiOverlay::set_markup(const std::string &id, const std::string &rml) {
    if (auto *element = impl_->document->GetElementById(id)) {
        element->SetInnerRML(rml);
        impl_->dirty = true;
    }
}
void UiOverlay::set_visible(bool visible) {
    if (visible == impl_->shown)
        return;
    if (visible)
        impl_->document->Show();
    else
        impl_->document->Hide();
    impl_->shown = visible;
    impl_->dirty = true;
}
bool UiOverlay::visible() const { return impl_->shown; }
const std::vector<uint32_t> &UiOverlay::pixels() {
    const double now = impl_->clock.GetElapsedTime();
    if (impl_->dirty || now >= impl_->next_update) {
        impl_->context->Update();
        impl_->painter.pixels.assign(size_t(impl_->painter.width) * size_t(impl_->painter.height), 0);
        impl_->context->Render();
        impl_->next_update = now + std::max(.001, impl_->context->GetNextUpdateDelay());
        impl_->dirty = false;
        ++impl_->redraw_count;
    }
    return impl_->painter.pixels;
}
size_t UiOverlay::redraws() const { return impl_->redraw_count; }

} // namespace ember
