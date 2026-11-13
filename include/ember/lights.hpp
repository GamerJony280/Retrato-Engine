#pragma once

#include "math.hpp"
#include "scene.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ember {

// Reuses transient light slots without erasing Scene::lights. One pool belongs
// to one Scene for its lifetime; static light erasure/clear must not occur while
// the pool is active. Duplicate release is harmless and cannot duplicate a slot.
class LightPool {
    Scene *scene_ = nullptr;
    std::vector<int> free_;
    std::vector<unsigned char> owned_;
    std::vector<unsigned char> available_;
    static constexpr float kParkY = -1e6f;

    void bind(Scene &scene) {
        if (scene_ && scene_ != &scene)
            throw std::logic_error("A LightPool cannot be shared by multiple scenes");
        scene_ = &scene;
        if (scene.lights.size() < owned_.size())
            throw std::logic_error("Scene lights were erased while a LightPool was active");
        if (owned_.size() < scene.lights.size()) {
            owned_.resize(scene.lights.size(), 0);
            available_.resize(scene.lights.size(), 0);
        }
    }

    bool valid(const Scene &scene, int id) const {
        return scene_ == &scene && id >= 0 && size_t(id) < scene.lights.size() &&
               size_t(id) < owned_.size() && owned_[size_t(id)] != 0;
    }

  public:
    LightPool() = default;
    LightPool(const LightPool &) = delete;
    LightPool &operator=(const LightPool &) = delete;
    LightPool(LightPool &&) = delete;
    LightPool &operator=(LightPool &&) = delete;

    int acquire(Scene &scene) {
        bind(scene);
        while (!free_.empty()) {
            const int id = free_.back();
            free_.pop_back();
            if (id < 0 || size_t(id) >= scene.lights.size() || !owned_[size_t(id)] ||
                !available_[size_t(id)])
                continue;
            available_[size_t(id)] = 0;
            return id;
        }
        scene.lights.push_back({Vec3{0, kParkY, 0}, Vec3{}, 0});
        owned_.resize(scene.lights.size(), 0);
        available_.resize(scene.lights.size(), 0);
        const int id = int(scene.lights.size()) - 1;
        owned_[size_t(id)] = 1;
        return id;
    }

    bool set(Scene &scene, int id, Vec3 position, Vec3 color, float radius) {
        if (!valid(scene, id) || available_[size_t(id)])
            return false;
        if (!finite(position) || !finite(color) || !std::isfinite(radius) || radius < 0)
            throw std::invalid_argument("Invalid light values");
        scene.lights[size_t(id)] = {position, color, radius};
        return true;
    }

    bool park(Scene &scene, int id) {
        if (!valid(scene, id))
            return false;
        scene.lights[size_t(id)] = {{0, kParkY, 0}, {}, 0};
        return true;
    }

    bool release(Scene &scene, int id) {
        if (!valid(scene, id) || available_[size_t(id)])
            return false;
        park(scene, id);
        available_[size_t(id)] = 1;
        free_.push_back(id);
        return true;
    }

    size_t parked() const { return free_.size(); }

  private:
    static bool finite(Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
};

} // namespace ember
