#include <filesystem>
#include "ember/collision.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace ember {
namespace {
bool finite(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool valid_box(Aabb b) {return finite(b.lo)&&finite(b.hi)&&b.lo.x<=b.hi.x&&b.lo.y<=b.hi.y&&b.lo.z<=b.hi.z;}
float axis(Vec3 p, int a) {
    return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
bool slab(Aabb b, Vec3 p, Vec3 d, float maximum, float &enter, Vec3 &normal) {
    float exit = maximum;
    enter = 0;
    normal = {};
    for (int i = 0; i < 3; i++) {
        float v = axis(d, i), o = axis(p, i), lo = axis(b.lo, i), hi = axis(b.hi, i);
        if (std::abs(v) < 1e-9f) {
            if (o < lo || o > hi)
                return false;
            continue;
        }
        float a = (lo - o) / v, c = (hi - o) / v;
        float sign = -1;
        if (a > c) {
            std::swap(a, c);
            sign = 1;
        }
        if (a > enter) {
            enter = a;
            normal = {};
            if (i == 0)
                normal.x = sign;
            else if (i == 1)
                normal.y = sign;
            else
                normal.z = sign;
        }
        exit = std::min(exit, c);
        if (enter > exit)
            return false;
    }
    return exit >= 0;
}
template <class TriangleType>
bool ray_triangle(Vec3 o, Vec3 d, const TriangleType &t, float &distance, Vec3 &normal) {
    Vec3 a = t.v[1] - t.v[0], b = t.v[2] - t.v[0], p = cross(d, b);
    float det = dot(a, p);
    if (std::abs(det) < 1e-8f)
        return false;
    float inv = 1 / det;
    Vec3 s = o - t.v[0];
    float u = dot(s, p) * inv;
    if (u < 0 || u > 1)
        return false;
    Vec3 q = cross(s, a);
    float v = dot(d, q) * inv;
    if (v < 0 || u + v > 1)
        return false;
    float z = dot(b, q) * inv;
    if (z < 0 || z > distance)
        return false;
    distance = z;
    normal = normalized(cross(a, b));
    if (dot(normal, d) > 0)
        normal = normal * -1;
    return true;
}
void put32(std::ostream &s, uint32_t v) {
    for (int i = 0; i < 4; i++)
        s.put(char(v >> (8 * i)));
}
uint32_t get32(std::istream &s) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        int c = s.get();
        if (c == EOF)
            throw std::runtime_error("Truncated collision file");
        v |= uint32_t(c) << (i * 8);
    }
    return v;
}
void put_float(std::ostream &s, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(s, u);
}
float get_float(std::istream &s) {
    uint32_t u = get32(s);
    float f;
    std::memcpy(&f, &u, 4);
    if (!std::isfinite(f))
        throw std::runtime_error("Non-finite geometry");
    return f;
}
} // namespace
uint32_t CollisionMesh::build(uint32_t first, uint32_t count) {
    uint32_t id = uint32_t(nodes_.size());
    nodes_.push_back({});
    Aabb b;
    for (uint32_t i = first; i < first + count; i++)
        for (auto v : triangles_[i].v)
            b.add(v);
    nodes_[id].bounds = b;
    if (count <= uint32_t(leaf_size_)) {
        nodes_[id].first = first;
        nodes_[id].count = count;
        return id;
    }
    Vec3 extent = b.hi - b.lo;
    int a = extent.y > extent.x ? 1 : 0;
    if (extent.z > axis(extent, a))
        a = 2;
    uint32_t mid = first + count / 2;
    std::nth_element(triangles_.begin() + first, triangles_.begin() + mid, triangles_.begin() + first + count,
                     [&](const CollisionTriangle &x, const CollisionTriangle &y) {
                         return axis(x.v[0] + x.v[1] + x.v[2], a) < axis(y.v[0] + y.v[1] + y.v[2], a);
                     });
    uint32_t left = build(first, mid - first), right = build(mid, first + count - mid);
    nodes_[id].left = left;
    nodes_[id].right = right;
    return id;
}
void CollisionMesh::cook(const Mesh &m, int quality) {
    if (quality < 1 || quality > 100)
        throw std::runtime_error("Collision quality must be 1..100");
    leaf_size_ = 2 + (100 - quality) * 14 / 99;
    triangles_.resize(m.triangles.size());
    for (size_t i = 0; i < m.triangles.size(); ++i) {
        triangles_[i].source = uint32_t(i);
        for (int k = 0; k < 3; ++k)
            triangles_[i].v[k] = m.triangles[i].v[k].position;
    }
    nodes_.clear();
    nodes_.reserve(triangles_.size() * 2);
    if (!triangles_.empty())
        build(0, uint32_t(triangles_.size()));
}
bool CollisionMesh::raycast(Vec3 origin, Vec3 direction, float max_distance, Hit &hit) const {
    hit = Hit{};
    if(!finite(origin)||!finite(direction)||!std::isfinite(max_distance))
        throw std::invalid_argument("Non-finite raycast parameters");
    double magnitude=std::sqrt(double(direction.x)*direction.x+double(direction.y)*direction.y+double(direction.z)*direction.z);
    if(nodes_.empty()||magnitude<1e-8||max_distance<0) return false;
    direction={float(direction.x/magnitude),float(direction.y/magnitude),float(direction.z/magnitude)};
    hit.distance = max_distance;
    std::array<uint32_t, 128> stack{};
    int count = 1;
    stack[0] = 0;
    bool found = false;
    while (count) {
        const auto &n = nodes_[stack[--count]];
        float enter;
        Vec3 normal;
        if (!slab(n.bounds, origin, direction, hit.distance, enter, normal))
            continue;
        if (n.count) {
            for (uint32_t i = n.first; i < n.first + n.count; i++)
                if (ray_triangle(origin, direction, triangles_[i], hit.distance, normal)) {
                    found = true;
                    hit.triangle = int(triangles_[i].source);
                    hit.normal = normal;
                }
        } else {
            stack[count++] = n.left;
            stack[count++] = n.right;
        }
    }
    if (found)
        hit.position = origin + direction * hit.distance;
    return found;
}
bool sweep_sphere_box(Vec3 from, Vec3 delta, float radius, Aabb box, float &t, Vec3 &normal) {
    if(!finite(from)||!finite(delta)||!finite(from+delta)||!std::isfinite(radius)||radius<0||!valid_box(box))
        throw std::invalid_argument("Invalid sphere sweep parameters");
    Vec3 r{radius, radius, radius};
    box.lo = box.lo - r;
    box.hi = box.hi + r;
    if(!valid_box(box)) throw std::invalid_argument("Expanded collision box overflow");
    return slab(box, from, delta, 1, t, normal);
}
SlideResult slide_sphere_boxes(Vec3 from, Vec3 delta, float radius, const std::vector<Aabb> &boxes,
                               int iterations) {
    if (!std::isfinite(radius) || !finite(from+delta) || radius < 0 || iterations < 1 || iterations > 16 || !std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(from.z) || !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
        throw std::runtime_error("Invalid sphere slide parameters");
    for(const auto &box:boxes) if(!valid_box(box)) throw std::invalid_argument("Invalid collision box");
    SlideResult result{from, delta, {}, 0};
    constexpr float skin = 1e-4f;
    for (int step = 0; step < iterations && length(result.remaining) > 1e-7f; ++step) {
        float first = 1.f;
        Vec3 contact{};
        bool hit = false;
        for (const auto &box : boxes) {
            if (box.lo.x > box.hi.x || box.lo.y > box.hi.y || box.lo.z > box.hi.z)
                throw std::runtime_error("Invalid collision box");
            float t;
            Vec3 normal;
            if (sweep_sphere_box(result.position, result.remaining, radius, box, t, normal) && t <= first) {
                first = std::max(0.f, t);
                contact = normal;
                hit = true;
            }
        }
        if (!hit) {
            result.position = result.position + result.remaining;
            result.remaining = {};
            break;
        }
        result.position = result.position + result.remaining * first;
        ++result.contacts;
        result.normal = contact;
        if (length(contact) < .5f) {
            result.remaining = {};
            break;
        }
        result.position = result.position + contact * skin;
        Vec3 rest = result.remaining * (1 - first);
        float inward = dot(rest, contact);
        if (inward < 0)
            rest = rest - contact * inward;
        result.remaining = rest;
    }
    return result;
}
void CollisionMesh::save(const std::string &path) const {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    f.write("EMBCOL02", 8);
    put32(f, uint32_t(triangles_.size()));
    put32(f, uint32_t(leaf_size_));
    put32(f, uint32_t(nodes_.size()));
    for (auto &t : triangles_)
        for (auto v : t.v) {
            put_float(f, v.x);
            put_float(f, v.y);
            put_float(f, v.z);
        }
    for (auto &t : triangles_)
        put32(f, t.source);
    for (auto &n : nodes_) {
        for (float v :
             {n.bounds.lo.x, n.bounds.lo.y, n.bounds.lo.z, n.bounds.hi.x, n.bounds.hi.y, n.bounds.hi.z})
            put_float(f, v);
        for (uint32_t v : {n.first, n.count, n.left, n.right})
            put32(f, v);
    }
    if (!f)
        throw std::runtime_error("Failed saving collision data");
}
CollisionMesh CollisionMesh::load(const std::string &path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    char magic[8];
    f.read(magic, 8);
    bool legacy = !std::memcmp(magic, "EMBCOL01", 8), current = !std::memcmp(magic, "EMBCOL02", 8);
    if (!f || (!legacy && !current))
        throw std::runtime_error("Invalid collision header");
    CollisionMesh out;
    uint32_t count = get32(f), leaf = get32(f), nodes = get32(f);
    if (count > 2000000 || nodes > uint64_t(count) * 2 || leaf < 2 || leaf > 16 || (count > 0 && nodes == 0))
        throw std::runtime_error("Invalid collision sizes");
    auto here = f.tellg();
    f.seekg(0, std::ios::end);
    auto available = f.tellg() - here;
    uint64_t payload = uint64_t(count) * (legacy ? 36 : 40) + uint64_t(nodes) * 40;
    if (payload > 256ull * 1024 * 1024 || available != std::streamoff(payload))
        throw std::runtime_error("Collision payload size mismatch");
    f.seekg(here);
    out.leaf_size_ = int(leaf);
    out.triangles_.resize(count);
    out.nodes_.resize(nodes);
    for (auto &t : out.triangles_)
        for (auto &v : t.v)
            v = {get_float(f), get_float(f), get_float(f)};
    if (current)
        for (auto &t : out.triangles_)
            t.source = get32(f);
    else
        for (uint32_t i = 0; i < count; ++i)
            out.triangles_[i].source = i;
    for (uint32_t i = 0; i < nodes; i++) {
        auto &n = out.nodes_[i];
        n.bounds.lo = {get_float(f), get_float(f), get_float(f)};
        n.bounds.hi = {get_float(f), get_float(f), get_float(f)};
        n.first = get32(f);
        n.count = get32(f);
        n.left = get32(f);
        n.right = get32(f);
        if (n.bounds.lo.x > n.bounds.hi.x || n.bounds.lo.y > n.bounds.hi.y || n.bounds.lo.z > n.bounds.hi.z)
            throw std::runtime_error("Invalid BVH bounds");
        if (n.count) {
            if (uint64_t(n.first) + n.count > count)
                throw std::runtime_error("Invalid leaf");
        } else if (n.left <= i || n.right <= i || n.left >= nodes || n.right >= nodes)
            throw std::runtime_error("Invalid BVH topology");
    }
    // Validate depth before fixed traversal stack is used on externally supplied data.
    std::vector<int> depths(nodes, 0);
    for (uint32_t i = 0; i < nodes; i++)
        if (!out.nodes_[i].count) {
            int d = depths[i] + 1;
            if (d > 100)
                throw std::runtime_error("BVH too deep");
            depths[out.nodes_[i].left] = std::max(depths[out.nodes_[i].left], d);
            depths[out.nodes_[i].right] = std::max(depths[out.nodes_[i].right], d);
        }
    return out;
}
Mesh load_obj(const std::string &path, Vec3 color) {
    std::ifstream file(std::filesystem::u8path(path));
    if (!file)
        throw std::runtime_error("Cannot open OBJ: " + path);
    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    Mesh mesh;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream s(line);
        std::string type;
        s >> type;
        if (type == "v") {
            Vec3 p;
            if (!(s >> p.x >> p.y >> p.z) || !std::isfinite(p.x) || !std::isfinite(p.y) ||
                !std::isfinite(p.z))
                throw std::runtime_error("Invalid OBJ vertex");
            positions.push_back(p);
        } else if (type == "vt") {
            Vec2 uv;
            if (!(s >> uv.x >> uv.y) || !std::isfinite(uv.x) || !std::isfinite(uv.y))
                throw std::runtime_error("Invalid OBJ UV");
            uvs.push_back(uv);
        } else if (type == "f") {
            std::vector<Vertex> face;
            std::string ref;
            while (s >> ref) {
                if (ref[0] == '#')
                    break;
                auto slash = ref.find('/');
                int i = std::stoi(ref.substr(0, slash));
                i = i > 0 ? i - 1 : int(positions.size()) + i;
                if (i < 0 || size_t(i) >= positions.size())
                    throw std::runtime_error("OBJ position index out of bounds");
                Vertex v{positions[i], {}};
                if (slash != ref.npos) {
                    auto end = ref.find('/', slash + 1);
                    auto token = ref.substr(slash + 1, end == ref.npos ? end : end - slash - 1);
                    if (!token.empty()) {
                        int j = std::stoi(token);
                        j = j > 0 ? j - 1 : int(uvs.size()) + j;
                        if (j < 0 || size_t(j) >= uvs.size())
                            throw std::runtime_error("OBJ UV index out of bounds");
                        v.uv = uvs[j];
                    }
                }
                face.push_back(v);
            }
            if (face.size() < 3)
                throw std::runtime_error("OBJ face needs three vertices");
            for (size_t i = 1; i + 1 < face.size(); i++)
                mesh.triangles.push_back({{face[0], face[i], face[i + 1]}, color, !uvs.empty()});
        }
    }
    return mesh;
}
} // namespace ember
