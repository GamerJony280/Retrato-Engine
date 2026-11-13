#pragma once
#include "config.hpp"
#include "scene.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <array>
#include <type_traits>
#include <limits>
namespace ember {
// CPU-prepared packet: camera transforms / vertex lighting and surface attributes,
// interchangeable native GPU rasterization or reference software rasterization.
struct DrawVertex {
    float x, y, z, invw, u, v, r, g, b, checker;
    Vec3 world{}, normal{}, sun_color{};
    float texture_offset = 0, texture_width = 0, texture_height = 0, wrap = 0;
    float alpha_cutoff = 0, opacity = 1;
};
struct ShaderSettings {
    int profile = 0, crt = 0, height = 0, shadow_size = 0;
    float right[4]{}, up[4]{}, forward[4]{}, center[4]{};
    int shadow_offset = 0;
    float grid = 0, bias = .065f, pad = 0;
};
static_assert(sizeof(DrawVertex) == 100, "Shader vertex layout must match GLSL");
static_assert(sizeof(ShaderSettings) == 96, "Shader constants must match GLSL std140");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 && sizeof(int) == 4,
              "Renderer requires IEEE binary32 floats and 32-bit ints");
static_assert(std::is_standard_layout<DrawVertex>::value && std::is_trivially_copyable<DrawVertex>::value);
static_assert(std::is_standard_layout<ShaderSettings>::value && std::is_trivially_copyable<ShaderSettings>::value);
static_assert(sizeof(Vec3) == 12 && offsetof(Vec3, x) == 0 && offsetof(Vec3, y) == 4 && offsetof(Vec3, z) == 8);
static_assert(offsetof(DrawVertex, x) == 0 && offsetof(DrawVertex, y) == 4 && offsetof(DrawVertex, z) == 8 && offsetof(DrawVertex, invw) == 12);
static_assert(offsetof(DrawVertex, u) == 16 && offsetof(DrawVertex, v) == 20);
static_assert(offsetof(DrawVertex, r) == 24 && offsetof(DrawVertex, g) == 28 && offsetof(DrawVertex, b) == 32 && offsetof(DrawVertex, checker) == 36);
static_assert(offsetof(DrawVertex, world) == 40 && offsetof(DrawVertex, normal) == 52 && offsetof(DrawVertex, sun_color) == 64);
static_assert(offsetof(DrawVertex, texture_offset) == 76 && offsetof(DrawVertex, texture_width) == 80 && offsetof(DrawVertex, texture_height) == 84 && offsetof(DrawVertex, wrap) == 88);
static_assert(offsetof(DrawVertex, alpha_cutoff) == 92 && offsetof(DrawVertex, opacity) == 96);
static_assert(offsetof(ShaderSettings, profile) == 0 && offsetof(ShaderSettings, crt) == 4 && offsetof(ShaderSettings, height) == 8 && offsetof(ShaderSettings, shadow_size) == 12);
static_assert(offsetof(ShaderSettings, right) == 16 && offsetof(ShaderSettings, up) == 32 && offsetof(ShaderSettings, forward) == 48 && offsetof(ShaderSettings, center) == 64);
static_assert(offsetof(ShaderSettings, shadow_offset) == 80 && offsetof(ShaderSettings, grid) == 84 && offsetof(ShaderSettings, bias) == 88 && offsetof(ShaderSettings, pad) == 92);
struct VertexAttribute { uint32_t components, offset; };
inline constexpr VertexAttribute vertex_attributes[] = {
    {4, offsetof(DrawVertex,x)}, {2, offsetof(DrawVertex,u)},
    {4, offsetof(DrawVertex,r)}, {3, offsetof(DrawVertex,world)},
    {3, offsetof(DrawVertex,normal)}, {3, offsetof(DrawVertex,sun_color)},
    {4, offsetof(DrawVertex,texture_offset)}, {2, offsetof(DrawVertex,alpha_cutoff)}
};
enum class DepthMode { Automatic, Enabled, Disabled };
struct Frame {
    DepthMode depth_mode = DepthMode::Automatic;
    int width = 0, height = 0;
    Profile profile = Profile::Pixel;
    bool crt = false;
    std::vector<DrawVertex> vertices;
    ShaderSettings settings;
    std::vector<uint32_t> resources;
    uint64_t texture_revision = 0;
    size_t texture_words = 0;
};
class SceneRenderer {
    struct PreparedTriangle {
        Triangle geometry;
        size_t light_set = 0;
        float sort_depth = 0;
    };
    struct LightSet {
        std::array<size_t, 4> indices{};
        size_t count = 0;
    };
    std::vector<PreparedTriangle> world_;
    std::vector<LightSet> light_sets_;
    std::vector<float> shadows_;
    Vec3 light_right_, light_up_, light_forward_;
    int shadow_size_ = 0;
    Vec3 shadow_center_{};
    float shadow_extent_ = 32;
    std::unordered_map<const ImageData *, size_t> image_offsets_;
    std::vector<std::shared_ptr<const ImageData>> images_;
    std::vector<uint32_t> texture_words_;
    uint64_t revision_ = 0;

  public:
    void prepare(const Scene &, const Camera &, const Config &, Frame &);
};
class Backend {
  public:
    virtual ~Backend() = default;
    virtual const char *name() const = 0;
    virtual std::string device_description() const { return name(); }
    virtual void draw(const Frame &) = 0;
    // True when a frame was presented or skipped for a minimized native surface.
    // False means the caller must present readback() with its platform adapter.
    virtual bool present() { return false; }
    virtual bool direct_presentation() const noexcept { return false; }
    // RGBA, top-left origin. For export/testing; GPU readback is intentionally explicit.
    virtual const std::vector<uint32_t> &readback() = 0;
};
inline bool frame_depth_test(const Frame &f) noexcept {
    return f.depth_mode == DepthMode::Enabled ||
        (f.depth_mode == DepthMode::Automatic && f.profile != Profile::PS1);
}
// Validates the public CPU/GPU packet contract before a backend consumes it.
void validate_frame(const Frame &);
std::unique_ptr<Backend> make_cpu(int workers);
std::unique_ptr<Backend> make_opengl();
std::unique_ptr<Backend> make_vulkan();
std::unique_ptr<Backend> make_backend(const std::string &, int workers);
void write_ppm(const std::string &, int width, int height, const std::vector<uint32_t> &);
Vec3 shade_fragment(const Frame &, const DrawVertex &, Vec3 color, Vec3 sun, Vec3 world, Vec3 normal, Vec2 uv,
                    float &alpha);
Vec3 texture_sample(float u, float v, Profile profile);
uint32_t finish_pixel(Vec3 color, int x, int y, Profile profile, bool crt);
} // namespace ember
