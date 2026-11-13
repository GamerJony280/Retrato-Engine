#include "ember/render.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
namespace ember {
namespace {
int coord(int x, int n, int wrap) {
    if (wrap == 1)
        return std::clamp(x, 0, n - 1);
    if (wrap == 2) {
        int a = x % (2 * n);
        if (a < 0)
            a += 2 * n;
        return a < n ? a : 2 * n - a - 1;
    }
    x %= n;
    return x < 0 ? x + n : x;
}
} // namespace
void validate_frame(const Frame &f) {
    if (f.depth_mode < DepthMode::Automatic || f.depth_mode > DepthMode::Disabled)
        throw std::runtime_error("Invalid frame depth mode");
    constexpr uint64_t max_pixels = 256ull * 1024 * 1024;
    if (f.width <= 0 || f.height <= 0 || f.width > 16384 || f.height > 16384 ||
        uint64_t(f.width) * uint64_t(f.height) > max_pixels)
        throw std::runtime_error("Invalid frame dimensions");
    if (f.vertices.size() % 3 || f.vertices.size() > uint64_t(INT32_MAX))
        throw std::runtime_error("Frame vertices must contain complete triangles");
    if (f.texture_words > f.resources.size() || f.texture_words > uint64_t(INT32_MAX))
        throw std::runtime_error("Invalid frame texture pool");
    const auto &s = f.settings;
    if (f.profile < Profile::Normal || f.profile > Profile::Pixel || s.profile != int(f.profile) ||
        s.height != f.height || s.crt != int(f.crt))
        throw std::runtime_error("Frame and shader settings disagree");
    for (const float *vector : {s.right, s.up, s.forward, s.center})
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(vector[i]))
                throw std::runtime_error("Non-finite shader transform");
    if (s.shadow_size < 0 || s.shadow_size > 8192 || s.shadow_offset < 0 || !std::isfinite(s.grid) ||
        !std::isfinite(s.bias) || !std::isfinite(s.right[3]) || s.grid < 0 || s.bias < 0)
        throw std::runtime_error("Invalid shader settings");
    if (s.shadow_size) {
        uint64_t end = uint64_t(s.shadow_offset) + uint64_t(s.shadow_size) * s.shadow_size;
        if (s.right[3] <= 0 || end > f.resources.size())
            throw std::runtime_error("Invalid frame shadow pool");
    }
    auto finite3 = [](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (const auto &v : f.vertices) {
        const float scalar[] = {v.x,          v.y,           v.z,          v.invw,       v.u,
                                v.v,          v.r,           v.g,          v.b,          v.checker,
                                v.texture_offset, v.texture_width, v.texture_height, v.wrap,
                                v.alpha_cutoff, v.opacity};
        for (float value : scalar)
            if (!std::isfinite(value))
                throw std::runtime_error("Non-finite frame vertex");
        if (!finite3(v.world) || !finite3(v.normal) || !finite3(v.sun_color))
            throw std::runtime_error("Non-finite frame vector");
        // UVs are premultiplied by invw. Limit the decoded coordinate so
        // texel conversion fits int32 on every CPU and GLSL backend.
        if (v.invw <= 0 || std::abs(double(v.u) / v.invw) > 65536.0 ||
            std::abs(double(v.v) / v.invw) > 65536.0)
            throw std::runtime_error("Frame UV exceeds supported range (+/-65536)");
        if (v.invw <= 0 || v.opacity < 0 || v.opacity > 1 || v.alpha_cutoff < 0 || v.alpha_cutoff > 1 ||
            v.wrap < 0 || v.wrap > 10 || std::floor(v.wrap) != v.wrap || int(v.wrap) % 4 == 3)
            throw std::runtime_error("Invalid frame interpolation or material values");
        if (v.texture_width < 0 || v.texture_height < 0 || v.texture_offset < 0 ||
            (v.texture_width == 0) != (v.texture_height == 0))
            throw std::runtime_error("Invalid frame texture reference");
        if (v.texture_width > 0) {
            if (double(v.texture_offset) > double(f.texture_words) ||
                v.texture_width > 16384 || v.texture_height > 16384 ||
                std::floor(v.texture_width) != v.texture_width ||
                std::floor(v.texture_height) != v.texture_height ||
                std::floor(v.texture_offset) != v.texture_offset)
                throw std::runtime_error("Invalid frame texture dimensions");
            uint64_t end = uint64_t(v.texture_offset) + uint64_t(v.texture_width) * uint64_t(v.texture_height);
            if (end > f.texture_words)
                throw std::runtime_error("Frame texture reference exceeds pool");
        }
    }
}
Vec3 shade_fragment(const Frame &f, const DrawVertex &v, Vec3 color, Vec3 sun, Vec3 world, Vec3 normal,
                    Vec2 uv, float &alpha) {
    const auto &s = f.settings;
    float visibility = 1;
    if (s.shadow_size > 0 && (sun.x + sun.y + sun.z) > 0) {
        if (s.grid > 0) {
            Vec3 q{(std::floor(world.x / s.grid) + .5f) * s.grid,
                   (std::floor(world.y / s.grid) + .5f) * s.grid,
                   (std::floor(world.z / s.grid) + .5f) * s.grid};
            normal = normalized(normal);
            q = q - normal * dot(q - world, normal);
            world = q;
        }
        Vec3 p = world - Vec3{s.center[0], s.center[1], s.center[2]};
        float x = dot(p, {s.right[0], s.right[1], s.right[2]}), y = dot(p, {s.up[0], s.up[1], s.up[2]}),
              z = dot(p, {s.forward[0], s.forward[1], s.forward[2]});
        float px = (x / s.right[3] + .5f) * s.shadow_size,
              py = (y / s.right[3] + .5f) * s.shadow_size;
        if (px >= 0 && py >= 0 && px < s.shadow_size && py < s.shadow_size) {
            int ix = int(px), iy = int(py);
            uint32_t raw = f.resources[size_t(s.shadow_offset) + size_t(iy) * s.shadow_size + ix];
            float depth;
            std::memcpy(&depth, &raw, 4);
            normal = normalized(normal);
            float slope = (std::abs(dot(normal, {s.right[0], s.right[1], s.right[2]})) +
                           std::abs(dot(normal, {s.up[0], s.up[1], s.up[2]}))) /
                          std::max(.1f, std::abs(dot(normal, {s.forward[0], s.forward[1], s.forward[2]})));
            float bias = s.bias + .6f * (s.right[3] / s.shadow_size) * slope;
            visibility = z <= depth + bias ? 1 : .22f;
        }
    }
    color = color + sun * visibility;
    alpha = v.opacity;
    if (v.texture_width > 0) {
        int w = int(v.texture_width), h = int(v.texture_height), wrap = int(v.wrap);
        auto texel = [&](int x, int y) {
            uint32_t p = f.resources[size_t(v.texture_offset) + size_t(coord(y, h, wrap / 4)) * w +
                                     coord(x, w, wrap % 4)];
            return std::array<float, 4>{float(p & 255) / 255, float((p >> 8) & 255) / 255,
                                        float((p >> 16) & 255) / 255, float(p >> 24) / 255};
        };
        std::array<float, 4> c{};
        float x = uv.x * w, y = uv.y * h;
        if (f.profile == Profile::N64) {
            x -= .5f;
            y -= .5f;
            int ix = int(std::floor(x)), iy = int(std::floor(y));
            float fx = x - ix, fy = y - iy;
            auto a = texel(ix, iy), b = texel(ix + 1, iy), d = texel(ix, iy + 1), e = texel(ix + 1, iy + 1);
            for (int k = 0; k < 4; k++)
                c[k] = fx + fy <= 1 ? a[k] * (1 - fx - fy) + b[k] * fx + d[k] * fy
                                    : e[k] * (fx + fy - 1) + d[k] * (1 - fx) + b[k] * (1 - fy);
        } else
            c = texel(int(std::floor(x)), int(std::floor(y)));
        color = mul(color, {c[0], c[1], c[2]});
        alpha *= c[3];
    } else if (v.checker > .5f)
        color = mul(color, texture_sample(uv.x, uv.y, f.profile));
    return color;
}
} // namespace ember
