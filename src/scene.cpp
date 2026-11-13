#include "ember/scene.hpp"
#include "ember/asset.hpp"
#include <atomic>
#include <stdexcept>
namespace ember {
uint64_t next_object_id() {
    static std::atomic<uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    do {
        if (value == UINT64_MAX)
            throw std::runtime_error("Scene object ID space exhausted");
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return value;
}
Object::Object(const Object &o)
    : mesh(o.mesh), position(o.position), yaw(o.yaw),
      animator(o.animator ? std::make_shared<Animator>(*o.animator) : nullptr),
      parent_transform(o.parent_transform), active(o.active), visible(o.visible) {}
Object &Object::operator=(const Object &o) {
    if (this != &o) {
        Object copy(o);
        *this = std::move(copy);
    }
    return *this;
}

static void quad(Mesh &m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, bool check = false, float uv = 1) {
    m.triangles.push_back({{{a, {0, 0}}, {b, {uv, 0}}, {c, {uv, uv}}}, color, check});
    m.triangles.push_back({{{a, {0, 0}}, {c, {uv, uv}}, {d, {0, uv}}}, color, check});
}
Mesh box(Vec3 s, Vec3 color, bool check) {
    Mesh m;
    s = s * .5f;
    Vec3 p[8];
    for (int i = 0; i < 8; i++)
        p[i] = {(i & 1) ? s.x : -s.x, (i & 2) ? s.y : -s.y, (i & 4) ? s.z : -s.z};
    quad(m, p[0], p[4], p[6], p[2], color, check);
    quad(m, p[5], p[1], p[3], p[7], color, check);
    quad(m, p[2], p[6], p[7], p[3], color, check);
    quad(m, p[4], p[0], p[1], p[5], color, check);
    quad(m, p[0], p[2], p[3], p[1], color, check);
    quad(m, p[6], p[4], p[5], p[7], color, check);
    return m;
}
Scene demo_scene() {
    Scene s;
    Object floor;
    for (int z = -14; z < 14; z++)
        for (int x = -14; x < 14; x++) {
            float k = ((x + z) & 1) ? 1 : .86f;
            quad(floor.mesh, {float(x), 0, float(z)}, {float(x), 0, float(z + 1)},
                 {float(x + 1), 0, float(z + 1)}, {float(x + 1), 0, float(z)}, Vec3{.27f, .37f, .43f} * k,
                 false);
        }
    s.objects.push_back(std::move(floor));
    s.objects.push_back({box({2, 2, 2}, {.9f, .34f, .14f}, true), {0, 2, 0}, 0});
    for (int x : {-5, 5})
        for (int z : {0, 6}) {
            s.objects.push_back({box({1.1f, 4, 1.1f}, {.42f, .55f, .64f}), {float(x), 2, float(z)}});
            s.objects.push_back({box({1.8f, .4f, 1.8f}, {.72f, .69f, .48f}), {float(x), 4.1f, float(z)}});
        }
    s.objects.push_back({box({11, .6f, 1.4f}, {.44f, .6f, .66f}), {0, 4.65f, 6}});
    s.objects.push_back({box({5, .4f, 5}, {.46f, .54f, .60f}), {0, .2f, 0}});
    for (int i = 0; i < 5; i++)
        s.objects.push_back({box({3, .3f, 1}, {.48f, .57f, .63f}), {-8, .15f + i * .3f, 3.f + i}});
    for (int i = 0; i < 12; i++) {
        float a = i * 6.2831853f / 12;
        float r = 10;
        s.objects.push_back({box({1, 1.2f + (i % 3), 1}, {.2f, .45f, .43f}),
                             {std::cos(a) * r, .6f + (i % 3) * .5f, std::sin(a) * r},
                             a});
    }
    s.lights = {{{-3, 2, -2}, {.12f, .7f, 1}, 6}, {{3, 2, 2}, {1, .22f, .08f}, 6}};
    return s;
}
void demo_animate(Scene &s, double time) {
    s.objects[1].yaw = float(time * .55);
    s.objects[1].position.y = 2 + std::sin(float(time)) * 0.3f;
    s.lights[0].position = {std::cos(float(time)) * 4, 2, -2 + std::sin(float(time)) * 3};
}
} // namespace ember
