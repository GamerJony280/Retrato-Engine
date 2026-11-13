#include "ember/asset.hpp"
#include "ember/render.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
namespace ember {
namespace {
float edge(Vec2 a, Vec2 b, Vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}
struct ClipVertex {
    Vec3 p;
    Vec2 uv;
    Vec3 color, world, normal, sun;
};
ClipVertex lerp(ClipVertex a, ClipVertex b, float t) {
    return {mix(a.p, b.p, t),         a.uv * (1 - t) + b.uv * t,  mix(a.color, b.color, t),
            mix(a.world, b.world, t), mix(a.normal, b.normal, t), mix(a.sun, b.sun, t)};
}
// Clip in camera space before perspective division; fixed buffers avoid per-face allocation.
int clip_plane(const ClipVertex *in, int n, ClipVertex *out, Vec3 normal, float distance) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        auto a = in[i], b = in[(i + 1) % n];
        float da = dot(a.p, normal) + distance, db = dot(b.p, normal) + distance;
        if (da >= 0)
            out[count++] = a;
        if ((da >= 0) != (db >= 0))
            out[count++] = lerp(a, b, da / (da - db));
    }
    return count;
}
} // namespace
void SceneRenderer::prepare(const Scene &scene, const Camera &camera, const Config &cfg, Frame &frame) {
    cfg.validate();
    const auto finite = [](Vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(camera.position) || !finite(camera.target) || !std::isfinite(camera.near_plane) ||
        !std::isfinite(camera.far_plane) || !std::isfinite(camera.fov) || camera.near_plane <= 0 ||
        camera.far_plane <= camera.near_plane || camera.fov <= 0 || camera.fov >= 3.14159265f ||
        length(camera.target - camera.position) < 1e-6f || !finite(scene.sun))
        throw std::runtime_error("Invalid camera or scene sun");
    world_.clear();
    light_sets_.clear();
    std::unordered_set<const ImageData *> checked_images;
    std::unordered_set<const Material *> checked_materials;
    for (const auto &light : scene.lights)
        if (!finite(light.position) || !finite(light.color) || !std::isfinite(light.radius) || light.radius < 0)
            throw std::runtime_error("Invalid scene light");
    size_t triangle_count = 0;
    for (const auto &object : scene.objects)
        if (object.active && object.visible)
            triangle_count += object.mesh.triangles.size();
    world_.reserve(triangle_count);
    for (const auto &o : scene.objects) {
        if (!o.active || !o.visible || o.mesh.triangles.empty())
            continue;
        Aabb bounds;
        const size_t light_set = light_sets_.size();
        for (const auto &source : o.mesh.triangles) {
            if (source.image && checked_images.insert(source.image.get()).second) {
                const auto &image = *source.image;
                if (image.width <= 0 || image.height <= 0 || image.width > 16384 || image.height > 16384 ||
                    image.rgba.size() != size_t(image.width) * size_t(image.height))
                    throw std::runtime_error("Invalid scene image");
            }
            if (source.material && checked_materials.insert(source.material.get()).second) {
                const auto &material = *source.material;
                if (!finite(material.base_color) || !finite(material.emissive) ||
                    !std::isfinite(material.opacity) || material.opacity < 0 || material.opacity > 1 ||
                    !std::isfinite(material.alpha_cutoff) || material.alpha_cutoff < 0 || material.alpha_cutoff > 1 ||
                    material.wrap_u < 0 || material.wrap_u > 2 || material.wrap_v < 0 || material.wrap_v > 2)
                    throw std::runtime_error("Invalid scene material");
            }
            if (!finite(source.color))
                throw std::runtime_error("Invalid triangle color");
            auto t = source;
            for (auto &v : t.v) {
                v.position = o.parent_transform.point(rotate_y(v.position, o.yaw) + o.position);
                v.normal = o.parent_transform.normal(rotate_y(v.normal, o.yaw));
                if (!finite(v.position) || !finite(v.normal) || !finite(v.color) ||
                    !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) ||
                    std::abs(v.uv.x) > 65536.f || std::abs(v.uv.y) > 65536.f)
                    throw std::runtime_error("Invalid transformed geometry");
                bounds.add(v.position);
            }
            world_.push_back({std::move(t), light_set});
        }
        // Select once per object, rather than scanning all lights per vertex.
        // A parked or distant light must not consume the local-light budget.
        LightSet selected;
        std::array<float, 4> scores{};
        for (size_t i = 0; cfg.local_lights_per_object > 0 && i < scene.lights.size(); ++i) {
            const auto &light = scene.lights[i];
            if (light.radius <= 0)
                continue;
            const Vec3 closest{std::clamp(light.position.x, bounds.lo.x, bounds.hi.x),
                               std::clamp(light.position.y, bounds.lo.y, bounds.hi.y),
                               std::clamp(light.position.z, bounds.lo.z, bounds.hi.z)};
            const float attenuation = std::max(0.f, 1 - length(light.position - closest) / light.radius);
            const float score = attenuation * attenuation * std::max({light.color.x, light.color.y, light.color.z});
            if (score <= 0)
                continue;
            const size_t budget = size_t(cfg.local_lights_per_object);
            size_t slot = std::min(selected.count, budget - 1);
            if (selected.count == budget && score <= scores[slot])
                continue;
            if (selected.count < budget)
                ++selected.count;
            while (slot > 0 && score > scores[slot - 1]) {
                scores[slot] = scores[slot - 1];
                selected.indices[slot] = selected.indices[slot - 1];
                --slot;
            }
            scores[slot] = score;
            selected.indices[slot] = i;
        }
        light_sets_.push_back(selected);
    }
    light_forward_ = scene.sun * -1;
    light_right_ = normalized(cross({0, 1, 0}, light_forward_));
    if (length(light_right_) < .1f)
        light_right_ = {1, 0, 0};
    light_up_ = cross(light_forward_, light_right_);
    shadow_size_ = cfg.shadow_size();
    shadow_extent_ = float(cfg.shadow_distance);
    float step = shadow_extent_ / shadow_size_;
    shadow_center_ = light_right_ * (std::floor(dot(camera.target, light_right_) / step) * step) +
                     light_up_ * (std::floor(dot(camera.target, light_up_) / step) * step) +
                     light_forward_ * dot(camera.target, light_forward_);

    if (cfg.shadowed_lights) {
        shadows_.assign(size_t(shadow_size_) * shadow_size_, 1e30f);
        for (const auto &prepared : world_) {
            const auto &t = prepared.geometry;
            Vec2 p[3];
            float z[3];
            for (int i = 0; i < 3; i++) {
                p[i] = {(dot(t.v[i].position - shadow_center_, light_right_) / shadow_extent_ + .5f) *
                            shadow_size_,
                        (dot(t.v[i].position - shadow_center_, light_up_) / shadow_extent_ + .5f) *
                            shadow_size_};
                z[i] = dot(t.v[i].position - shadow_center_, light_forward_);
            }
            float area = edge(p[0], p[1], p[2]);
            if (!std::isfinite(area) || std::abs(area) < 1e-6f)
                continue;
            int x0 = int(std::floor(std::clamp(std::min({p[0].x, p[1].x, p[2].x}), 0.f, float(shadow_size_)))),
                x1 = int(std::ceil(std::clamp(std::max({p[0].x, p[1].x, p[2].x}), -1.f, float(shadow_size_ - 1))));
            int y0 = int(std::floor(std::clamp(std::min({p[0].y, p[1].y, p[2].y}), 0.f, float(shadow_size_)))),
                y1 = int(std::ceil(std::clamp(std::max({p[0].y, p[1].y, p[2].y}), -1.f, float(shadow_size_ - 1))));
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    Vec2 q{x + .5f, y + .5f};
                    float a = edge(p[1], p[2], q) / area, b = edge(p[2], p[0], q) / area, c = 1 - a - b;
                    if (a >= 0 && b >= 0 && c >= 0) {
                        if (t.material && t.material->alpha_cutoff > 0) {
                            float alpha = t.material->opacity;
                            if (t.image) {
                                Vec2 uv = t.v[0].uv * a + t.v[1].uv * b + t.v[2].uv * c;
                                auto coord = [](float u, int n, int wrap) {
                                    int i = int(std::floor(u * n));
                                    if (wrap == 1)
                                        return std::clamp(i, 0, n - 1);
                                    if (wrap == 2) {
                                        i %= 2 * n;
                                        if (i < 0)
                                            i += 2 * n;
                                        return i < n ? i : 2 * n - i - 1;
                                    }
                                    i %= n;
                                    return i < 0 ? i + n : i;
                                };
                                int tx = coord(uv.x, t.image->width, t.material->wrap_u),
                                    ty = coord(uv.y, t.image->height, t.material->wrap_v);
                                alpha *= float(t.image->rgba[size_t(ty) * t.image->width + tx] >> 24) / 255;
                            }
                            if (alpha < t.material->alpha_cutoff)
                                continue;
                        }
                        float d = a * z[0] + b * z[1] + c * z[2];
                        auto &old = shadows_[size_t(y) * shadow_size_ + x];
                        old = std::min(old, d);
                    }
                }
        }
    } else
        shadows_.clear();
    frame.width = cfg.width();
    frame.height = cfg.height();
    frame.profile = cfg.profile;
    frame.crt = cfg.filter == "crt";
    std::unordered_set<const ImageData *> active_images;
    bool changed = false;
    for (const auto &prepared : world_) {
        const auto &t = prepared.geometry;
        if (t.image) {
            active_images.insert(t.image.get());
            if (!image_offsets_.count(t.image.get()))
                changed = true;
        }
    }
    if (changed || active_images.size() != image_offsets_.size() || revision_ == 0) {
        image_offsets_.clear();
        images_.clear();
        std::vector<uint32_t>().swap(texture_words_);
        for (const auto &prepared : world_) {
            const auto &t = prepared.geometry;
            if (t.image && !image_offsets_.count(t.image.get())) {
                image_offsets_[t.image.get()] = texture_words_.size();
                images_.push_back(t.image);
                texture_words_.insert(texture_words_.end(), t.image->rgba.begin(), t.image->rgba.end());
            }
        }
        static std::atomic<uint64_t> next{1};
        revision_ = next.fetch_add(1);
    }
    if (frame.texture_revision != revision_) {
        frame.resources = std::vector<uint32_t>(texture_words_);
        frame.texture_revision = revision_;
    }
    frame.texture_words = texture_words_.size();
    frame.resources.resize(std::max<size_t>(1, frame.texture_words + shadows_.size()));
    if (!shadows_.empty())
        std::memcpy(frame.resources.data() + frame.texture_words, shadows_.data(), shadows_.size() * 4);
    auto &settings = frame.settings;
    settings.profile = int(cfg.profile);
    settings.crt = frame.crt;
    settings.height = frame.height;
    settings.shadow_size = shadows_.empty() ? 0 : shadow_size_;
    settings.shadow_offset = int(frame.texture_words);
    settings.right[3] = shadow_extent_;
    settings.grid = cfg.profile == Profile::Pixel ? 1.f / (2 + 30.f * (cfg.lighting_quality - 1) / 99) : 0;
    for (int i = 0; i < 3; i++) {
        settings.right[i] = i == 0 ? light_right_.x : i == 1 ? light_right_.y : light_right_.z;
        settings.up[i] = i == 0 ? light_up_.x : i == 1 ? light_up_.y : light_up_.z;
        settings.forward[i] = i == 0 ? light_forward_.x : i == 1 ? light_forward_.y : light_forward_.z;
        settings.center[i] = i == 0 ? shadow_center_.x : i == 1 ? shadow_center_.y : shadow_center_.z;
    }
    frame.vertices.clear();
    frame.vertices.reserve(6 + world_.size() * 3);
    // Shade the background through the same profile/filter path as geometry.
    // Very close to the far plane; the demo's content stays well inside it.
    for (Vec2 p : {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, -1}, Vec2{1, 1}, Vec2{-1, 1}})
        frame.vertices.push_back({p.x, p.y, .999999f, 1, 0, 0, .09f, .14f, .21f, 0});
    Vec3 f = normalized(camera.target - camera.position), right = normalized(cross({0, 1, 0}, f)),
         up = cross(f, right);
    if (length(right) < 1e-6f) {
        right = {1, 0, 0};
        up = cross(f, right);
    }
    float sy = 1 / std::tan(camera.fov * .5f), sx = sy * float(frame.height) / frame.width;
    frame.depth_mode = cfg.profile == Profile::PS1 && cfg.ps1_depth_test
        ? DepthMode::Enabled : DepthMode::Automatic;
    if (cfg.profile == Profile::PS1 && !cfg.ps1_depth_test) {
        for (auto &triangle : world_) {
            triangle.sort_depth = 0;
            for (int i = 0; i < 3; ++i)
                triangle.sort_depth += dot(triangle.geometry.v[i].position - camera.position, f);
        }
        std::stable_sort(world_.begin(), world_.end(), [](const PreparedTriangle &a, const PreparedTriangle &b) {
            return a.sort_depth > b.sort_depth;
        });
    }
    // Camera-space planes are invariant for the frame. The cheap triangle
    // reject below skips lighting and polygon clipping only when all three
    // vertices are outside the same plane; shadow generation still uses the
    // complete world geometry above.
    const std::array<std::pair<Vec3, float>, 6> planes = {{{{0, 0, 1}, -camera.near_plane},
                                                            {{0, 0, -1}, camera.far_plane},
                                                            {{sx, 0, 1}, 0},
                                                            {{-sx, 0, 1}, 0},
                                                            {{0, sy, 1}, 0},
                                                            {{0, -sy, 1}, 0}}};
    for (const auto &prepared : world_) {
        const auto &t = prepared.geometry;
        const auto &lights = light_sets_[prepared.light_set];
        Vec3 normal = normalized(cross(t.v[1].position - t.v[0].position, t.v[2].position - t.v[0].position));
        if (t.material && !t.material->double_sided && dot(normal, camera.position - t.v[0].position) <= 0)
            continue;
        Vec3 camera_space[3];
        for (int i = 0; i < 3; i++) {
            Vec3 relative = t.v[i].position - camera.position;
            camera_space[i] = {dot(relative, right), dot(relative, up), dot(relative, f)};
        }
        bool outside = false;
        for (const auto &plane : planes) {
            bool all_outside = true;
            for (const auto &point : camera_space)
                if (dot(point, plane.first) + plane.second >= 0) {
                    all_outside = false;
                    break;
                }
            if (all_outside) {
                outside = true;
                break;
            }
        }
        if (outside)
            continue;
        ClipVertex a[16], b[16];
        for (int i = 0; i < 3; i++) {
            Vec3 p = t.v[i].position, lighting_p = p;
            Vec3 vn = length(t.v[i].normal) > .01f ? normalized(t.v[i].normal) : normal;
            if (cfg.profile == Profile::Pixel)
                lighting_p = {std::floor(p.x * 4) / 4, std::floor(p.y * 4) / 4, std::floor(p.z * 4) / 4};
            float ndl = std::max(0.f, dot(vn, scene.sun));
            if (cfg.profile == Profile::Pixel)
                ndl = std::floor(ndl * (cfg.toon_bands - 1) + .5f) / (cfg.toon_bands - 1);
            Vec3 light = mix({.18f, .12f, .24f}, {.24f, .29f, .34f}, vn.y * .5f + .5f);
            for (size_t k = 0; k < lights.count; k++) {
                const auto &l = scene.lights[lights.indices[k]];
                Vec3 d = l.position - lighting_p;
                float dist = length(d);
                float attenuation = std::max(0.f, 1 - dist / l.radius);
                light = light +
                        l.color * (std::max(0.f, dot(vn, normalized(d))) * attenuation * attenuation * 2.5f);
            }
            Vec3 cp = camera_space[i];
            Vec3 base = mul(t.color, t.v[i].color);
            Vec3 col = mul(base, light), sun = mul(base, Vec3{.9f, .82f, .65f} * ndl);
            if (t.material) {
                col = col + t.material->emissive;
                if (t.material->unlit) {
                    col = base + t.material->emissive;
                    sun = {};
                }
            }
            if (cfg.profile == Profile::N64) {
                float fog = clamp((cp.z - 12) / 32);
                col = mix(col, {.14f, .23f, .3f}, fog);
                sun = sun * (1 - fog);
            }
            a[i] = {cp, t.v[i].uv, col, p, normal, sun};
        }
        int n = 3;
        for (const auto &plane : planes) {
            n = clip_plane(a, n, b, plane.first, plane.second);
            std::copy(b, b + n, a);
            if (!n)
                break;
        }
        auto emit = [&](ClipVertex v) {
            float iw = 1 / v.p.z;
            float x = v.p.x * sx * iw, y = -v.p.y * sy * iw;
            if (cfg.profile == Profile::PS1) {
                x = (std::floor((x * .5f + .5f) * frame.width) * 2 / frame.width) - 1;
                y = (std::floor((y * .5f + .5f) * frame.height) * 2 / frame.height) - 1;
            }
            float interpolation = cfg.profile == Profile::PS1 ? 1 : iw;
            float z = camera.far_plane / (camera.far_plane - camera.near_plane) -
                      (camera.far_plane * camera.near_plane) / (camera.far_plane - camera.near_plane) * iw;
            frame.vertices.push_back({x, y, z, interpolation, v.uv.x * interpolation, v.uv.y * interpolation,
                                      v.color.x * interpolation, v.color.y * interpolation,
                                      v.color.z * interpolation, t.checker ? 1.f : 0.f});
            auto &dst = frame.vertices.back();
            dst.world = v.world * interpolation;
            dst.normal = v.normal * interpolation;
            dst.sun_color = v.sun * interpolation;
            if (t.image) {
                dst.texture_offset = float(image_offsets_.at(t.image.get()));
                dst.texture_width = float(t.image->width);
                dst.texture_height = float(t.image->height);
            }
            if (t.material) {
                dst.wrap = float(t.material->wrap_u + 4 * t.material->wrap_v);
                dst.alpha_cutoff = t.material->alpha_cutoff;
                dst.opacity = t.material->opacity;
            }
        };
        for (int i = 1; i < n - 1; i++) {
            emit(a[0]);
            emit(a[i]);
            emit(a[i + 1]);
        }
    }
}
Vec3 texture_sample(float u, float v, Profile profile) {
    auto texel = [](int x, int y) {
        int c = ((x / 2) & 1) ^ ((y / 2) & 1);
        return Vec3{1, 1, 1} * (c ? .68f : 1.f);
    };
    float x = u * 8, y = v * 8;
    if (profile != Profile::N64)
        return texel(int(std::floor(x)), int(std::floor(y)));
    x -= .5f;
    y -= .5f;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    if (fx + fy <= 1)
        return texel(ix, iy) * (1 - fx - fy) + texel(ix + 1, iy) * fx + texel(ix, iy + 1) * fy;
    return texel(ix + 1, iy + 1) * (fx + fy - 1) + texel(ix, iy + 1) * (1 - fx) +
           texel(ix + 1, iy) * (1 - fy);
}
uint32_t finish_pixel(Vec3 c, int x, int y, Profile profile, bool crt) {
    if (profile == Profile::PS1) {
        static constexpr int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        float d = (bayer[(y & 3) * 4 + (x & 3)] / 16.f - .5f) / 31;
        c = {std::floor(clamp(c.x + d) * 31 + .5f) / 31, std::floor(clamp(c.y + d) * 31 + .5f) / 31,
             std::floor(clamp(c.z + d) * 31 + .5f) / 31};
    }
    if (crt) {
        c = c * ((y & 1) ? .72f : 1.f);
        float m = .86f;
        switch (x % 3) {
        case 0:
            c = mul(c, {1, m, m});
            break;
        case 1:
            c = mul(c, {m, 1, m});
            break;
        default:
            c = mul(c, {m, m, 1});
        }
    }
    return uint32_t(clamp(c.x) * 255 + .5f) | (uint32_t(clamp(c.y) * 255 + .5f) << 8) |
           (uint32_t(clamp(c.z) * 255 + .5f) << 16) | 0xff000000u;
}
void write_ppm(const std::string &file, int w, int h, const std::vector<uint32_t> &pixels) {
    std::ofstream out(file, std::ios::binary);
    if (!out || pixels.size() != size_t(w) * h)
        throw std::runtime_error("Invalid image output");
    out << "P6\n" << w << " " << h << "\n255\n";
    for (auto p : pixels) {
        char rgb[] = {char(p & 255), char((p >> 8) & 255), char((p >> 16) & 255)};
        out.write(rgb, 3);
    }
    if (!out)
        throw std::runtime_error("Failed writing image");
}
std::unique_ptr<Backend> make_backend(const std::string &name, int workers) {
    if (name == "cpu")
        return make_cpu(workers);
#ifdef EMBER_VULKAN
    if (name == "vulkan")
        return make_vulkan();
#endif
#ifdef EMBER_OPENGL
    if (name == "opengl")
        return make_opengl();
#endif
    if (name == "auto") {
#ifdef EMBER_VULKAN
        try {
            return make_vulkan();
        } catch (const std::exception &e) {
            std::cerr << "Vulkan unavailable: " << e.what() << "\n";
        }
#endif
#ifdef EMBER_OPENGL
        try {
            return make_opengl();
        } catch (const std::exception &e) {
            std::cerr << "OpenGL unavailable: " << e.what() << "\n";
        }
#endif
        std::cerr << "No GPU backend available; falling back to Ember's CPU rasterizer.\n";
        return make_cpu(workers);
    }
    throw std::runtime_error("Backend not compiled: " + name);
}
} // namespace ember
