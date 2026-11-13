#pragma once
#include "math.hpp"
#include <cstdint>
#include <memory>
#include <vector>
namespace ember {
struct Material;
struct ImageData;
class Animator;
// Process-wide identity for scene objects. IDs are never reused during a
// process, so a stale index cannot silently alias a replacement object.
uint64_t next_object_id();
struct Vertex {
    Vec3 position;
    Vec2 uv;
    Vec3 normal{};
    Vec3 color{1, 1, 1};
};
struct Triangle {
    Vertex v[3];
    Vec3 color{1, 1, 1};
    bool checker = false;
    std::shared_ptr<const Material> material{};
    std::shared_ptr<const ImageData> image{};
};
struct Mesh {
    std::vector<Triangle> triangles;
};
struct Object {
    Object() = default;
    Object(Mesh data, Vec3 pos = {}, float angle = 0) : mesh(std::move(data)), position(pos), yaw(angle) {}
    Object(const Object &);
    Object &operator=(const Object &);
    Object(Object &&) noexcept = default;
    Object &operator=(Object &&) noexcept = default;
    Mesh mesh;
    Vec3 position;
    float yaw = 0;
    std::shared_ptr<Animator> animator{};
    Mat4 parent_transform{};
    // Runtime destruction leaves a stable, reusable slot in Scene::objects.
    // Render/animation paths must ignore inactive slots.
    bool active = true;
    bool visible = true; // rendering only; never invalidates identity or colliders
    // Copies are distinct objects and receive a fresh ID. Moves preserve it.
    uint64_t id = next_object_id();
};
struct Light {
    Vec3 position, color;
    float radius = 5;
};
struct Scene {
    std::vector<Object> objects;
    std::vector<Light> lights;
    Vec3 sun = normalized({-0.5f, 0.9f, -0.35f});
};
struct Camera {
    Vec3 position{11, 8, -15}, target{0, 1, 0};
    float near_plane = .15f, far_plane = 90, fov = 1.0f;
};
Mesh box(Vec3 size, Vec3 color, bool checker = false);
Scene demo_scene();
void demo_animate(Scene &scene, double time);
} // namespace ember
