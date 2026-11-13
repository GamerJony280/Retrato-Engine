#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace ember {
struct Vec2 {
    float x = 0, y = 0;
    Vec2 operator+(Vec2 b) const { return {x + b.x, y + b.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return *this * (1 / s); }
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalized(Vec3 v) {
    float l = length(v);
    return l > 1e-8f ? v / l : Vec3{};
}
inline Vec3 mul(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
inline Vec3 mix(Vec3 a, Vec3 b, float t) {
    return a * (1 - t) + b * t;
}
inline float clamp(float x, float a = 0, float b = 1) {
    return std::clamp(x, a, b);
}
inline Vec3 rotate_y(Vec3 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Vec3 point(Vec3) const;
    Vec3 vector(Vec3) const;
    Vec3 normal(Vec3) const;
};
Mat4 operator*(const Mat4 &, const Mat4 &);

struct Aabb {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void add(Vec3 p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
};
} // namespace ember
