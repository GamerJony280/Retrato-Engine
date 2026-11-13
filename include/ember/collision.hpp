#pragma once
#include "scene.hpp"
#include <limits>
#include <string>
namespace ember {
struct Hit {
    float distance = std::numeric_limits<float>::infinity();
    Vec3 position, normal;
    int triangle = -1;
};
class CollisionMesh {
    struct CollisionTriangle {
        Vec3 v[3];
        uint32_t source = 0;
    };
    struct Node {
        Aabb bounds;
        uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    // Positions only: collision data must not retain render materials or textures.
    std::vector<CollisionTriangle> triangles_;
    std::vector<Node> nodes_;
    int leaf_size_ = 4;
    uint32_t build(uint32_t first, uint32_t count);

  public:
    void cook(const Mesh &, int quality = 70);
    bool raycast(Vec3 origin, Vec3 direction, float max_distance, Hit &) const;
    void save(const std::string &) const;
    static CollisionMesh load(const std::string &);
    size_t triangle_count() const { return triangles_.size(); }
};
Mesh load_obj(const std::string &, Vec3 color = {.65f, .7f, .8f});
// Continuous segment vs expanded box. Exact for a point; conservative at
// rounded sphere corners. Returns t in [0,1], and the contacted slab normal.
bool sweep_sphere_box(Vec3 from, Vec3 delta, float radius, Aabb box, float &t, Vec3 &normal);
struct SlideResult {
    Vec3 position{}, remaining{}, normal{};
    int contacts = 0;
};
// Minimal arcade response for characters/projectiles against static boxes.
// It does not provide mass, impulses, stacking, joints or dynamic bodies.
SlideResult slide_sphere_boxes(Vec3 from, Vec3 delta, float radius, const std::vector<Aabb> &boxes,
                               int iterations = 4);
} // namespace ember
