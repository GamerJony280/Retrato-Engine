#pragma once
#include "math.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace ember {
using SaveValue = std::variant<int64_t, double, bool, std::string, Vec2, Vec3>;

// Small, engine-independent game-state container. Games decide which values
// are authoritative; Ember provides deterministic, validated persistence.
class SaveData {
    std::unordered_map<std::string, SaveValue> values_;

  public:
    bool contains(const std::string &key) const;
    void erase(const std::string &key);
    void clear() { values_.clear(); }
    size_t size() const { return values_.size(); }

    void set(const std::string &key, int64_t value);
    void set(const std::string &key, double value);
    void set(const std::string &key, bool value);
    void set(const std::string &key, std::string value);
    void set(const std::string &key, Vec2 value);
    void set(const std::string &key, Vec3 value);

    int64_t integer(const std::string &key) const;
    double number(const std::string &key) const;
    bool boolean(const std::string &key) const;
    const std::string &string(const std::string &key) const;
    Vec2 vec2(const std::string &key) const;
    Vec3 vec3(const std::string &key) const;

    void write(const std::string &path) const;
    static SaveData read(const std::string &path);
};
} // namespace ember
