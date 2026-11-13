#include "ember/asset.hpp"
#include <stdexcept>
namespace ember {
Vec3 Mat4::point(Vec3 p) const {
    return vector(p) + Vec3{m[12], m[13], m[14]};
}
Vec3 Mat4::vector(Vec3 p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
            m[2] * p.x + m[6] * p.y + m[10] * p.z};
}
Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 c;
    c.m.fill(0);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 4; k++)
                c.m[j * 4 + i] += a.m[k * 4 + i] * b.m[j * 4 + k];
    return c;
}
static Quat unit(Quat q) {
    float d = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return d > 1e-10f ? Quat{q.x / d, q.y / d, q.z / d, q.w / d} : Quat{};
}
Quat slerp(Quat a, Quat b, float t) {
    a = unit(a);
    b = unit(b);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    float u = 1 - t, v = t;
    if (d < .9995f) {
        float angle = std::acos(clamp(d, -1, 1));
        float den = std::sin(angle);
        u = std::sin((1 - t) * angle) / den;
        v = std::sin(t * angle) / den;
    }
    return unit({a.x * u + b.x * v, a.y * u + b.y * v, a.z * u + b.z * v, a.w * u + b.w * v});
}
Mat4 matrix(const Transform &t) {
    auto q = unit(t.rotation);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r;
    r.m = {(1 - 2 * (y * y + z * z)) * t.scale.x,
           2 * (x * y + z * w) * t.scale.x,
           2 * (x * z - y * w) * t.scale.x,
           0,
           2 * (x * y - z * w) * t.scale.y,
           (1 - 2 * (x * x + z * z)) * t.scale.y,
           2 * (y * z + x * w) * t.scale.y,
           0,
           2 * (x * z + y * w) * t.scale.z,
           2 * (y * z - x * w) * t.scale.z,
           (1 - 2 * (x * x + y * y)) * t.scale.z,
           0,
           t.translation.x,
           t.translation.y,
           t.translation.z,
           1};
    return r;
}
Vec3 Mat4::normal(Vec3 n) const {
    const Mat4 &m = *this;
    Vec3 a{m.m[0], m.m[1], m.m[2]}, b{m.m[4], m.m[5], m.m[6]}, c{m.m[8], m.m[9], m.m[10]};
    float d = dot(a, cross(b, c));
    if (std::abs(d) < 1e-12f)
        return normalized(m.vector(n));
    return normalized((cross(b, c) * n.x + cross(c, a) * n.y + cross(a, b) * n.z) / d);
}
static std::array<float, 4> sample(const Track &t, float time) {
    if (t.keys.empty())
        return {};
    auto it = std::upper_bound(t.keys.begin(), t.keys.end(), time,
                               [](float v, const Key &k) { return v < k.time; });
    if (it == t.keys.begin())
        return it->value;
    if (it == t.keys.end())
        return t.keys.back().value;
    const auto &a = *(it - 1);
    const auto &b = *it;
    float dt = b.time - a.time, u = dt > 0 ? (time - a.time) / dt : 0;
    if (t.interpolation == 0)
        return a.value;
    std::array<float, 4> r{};
    if (t.path == 1 && t.interpolation == 1) {
        auto q = slerp({a.value[0], a.value[1], a.value[2], a.value[3]},
                       {b.value[0], b.value[1], b.value[2], b.value[3]}, u);
        return {q.x, q.y, q.z, q.w};
    }
    for (int i = 0; i < 4; i++) {
        if (t.interpolation == 2) {
            float u2 = u * u, u3 = u2 * u;
            r[i] = (2 * u3 - 3 * u2 + 1) * a.value[i] + (u3 - 2 * u2 + u) * dt * a.out[i] +
                   (-2 * u3 + 3 * u2) * b.value[i] + (u3 - u2) * dt * b.in[i];
        } else
            r[i] = a.value[i] * (1 - u) + b.value[i] * u;
    }
    return r;
}
Animator::Animator(std::shared_ptr<const Asset> a) : asset_(std::move(a)) {
    if (!asset_)
        throw std::runtime_error("Missing asset");
    locals_.resize(asset_->nodes.size());
    globals_.resize(locals_.size());
    evaluate();
}
void Animator::play(const std::string &name, bool loop, float fade) {
    if (!std::isfinite(fade) || fade < 0)
        throw std::runtime_error("Invalid animation fade");
    int found = -1;
    for (size_t i = 0; i < asset_->clips.size(); i++)
        if (asset_->clips[i].name == name) {
            found = int(i);
            break;
        }
    if (found < 0)
        throw std::runtime_error("Animation clip not found: " + name);
    evaluate();
    from_ = locals_;
    clip_ = found;
    time_ = 0;
    fade_ = 0;
    fade_duration_ = fade;
    loop_ = loop;
    paused_ = false;
    dirty_ = true;
    mesh_dirty_ = true;
}
void Animator::seek(float t) {
    if (!std::isfinite(t) || t < 0)
        throw std::runtime_error("Invalid animation time");
    time_ = t;
    dirty_ = true;
    mesh_dirty_ = true;
}
bool Animator::advance(float dt) {
    if (!std::isfinite(dt) || dt < 0)
        throw std::runtime_error("Invalid animation delta");
    if (paused_ || clip_ < 0 || dt == 0)
        return mesh_dirty_;
    float duration = asset_->clips[clip_].duration;
    if (!loop_ && time_ >= duration && fade_ >= fade_duration_)
        return mesh_dirty_;
    time_ += dt;
    fade_ += dt;
    if (loop_ && duration > 0)
        time_ = std::fmod(time_, duration);
    else
        time_ = std::min(time_, duration);
    dirty_ = true;
    mesh_dirty_ = true;
    return true;
}
void Animator::evaluate() {
    if (!dirty_)
        return;
    for (size_t i = 0; i < locals_.size(); i++)
        locals_[i] = asset_->nodes[i].local;
    if (clip_ >= 0) {
        const auto &clip = asset_->clips[clip_];
        float t = time_;
        if (loop_ && clip.duration > 0)
            t = std::fmod(t, clip.duration);
        else
            t = std::min(t, clip.duration);
        for (auto &track : clip.tracks) {
            if (track.keys.empty())
                continue;
            auto v = sample(track, t);
            auto &dst = locals_.at(track.node);
            if (track.path == 0)
                dst.translation = {v[0], v[1], v[2]};
            else if (track.path == 2)
                dst.scale = {v[0], v[1], v[2]};
            else
                dst.rotation = unit({v[0], v[1], v[2], v[3]});
        }
    }
    if (fade_duration_ > 0 && fade_ < fade_duration_ && from_.size() == locals_.size()) {
        float t = fade_ / fade_duration_;
        for (size_t i = 0; i < locals_.size(); i++) {
            auto &b = locals_[i];
            const auto &a = from_[i];
            b.translation = mix(a.translation, b.translation, t);
            b.scale = mix(a.scale, b.scale, t);
            b.rotation = slerp(a.rotation, b.rotation, t);
        }
    }
    // Importers/cooker enforce parent-before-child order.
    for (size_t i = 0; i < locals_.size(); i++) {
        globals_[i] = matrix(locals_[i]);
        int p = asset_->nodes[i].parent;
        if (p >= 0)
            globals_[i] = globals_.at(p) * globals_[i];
    }
    dirty_ = false;
}
const std::vector<Mat4> &Animator::transforms() {
    evaluate();
    return globals_;
}
Mat4 Animator::node_transform(const std::string &name) {
    evaluate();
    for (size_t i = 0; i < asset_->nodes.size(); i++)
        if (asset_->nodes[i].name == name)
            return globals_[i];
    throw std::runtime_error("Bone/node not found: " + name);
}
void Animator::mesh(Mesh &out) {
    evaluate();
    size_t count = 0;
    for (auto &p : asset_->primitives)
        count += p.indices.size() / 3;
    out.triangles.resize(count);
    size_t ti = 0;
    for (const auto &p : asset_->primitives) {
        positions_.resize(p.vertices.size());
        normals_.resize(p.vertices.size());
        palette_.resize(p.joints.size());
        auto &palette = palette_;
        for (size_t i = 0; i < palette.size(); i++)
            palette[i] = globals_.at(p.joints[i]) * p.inverse_bind.at(i);
        Mat4 base = globals_.at(p.node) * p.geometry;
        for (size_t i = 0; i < p.vertices.size(); i++) {
            auto &v = p.vertices[i];
            float total = 0;
            Vec3 pos{}, n{};
            for (int k = 0; k < 4; k++)
                if (v.weights[k] > 0) {
                    const auto &m = palette.at(v.joints[k]);
                    pos = pos + m.point(v.position) * v.weights[k];
                    n = n + m.normal(v.normal) * v.weights[k];
                    total += v.weights[k];
                }
            positions_[i] = total > 0 ? pos / total : base.point(v.position);
            normals_[i] = total > 0 ? normalized(n) : base.normal(v.normal);
        }
        auto material = std::shared_ptr<const Material>(asset_, &asset_->materials.at(p.material));
        for (size_t i = 0; i < p.indices.size(); i += 3) {
            auto &t = out.triangles[ti++];
            t.color = material->base_color;
            t.checker = false;
            t.material = material;
            t.image = material->image >= 0 ? asset_->images.at(material->image) : nullptr;
            for (int k = 0; k < 3; k++) {
                size_t ix = p.indices[i + k];
                t.v[k] = {positions_[ix], p.vertices[ix].uv, normals_[ix], p.vertices[ix].color};
            }
        }
    }
    mesh_dirty_ = false;
}
Object instantiate(std::shared_ptr<const Asset> a, Vec3 p) {
    Object o;
    o.position = p;
    o.animator = std::make_shared<Animator>(std::move(a));
    o.animator->mesh(o.mesh);
    return o;
}
void update_animation(Object &o, float dt) {
    if (o.animator && o.animator->advance(dt))
        o.animator->mesh(o.mesh);
}
} // namespace ember
