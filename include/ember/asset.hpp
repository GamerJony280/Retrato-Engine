#pragma once
#include "scene.hpp"
#include <array>
#include <string>
namespace ember {
struct Transform {
    Vec3 translation{}, scale{1, 1, 1};
    Quat rotation{};
};
Mat4 matrix(const Transform &);
Quat slerp(Quat, Quat, float);
struct ImageData {
    int width = 0, height = 0;
    std::vector<uint32_t> rgba;
};
struct Material {
    std::string name;
    Vec3 base_color{1, 1, 1}, emissive{};
    float opacity = 1, alpha_cutoff = 0;
    int image = -1;
    bool unlit = false, double_sided = true;
    int wrap_u = 0, wrap_v = 0; // repeat=0, clamp=1, mirror=2
};
struct AssetVertex {
    Vec3 position, normal;
    Vec2 uv;
    Vec3 color{1, 1, 1};
    std::array<uint32_t, 4> joints{};
    std::array<float, 4> weights{};
};
struct Primitive {
    int node = 0, material = 0;
    std::vector<AssetVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<int> joints;
    std::vector<Mat4> inverse_bind;
    Mat4 geometry;
};
struct AssetNode {
    std::string name;
    int parent = -1;
    Transform local;
};
struct Key {
    float time = 0;
    std::array<float, 4> value{}, in{}, out{};
};
struct Track {
    int node = 0, path = 0; // translation=0, rotation=1, scale=2
    int interpolation = 1;  // step=0, linear=1, cubic=2
    std::vector<Key> keys;
};
struct Clip {
    std::string name;
    float duration = 0;
    std::vector<Track> tracks;
};
struct Asset {
    std::vector<AssetNode> nodes;
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<std::shared_ptr<const ImageData>> images;
    std::vector<Clip> clips;
    std::vector<std::string> warnings;
};
struct ImportOptions {
    size_t max_vertices = 100000, max_triangles = 100000, max_nodes = 2048;
    int max_texture_size = 2048;
    size_t max_image_bytes = 64 * 1024 * 1024;
    float bake_hz = 30;
};
std::shared_ptr<const Asset> import_asset(const std::string &, const ImportOptions & = {});
void save_asset(const Asset &, const std::string &);
std::shared_ptr<const Asset> load_asset(const std::string &, const ImportOptions & = {});
class Animator {
    std::shared_ptr<const Asset> asset_;
    std::vector<Transform> locals_, from_;
    std::vector<Mat4> globals_, palette_;
    std::vector<Vec3> positions_, normals_;
    int clip_ = -1;
    float time_ = 0, fade_ = 0, fade_duration_ = 0;
    bool loop_ = true, paused_ = false, dirty_ = true, mesh_dirty_ = true;
    void evaluate();

  public:
    explicit Animator(std::shared_ptr<const Asset>);
    void play(const std::string &, bool loop = true, float fade_seconds = 0);
    void seek(float);
    void pause(bool v) { paused_ = v; }
    // Returns true when the pose changed and the instance mesh must be refreshed.
    bool advance(float);
    void mesh(Mesh &);
    Mat4 node_transform(const std::string &);
    const std::vector<Mat4> &transforms();
    const Asset &asset() const { return *asset_; }
    float time() const { return time_; }
    int clip_index() const { return clip_; }
};
Object instantiate(std::shared_ptr<const Asset>, Vec3 position = {});
void update_animation(Object &, float dt);
} // namespace ember
