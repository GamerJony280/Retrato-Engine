#pragma once

#include "math.hpp"
#include "scene.hpp"
#include <cmath>
#include <vector>

namespace ember::mesh {
namespace detail {
inline bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
inline void push_tri(Mesh &mesh, Vertex a, Vertex b, Vertex c) {
    Triangle triangle;
    triangle.v[0] = a;
    triangle.v[1] = b;
    triangle.v[2] = c;
    triangle.color = {1, 1, 1};
    mesh.triangles.push_back(std::move(triangle));
}
inline Vertex vertex(Vec3 position, Vec3 color) {
    Vertex vertex;
    vertex.position = position;
    vertex.color = color;
    return vertex;
}
} // namespace detail

inline Mesh quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, bool double_sided = true) {
    Mesh mesh;
    if (!detail::finite(a) || !detail::finite(b) || !detail::finite(c) || !detail::finite(d) ||
        !detail::finite(color))
        return mesh;
    mesh.triangles.reserve(double_sided ? 4 : 2);
    detail::push_tri(mesh, detail::vertex(a, color), detail::vertex(b, color), detail::vertex(c, color));
    detail::push_tri(mesh, detail::vertex(a, color), detail::vertex(c, color), detail::vertex(d, color));
    if (double_sided) {
        detail::push_tri(mesh, detail::vertex(c, color), detail::vertex(b, color), detail::vertex(a, color));
        detail::push_tri(mesh, detail::vertex(d, color), detail::vertex(c, color), detail::vertex(a, color));
    }
    return mesh;
}

// Ring sector in the XY plane. half_thick separates front/back sheets; it
// intentionally does not create side walls because this helper targets flat
// effects such as slashes, rings and billboards rather than closed solids.
inline Mesh disc(float r0, float r1, float a0, float a1, int segments, Vec3 inner, Vec3 outer,
                 float half_thick = 0.15f, bool double_sided = true) {
    Mesh mesh;
    if (segments < 1 || r0 < 0 || r1 <= 0 || r1 < r0 || half_thick < 0 || !std::isfinite(r0) ||
        !std::isfinite(r1) || !std::isfinite(a0) || !std::isfinite(a1) || !std::isfinite(half_thick) ||
        std::fabs(a1 - a0) < 1e-7f || !detail::finite(inner) || !detail::finite(outer))
        return mesh;
    mesh.triangles.reserve(size_t(segments) * (double_sided ? 4 : 2));
    for (int i = 0; i < segments; ++i) {
        const float t0 = a0 + (a1 - a0) * i / segments;
        const float t1 = a0 + (a1 - a0) * (i + 1) / segments;
        for (int layer = 0; layer < (double_sided ? 2 : 1); ++layer) {
            const float z = layer == 0 ? half_thick : -half_thick;
            auto point = [&](float angle, float radius, Vec3 color) {
                return detail::vertex({std::cos(angle) * radius, std::sin(angle) * radius, z}, color);
            };
            if (layer == 0) {
                detail::push_tri(mesh, point(t0, r0, inner), point(t0, r1, outer), point(t1, r1, outer));
                detail::push_tri(mesh, point(t0, r0, inner), point(t1, r1, outer), point(t1, r0, inner));
            } else {
                detail::push_tri(mesh, point(t1, r1, outer), point(t0, r1, outer), point(t0, r0, inner));
                detail::push_tri(mesh, point(t1, r0, inner), point(t1, r1, outer), point(t0, r0, inner));
            }
        }
    }
    return mesh;
}

inline Mesh grid(int nx, int nz, float cell, Vec3 color, Vec3 origin = {}) {
    Mesh mesh;
    if (nx < 1 || nz < 1 || !std::isfinite(cell) || cell <= 0 || !detail::finite(color) ||
        !detail::finite(origin))
        return mesh;
    mesh.triangles.reserve(size_t(nx) * size_t(nz) * 2);
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            const Vec3 p0 = origin + Vec3{float(x) * cell, 0, float(z) * cell};
            detail::push_tri(mesh, detail::vertex(p0, color),
                             detail::vertex(p0 + Vec3{0, 0, cell}, color),
                             detail::vertex(p0 + Vec3{cell, 0, cell}, color));
            detail::push_tri(mesh, detail::vertex(p0, color),
                             detail::vertex(p0 + Vec3{cell, 0, cell}, color),
                             detail::vertex(p0 + Vec3{cell, 0, 0}, color));
        }
    return mesh;
}

} // namespace ember::mesh
