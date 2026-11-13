#include "ember/asset.hpp"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace ember {
namespace {
struct Stream {
    std::fstream f;
    bool writing;
    size_t remaining = 0;
    Stream(const std::string &p, bool w) : writing(w) {
        f.open(std::filesystem::u8path(p), std::ios::binary | (w ? (std::ios::out | std::ios::trunc) : std::ios::in));
        if (!f)
            throw std::runtime_error("Cannot open asset: " + p);
        if (!w) {
            f.seekg(0, std::ios::end);
            auto n = f.tellg();
            if (n < 0 || n > 256 * 1024 * 1024)
                throw std::runtime_error("Cooked asset exceeds 256 MiB");
            remaining = size_t(n);
            f.seekg(0);
        }
    }
    void u(uint32_t &v) {
        unsigned char b[4];
        if (writing) {
            for (int i = 0; i < 4; i++)
                b[i] = (v >> (i * 8)) & 255;
            f.write((char *)b, 4);
        } else {
            if (remaining < 4)
                throw std::runtime_error("Truncated cooked asset");
            remaining -= 4;
            f.read((char *)b, 4);
            v = 0;
            for (int i = 0; i < 4; i++)
                v |= uint32_t(b[i]) << (i * 8);
        }
        if (!f)
            throw std::runtime_error("Asset IO failure");
    }
    void i(int &v) {
        uint32_t x = uint32_t(v);
        u(x);
        v = int32_t(x);
    }
    void real(float &v) {
        uint32_t x;
        std::memcpy(&x, &v, 4);
        u(x);
        std::memcpy(&v, &x, 4);
        if (!std::isfinite(v))
            throw std::runtime_error("Non-finite asset value");
    }
    void boolean(bool &v) {
        uint32_t x = v;
        u(x);
        if (x > 1)
            throw std::runtime_error("Invalid bool");
        v = x != 0;
    }
    void vec(Vec3 &v) {
        real(v.x);
        real(v.y);
        real(v.z);
    }
    void mat(Mat4 &m) {
        for (auto &v : m.m)
            real(v);
    }
    void transform(Transform &t) {
        vec(t.translation);
        vec(t.scale);
        real(t.rotation.x);
        real(t.rotation.y);
        real(t.rotation.z);
        real(t.rotation.w);
    }
    size_t count(size_t n, size_t max) {
        uint32_t x = uint32_t(n);
        if (writing && n > max)
            throw std::runtime_error("Asset count too large");
        u(x);
        if (x > max || (!writing && x > remaining + 1))
            throw std::runtime_error("Asset count exceeds budget");
        return x;
    }
    void str(std::string &s) {
        size_t n = count(s.size(), 65536);
        if (writing)
            f.write(s.data(), n);
        else {
            if (n > remaining)
                throw std::runtime_error("Truncated string");
            remaining -= n;
            s.resize(n);
            f.read(s.data(), n);
        }
        if (!f)
            throw std::runtime_error("Asset string IO");
    }
    template <class T, class Fn> void list(std::vector<T> &v, size_t max, Fn fn) {
        size_t n = count(v.size(), max);
        if (!writing)
            v.resize(n);
        for (auto &x : v)
            fn(x);
    }
};
void process(Stream &s, Asset &a, const ImportOptions &o) {
    uint32_t magic = 0x32424d45, version = 2;
    s.u(magic);
    s.u(version);
    if (magic != 0x32424d45 || version != 2)
        throw std::runtime_error("Unsupported cooked asset version");
    s.list(a.nodes, o.max_nodes, [&](AssetNode &n) {
        s.str(n.name);
        s.i(n.parent);
        s.transform(n.local);
    });
    s.list(a.materials, 4096, [&](Material &m) {
        s.str(m.name);
        s.vec(m.base_color);
        s.vec(m.emissive);
        s.real(m.opacity);
        s.real(m.alpha_cutoff);
        s.i(m.image);
        s.boolean(m.unlit);
        s.boolean(m.double_sided);
        s.i(m.wrap_u);
        s.i(m.wrap_v);
    });
    size_t image_bytes = 0;
    s.list(a.images, 4096, [&](std::shared_ptr<const ImageData> &ptr) {
        if (s.writing) {
            if (!ptr)
                throw std::runtime_error("Missing image data");
            int width = ptr->width, height = ptr->height;
            s.i(width);
            s.i(height);
            if (width < 1 || height < 1 || width > o.max_texture_size || height > o.max_texture_size)
                throw std::runtime_error("Invalid image size");
            size_t pixels = size_t(width) * height;
            image_bytes += pixels * 4;
            if (image_bytes > o.max_image_bytes || ptr->rgba.size() != pixels)
                throw std::runtime_error("Invalid image data length or texture budget");
            s.count(ptr->rgba.size(), pixels);
            for (uint32_t value : ptr->rgba)
                s.u(value);
            return;
        }
        auto im = std::make_shared<ImageData>();
        s.i(im->width);
        s.i(im->height);
        if (im->width < 1 || im->height < 1 || im->width > o.max_texture_size ||
            im->height > o.max_texture_size)
            throw std::runtime_error("Invalid image size");
        size_t count = size_t(im->width) * im->height;
        image_bytes += count * 4;
        if (image_bytes > o.max_image_bytes)
            throw std::runtime_error("Texture memory budget exceeded");
        s.list(im->rgba, count, [&](uint32_t &p) { s.u(p); });
        if (im->rgba.size() != count)
            throw std::runtime_error("Invalid image data length");
        ptr = im;
    });
    size_t nv = 0, nt = 0;
    s.list(a.primitives, 16384, [&](Primitive &p) {
        s.i(p.node);
        s.i(p.material);
        s.mat(p.geometry);
        s.list(p.joints, o.max_nodes, [&](int &j) { s.i(j); });
        s.list(p.inverse_bind, o.max_nodes, [&](Mat4 &m) { s.mat(m); });
        s.list(p.vertices, o.max_vertices - nv, [&](AssetVertex &v) {
            s.vec(v.position);
            s.vec(v.normal);
            s.real(v.uv.x);
            s.real(v.uv.y);
            s.vec(v.color);
            for (auto &j : v.joints)
                s.u(j);
            for (auto &w : v.weights)
                s.real(w);
        });
        nv += p.vertices.size();
        s.list(p.indices, (o.max_triangles - nt) * 3, [&](uint32_t &i) { s.u(i); });
        nt += p.indices.size() / 3;
    });
    s.list(a.clips, 4096, [&](Clip &c) {
        s.str(c.name);
        s.real(c.duration);
        s.list(c.tracks, o.max_nodes * 3, [&](Track &t) {
            s.i(t.node);
            s.i(t.path);
            s.i(t.interpolation);
            s.list(t.keys, 1000000, [&](Key &k) {
                s.real(k.time);
                for (auto &v : k.value)
                    s.real(v);
                for (auto &v : k.in)
                    s.real(v);
                for (auto &v : k.out)
                    s.real(v);
            });
        });
    });
    s.list(a.warnings, 4096, [&](std::string &w) { s.str(w); });
    for (size_t i = 0; i < a.nodes.size(); i++)
        if (a.nodes[i].parent < -1 || a.nodes[i].parent >= int(i))
            throw std::runtime_error("Invalid node hierarchy");
    for (auto &m : a.materials)
        if (m.image < -1 || m.image >= int(a.images.size()) || m.wrap_u < 0 || m.wrap_u > 2 || m.wrap_v < 0 ||
            m.wrap_v > 2)
            throw std::runtime_error("Invalid material");
    for (auto &p : a.primitives) {
        if (p.node < 0 || p.node >= int(a.nodes.size()) || p.material < 0 ||
            p.material >= int(a.materials.size()) || p.indices.size() % 3 ||
            p.joints.size() != p.inverse_bind.size())
            throw std::runtime_error("Invalid primitive");
        for (auto j : p.joints)
            if (j < 0 || j >= int(a.nodes.size()))
                throw std::runtime_error("Invalid joint");
        for (auto i : p.indices)
            if (i >= p.vertices.size())
                throw std::runtime_error("Invalid vertex index");
        for (auto &v : p.vertices)
            for (int k = 0; k < 4; k++)
                if (v.weights[k] < 0 || (v.weights[k] > 0 && v.joints[k] >= p.joints.size()))
                    throw std::runtime_error("Invalid skin weights");
    }
    for (auto &c : a.clips) {
        if (c.duration < 0)
            throw std::runtime_error("Invalid duration");
        for (auto &t : c.tracks) {
            if (t.node < 0 || t.node >= int(a.nodes.size()) || t.path < 0 || t.path > 2 ||
                t.interpolation < 0 || t.interpolation > 2)
                throw std::runtime_error("Invalid animation track");
            float prev = -1;
            for (auto &k : t.keys) {
                if (k.time < 0 || k.time < prev)
                    throw std::runtime_error("Unordered animation keys");
                prev = k.time;
            }
        }
    }
}
} // namespace
void save_asset(const Asset &a, const std::string &p) {
    Asset copy = a;
    ImportOptions o;
    o.max_vertices = 1000000;
    o.max_triangles = 1000000;
    o.max_nodes = 16384;
    o.max_texture_size = 8192;
    o.max_image_bytes = 256 * 1024 * 1024;
    static std::atomic<uint64_t> serial{1};
    auto target = std::filesystem::u8path(p);
    auto temporary = target;temporary += ".tmp-" + std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
    try {
        {
            Stream s(temporary.u8string(), true);
            process(s, copy, o);
        }
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (error) {
            // Windows does not replace an existing target with rename(). Preserve a backup until commit.
            auto backup = target;backup += ".bak-" + std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
            bool had_target = std::filesystem::exists(target);
            if (had_target)
                std::filesystem::rename(target, backup);
            try {
                std::filesystem::rename(temporary, target);
                if (had_target)
                    std::filesystem::remove(backup);
            } catch (...) {
                if (had_target && !std::filesystem::exists(target))
                    std::filesystem::rename(backup, target);
                throw;
            }
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
std::shared_ptr<const Asset> load_asset(const std::string &p, const ImportOptions &o) {
    Stream s(p, false);
    auto a = std::make_shared<Asset>();
    process(s, *a, o);
    if (s.remaining)
        throw std::runtime_error("Trailing bytes in cooked asset");
    return a;
}
#ifndef EMBER_IMPORTERS
std::shared_ptr<const Asset> import_asset(const std::string &p, const ImportOptions &o) {
    return load_asset(p, o);
}
#endif
} // namespace ember
