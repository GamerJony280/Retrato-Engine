#include "ember/save.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ember {
namespace {
constexpr char magic[8] = {'E', 'M', 'B', 'S', 'A', 'V', 'E', 0};
constexpr uint32_t version = 1, max_entries = 65536, max_string = 1024 * 1024;

void key_ok(const std::string &key) {
    if (key.empty() || key.size() > 255 || key.find('\0') != std::string::npos)
        throw std::runtime_error("Save key must contain 1..255 non-NUL bytes");
}
void finite(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Save value must be finite");
}
template <class T> void raw(std::ostream &out, T value) {
    static_assert(std::is_integral<T>::value, "integer required");
    using U = typename std::make_unsigned<T>::type;
    U bits = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        out.put(char((bits >> (i * 8)) & 255));
    if (!out)
        throw std::runtime_error("Cannot write save data");
}
void floating(std::ostream &out, double value) {
    finite(value);
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value), "double layout");
    std::memcpy(&bits, &value, sizeof(bits));
    raw(out, bits);
}
template <class T> T raw(std::istream &in) {
    static_assert(std::is_integral<T>::value, "integer required");
    using U = typename std::make_unsigned<T>::type;
    U value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        int byte = in.get();
        if (byte == EOF)
            throw std::runtime_error("Truncated save file");
        value |= U(uint8_t(byte)) << (i * 8);
    }
    return static_cast<T>(value);
}
double floating(std::istream &in) {
    uint64_t bits = raw<uint64_t>(in);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    finite(value);
    return value;
}
std::string bytes(std::istream &in, size_t count) {
    std::string value(count, '\0');
    if (count && !in.read(&value[0], std::streamsize(count)))
        throw std::runtime_error("Truncated save file");
    return value;
}
template <class T> const T &get(const std::unordered_map<std::string, SaveValue> &values, const std::string &key) {
    auto it = values.find(key);
    if (it == values.end())
        throw std::runtime_error("Missing save key: " + key);
    auto *value = std::get_if<T>(&it->second);
    if (!value)
        throw std::runtime_error("Wrong type for save key: " + key);
    return *value;
}
} // namespace

bool SaveData::contains(const std::string &key) const { return values_.count(key) != 0; }
void SaveData::erase(const std::string &key) { values_.erase(key); }
void SaveData::set(const std::string &key, int64_t value) { key_ok(key); values_[key] = value; }
void SaveData::set(const std::string &key, double value) { key_ok(key); finite(value); values_[key] = value; }
void SaveData::set(const std::string &key, bool value) { key_ok(key); values_[key] = value; }
void SaveData::set(const std::string &key, std::string value) {
    key_ok(key);
    if (value.size() > max_string)
        throw std::runtime_error("Save string is too large");
    values_[key] = std::move(value);
}
void SaveData::set(const std::string &key, Vec2 value) {
    finite(value.x); finite(value.y); key_ok(key); values_[key] = value;
}
void SaveData::set(const std::string &key, Vec3 value) {
    finite(value.x); finite(value.y); finite(value.z); key_ok(key); values_[key] = value;
}
int64_t SaveData::integer(const std::string &key) const { return get<int64_t>(values_, key); }
double SaveData::number(const std::string &key) const { return get<double>(values_, key); }
bool SaveData::boolean(const std::string &key) const { return get<bool>(values_, key); }
const std::string &SaveData::string(const std::string &key) const { return get<std::string>(values_, key); }
Vec2 SaveData::vec2(const std::string &key) const { return get<Vec2>(values_, key); }
Vec3 SaveData::vec3(const std::string &key) const { return get<Vec3>(values_, key); }

void SaveData::write(const std::string &path) const {
    if (values_.size() > max_entries)
        throw std::runtime_error("Too many save entries");
    const std::filesystem::path target(std::filesystem::u8path(path)), temporary(std::filesystem::u8path(path + ".tmp")), backup(std::filesystem::u8path(path + ".bak"));
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Cannot create save file: " + temporary.string());
        out.write(magic, sizeof(magic));
        raw(out, version);
        raw(out, uint32_t(values_.size()));
        std::vector<std::string> keys;
        keys.reserve(values_.size());
        for (const auto &entry : values_)
            keys.push_back(entry.first);
        std::sort(keys.begin(), keys.end());
        for (const auto &key : keys) {
            key_ok(key);
            raw(out, uint8_t(key.size()));
            out.write(key.data(), std::streamsize(key.size()));
            const auto &value = values_.at(key);
            raw(out, uint8_t(value.index() + 1));
            if (auto p = std::get_if<int64_t>(&value)) raw(out, *p);
            else if (auto p = std::get_if<double>(&value)) floating(out, *p);
            else if (auto p = std::get_if<bool>(&value)) raw(out, uint8_t(*p));
            else if (auto p = std::get_if<std::string>(&value)) {
                if (p->size() > max_string) throw std::runtime_error("Save string is too large");
                raw(out, uint32_t(p->size())); out.write(p->data(), std::streamsize(p->size()));
            } else if (auto p = std::get_if<Vec2>(&value)) {
                floating(out, p->x); floating(out, p->y);
            } else if (auto p = std::get_if<Vec3>(&value)) {
                floating(out, p->x); floating(out, p->y); floating(out, p->z);
            }
        }
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot finish save file");
        out.close();
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
        if (std::filesystem::exists(target))
            std::filesystem::rename(target, backup);
        try {
            std::filesystem::rename(temporary, target);
        } catch (...) {
            if (std::filesystem::exists(backup))
                std::filesystem::rename(backup, target);
            throw;
        }
        std::filesystem::remove(backup, ignored);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

SaveData SaveData::read(const std::string &path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open save file: " + path);
    if (bytes(in, sizeof(magic)) != std::string(magic, sizeof(magic)))
        throw std::runtime_error("Invalid save magic");
    if (raw<uint32_t>(in) != version)
        throw std::runtime_error("Unsupported save version");
    uint32_t count = raw<uint32_t>(in);
    if (count > max_entries)
        throw std::runtime_error("Save entry limit exceeded");
    SaveData result;
    result.values_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto key = bytes(in, raw<uint8_t>(in));
        key_ok(key);
        if (result.contains(key))
            throw std::runtime_error("Duplicate save key: " + key);
        switch (raw<uint8_t>(in)) {
        case 1: result.set(key, raw<int64_t>(in)); break;
        case 2: result.set(key, floating(in)); break;
        case 3: {
            auto value = raw<uint8_t>(in);
            if (value > 1) throw std::runtime_error("Invalid save boolean");
            result.set(key, value != 0); break;
        }
        case 4: {
            auto length = raw<uint32_t>(in);
            if (length > max_string) throw std::runtime_error("Save string limit exceeded");
            result.set(key, bytes(in, length)); break;
        }
        case 5: result.set(key, Vec2{float(floating(in)), float(floating(in))}); break;
        case 6: result.set(key, Vec3{float(floating(in)), float(floating(in)), float(floating(in))}); break;
        default: throw std::runtime_error("Unknown save value type");
        }
    }
    if (in.get() != EOF)
        throw std::runtime_error("Trailing data in save file");
    return result;
}
} // namespace ember
