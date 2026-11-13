#pragma once
#include "ember/game.hpp"
#include "ember/save.hpp"
#include <cmath>
#include <algorithm>
#include <memory>
#include <sstream>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
namespace nova {
// Fixed-size owning collection; no resize operation can invalidate an indexed
// lvalue while its right-hand side calls another Nova function. bool uses real
// bool storage, never vector<bool> proxies. Copies are explicit value copies.
template<class T> class Array {
    std::unique_ptr<T[]> values_;
    std::int64_t count_ = 0;
  public:
    Array() = default;
    Array(std::int64_t count, const T &fill) : count_(count) {
        if (count < 0 || count > 8192) throw std::runtime_error("array size must be 0..8192");
        if (count) { values_ = std::make_unique<T[]>(size_t(count)); std::fill_n(values_.get(),count,fill); }
    }
    Array(const Array &other) : Array(other.count_, T{}) {
        if (count_) std::copy_n(other.values_.get(),count_,values_.get());
    }
    Array &operator=(const Array &other) { if(this != &other) { Array copy(other); *this = std::move(copy); } return *this; }
    Array(Array &&other) noexcept : values_(std::move(other.values_)), count_(other.count_) { other.count_=0; }
    Array &operator=(Array &&other) noexcept {
        if(this != &other) { values_=std::move(other.values_); count_=other.count_; other.count_=0; } return *this;
    }
    std::int64_t size() const { return count_; }
    T &at(std::int64_t i) { if(i<0 || i>=count_) throw std::runtime_error("array index out of bounds"); return values_[size_t(i)]; }
    const T &at(std::int64_t i) const { if(i<0 || i>=count_) throw std::runtime_error("array index out of bounds"); return values_[size_t(i)]; }
};
template<class T> Array<T> make_array(std::int64_t n, const T &value) { return Array<T>(n,value); }
template<class T> std::int64_t array_size(const Array<T> &a) { return a.size(); }
template<class T> T &at(Array<T> &a, std::int64_t n) { return a.at(n); }
template<class T> const T &at(const Array<T> &a, std::int64_t n) { return a.at(n); }
template<class T> std::string text(const T &value) { std::ostringstream s; s << value; return s.str(); }
inline std::string text(bool v) { return v ? "true" : "false"; }
inline int native_int(std::int64_t n) {
    if(n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
        throw std::runtime_error("native integer out of range");
    return static_cast<int>(n);
}

class Fault : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
template <class F> decltype(auto) located(const char *file, int line, int column, F &&fn) {
    try {
        return fn();
    } catch (const Fault &) {
        throw;
    } catch (const std::exception &e) {
        throw Fault(std::string(file) + ":" + std::to_string(line) + ":" + std::to_string(column) +
                    ": runtime error: " + e.what());
    }
}
struct EntryGuard {
    bool &flag;
    explicit EntryGuard(bool &f) : flag(f) { if(f) throw std::runtime_error("recursive Nova entry"); f=true; }
    ~EntryGuard() { flag=false; }
};
struct Budget {
    int remaining = 100000, depth = 0;
    void step() {
        if (--remaining < 0)
            throw std::runtime_error("execution budget exceeded");
    }
    void reset() { remaining = 100000; }
};
struct CallDepth {
    Budget &budget;
    explicit CallDepth(Budget &b) : budget(b) {
        b.step();
        if (b.depth >= 128)
            throw std::runtime_error("call depth exceeded");
        ++b.depth;
    }
    ~CallDepth() { --budget.depth; }
};
inline int64_t iadd(int64_t a, int64_t b) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        throw std::runtime_error("integer overflow");
    return a + b;
}
inline int64_t isub(int64_t a, int64_t b) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        throw std::runtime_error("integer overflow");
    return a - b;
}
inline int64_t imul(int64_t a, int64_t b) {
    if (a > 0   ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
        : a < 0 ? (b > 0 ? a < INT64_MIN / b : b < 0 && a < INT64_MAX / b)
                : false)
        throw std::runtime_error("integer overflow");
    return a * b;
}
inline int64_t idiv(int64_t a, int64_t b) {
    if (!b)
        throw std::runtime_error("division by zero");
    if (a == INT64_MIN && b == -1)
        throw std::runtime_error("integer overflow");
    return a / b;
}
inline int64_t imod(int64_t a, int64_t b) {
    if (!b)
        throw std::runtime_error("division by zero");
    return a == INT64_MIN && b == -1 ? 0 : a % b;
}
inline double finite(double x) {
    if (!std::isfinite(x))
        throw std::runtime_error("non-finite arithmetic");
    return x;
}
inline double absolute(double x) { return finite(std::fabs(x)); }
inline double sine(double x) { return finite(std::sin(x)); }
inline double cosine(double x) { return finite(std::cos(x)); }
inline double square_root(double x) { return finite(std::sqrt(x)); }
inline double minimum(double a, double b) { return std::min(finite(a),finite(b)); }
inline double maximum(double a, double b) { return std::max(finite(a),finite(b)); }
inline double clamped(double x, double a, double b) {
    finite(x); finite(a); finite(b);
    if (a > b)
        throw std::runtime_error("invalid clamp bounds");
    return std::clamp(x, a, b);
}
inline double magnitude(ember::Vec3 v) { return finite(std::sqrt(double(v.x)*v.x+double(v.y)*v.y+double(v.z)*v.z)); }
inline ember::Vec3 unit(ember::Vec3 v) { auto n=magnitude(v); return n>1e-8 ? ember::Vec3{float(v.x/n),float(v.y/n),float(v.z/n)} : ember::Vec3{}; }
inline double inner(ember::Vec3 a, ember::Vec3 b) { return finite(double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z); }
inline double fadd(double a, double b) {
    return finite(a + b);
}
inline double fsub(double a, double b) {
    return finite(a - b);
}
inline double fmul(double a, double b) {
    return finite(a * b);
}
inline double fdiv(double a, double b) {
    if (b == 0)
        throw std::runtime_error("division by zero");
    return finite(a / b);
}
inline float component(double x) {
    if (!std::isfinite(x) || std::abs(x) > std::numeric_limits<float>::max())
        throw std::runtime_error("vector component out of range");
    return static_cast<float>(x);
}
inline ember::Vec2 vec2(double x, double y) {
    return {component(x), component(y)};
}
inline ember::Vec3 vec3(double x, double y, double z) {
    return {component(x), component(y), component(z)};
}
inline ember::Vec2 vadd(ember::Vec2 a, ember::Vec2 b) {
    return vec2(double(a.x) + b.x, double(a.y) + b.y);
}
inline ember::Vec3 vadd(ember::Vec3 a, ember::Vec3 b) {
    return vec3(double(a.x) + b.x, double(a.y) + b.y, double(a.z) + b.z);
}
inline ember::Vec2 vsub(ember::Vec2 a, ember::Vec2 b) {
    return vec2(double(a.x) - b.x, double(a.y) - b.y);
}
inline ember::Vec3 vsub(ember::Vec3 a, ember::Vec3 b) {
    return vec3(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z);
}
inline ember::Vec2 vmul(ember::Vec2 a, double b) {
    return vec2(a.x * b, a.y * b);
}
inline ember::Vec3 vmul(ember::Vec3 a, double b) {
    return vec3(a.x * b, a.y * b, a.z * b);
}
inline ember::Vec2 vdiv(ember::Vec2 a, double b) {
    return vec2(fdiv(a.x, b), fdiv(a.y, b));
}
inline ember::Vec3 vdiv(ember::Vec3 a, double b) {
    return vec3(fdiv(a.x, b), fdiv(a.y, b), fdiv(a.z, b));
}
template <class T, class U> T store(T &target, U value) {
    if constexpr (std::is_same<T, float>::value)
        target = component(value);
    else
        target = value;
    return target;
}
template <class T> void print(const T &v) {
    std::cout << v << '\n';
}
inline void print(bool v) {
    std::cout << (v ? "true" : "false") << '\n';
}
inline void print(ember::Vec2 v) {
    std::cout << "(" << v.x << ", " << v.y << ")\n";
}
inline void print(ember::Vec3 v) {
    std::cout << "(" << v.x << ", " << v.y << ", " << v.z << ")\n";
}
} // namespace nova
