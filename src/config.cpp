#include <filesystem>
#include "ember/config.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace ember {
static std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
const char *profile_name(Profile p) {
    switch (p) {
    case Profile::PS1:
        return "ps1";
    case Profile::N64:
        return "n64";
    case Profile::Pixel:
        return "pixel";
    default:
        return "normal";
    }
}
Profile parse_profile(const std::string &s) {
    if (s == "ps1")
        return Profile::PS1;
    if (s == "n64")
        return Profile::N64;
    if (s == "pixel")
        return Profile::Pixel;
    if (s == "normal")
        return Profile::Normal;
    throw std::runtime_error("Unknown profile: " + s);
}
int Config::width() const {
    if (resolution_quality < 1 || resolution_quality > 100)
        throw std::runtime_error("Invalid resolution_quality (expected 1..100)");
    return 160 + ((resolution_quality - 1) * 800 / 99 / 16) * 16;
}
int Config::height() const {
    return width() * 9 / 16;
}
int Config::shadow_size() const {
    if (shadow_quality < 1 || shadow_quality > 100)
        throw std::runtime_error("Invalid shadow_quality (expected 1..100)");
    return shadow_quality < 34 ? 128 : shadow_quality < 67 ? 256 : 512;
}
double Config::fixed_delta() const {
    if (fixed_hz < 15 || fixed_hz > 240)
        throw std::runtime_error("Invalid fixed_hz (expected 15..240)");
    return 1.0 / fixed_hz;
}
PhysicsWorldSettings Config::physics_settings() const {
    fixed_delta();
    if (physics_backend != "none" && physics_backend != "jolt")
        throw std::runtime_error("Invalid physics backend (expected none or jolt)");
    PhysicsWorldSettings out;
    out.plane = parse_physics_plane(physics_plane.c_str());
    out.solver_quality = solver_quality;
    out.worker_threads = worker_threads;
    out.max_bodies = uint32_t(physics_max_bodies);
    out.max_body_pairs = uint32_t(physics_max_body_pairs);
    out.max_contacts = uint32_t(physics_max_contacts);
    out.temporary_bytes = size_t(physics_temporary_mb) * 1024 * 1024;
    out.gravity = physics_gravity;
    if (solver_quality < 1 || solver_quality > 100 || worker_threads < -1 || worker_threads > 64)
        throw std::runtime_error("Invalid direct physics configuration");
    if (physics_max_bodies < 64 || physics_max_bodies > 1000000 || physics_max_body_pairs < 64 ||
        physics_max_body_pairs > 4000000 || physics_max_contacts < 64 || physics_max_contacts > 1000000 ||
        physics_temporary_mb < 1 || physics_temporary_mb > 1024 || !std::isfinite(physics_gravity.x) ||
        !std::isfinite(physics_gravity.y) || !std::isfinite(physics_gravity.z))
        throw std::runtime_error("Invalid direct physics capacity or gravity");
    return out;
}
void Config::validate() const {
    width();
    shadow_size();
    physics_settings();
    if (backend != "auto" && backend != "cpu" && backend != "opengl" && backend != "vulkan")
        throw std::runtime_error("Invalid backend");
    if (filter != "normal" && filter != "crt")
        throw std::runtime_error("Invalid filter");
    if (profile < Profile::Normal || profile > Profile::Pixel || lighting_quality < 1 ||
        lighting_quality > 100 || shadow_distance < 4 || shadow_distance > 128 || toon_bands < 2 ||
        toon_bands > 16 || local_lights_per_object < 0 || local_lights_per_object > 4 ||
        shadowed_lights < 0 || shadowed_lights > 1 || collision_quality < 1 || collision_quality > 100)
        throw std::runtime_error("Invalid graphics or collision configuration");
    if (static_shape != "mesh_bvh" || dynamic_shape != "convex_compound")
        throw std::runtime_error("Unknown collision cooking shape");
    if (continuous_collision != "selected" && continuous_collision != "off")
        throw std::runtime_error("Invalid continuous_collision");
}
Config Config::load(const std::string &path) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f)
        throw std::runtime_error("Cannot open config: " + path);
    Config c;
    std::string section, line;
    std::set<std::string> seen;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\"')
                quoted = !quoted;
            if (line[i] == '#' && !quoted) {
                line.resize(i);
                break;
            }
        }
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section != "graphics" && section != "collision_cooking" && section != "physics" &&
                section != "runtime")
                throw std::runtime_error("Unknown section: " + section);
            continue;
        }
        auto eq = line.find('=');
        if (eq == line.npos)
            throw std::runtime_error("Expected key=value at line " + std::to_string(lineno));
        auto key = section + "." + trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        if (!seen.insert(key).second)
            throw std::runtime_error("Duplicate key: " + key);
        auto str = [&]() {
            if (value.size() < 2 || value.front() != '\"' || value.back() != '\"')
                throw std::runtime_error("Expected quoted string: " + key);
            return value.substr(1, value.size() - 2);
        };
        auto integer = [&](int lo, int hi) {
            size_t n = 0;
            int v = std::stoi(value, &n);
            if (n != value.size() || v < lo || v > hi)
                throw std::runtime_error("Invalid range: " + key);
            return v;
        };
        auto number = [&](float lo, float hi) {
            size_t n = 0;
            float v = std::stof(value, &n);
            if (n != value.size() || !std::isfinite(v) || v < lo || v > hi)
                throw std::runtime_error("Invalid range: " + key);
            return v;
        };
        auto boolean = [&]() {
            if (value != "true" && value != "false")
                throw std::runtime_error("Expected boolean: " + key);
            return value == "true";
        };
        if (key == "graphics.backend")
            c.backend = str();
        else if (key == "graphics.profile")
            c.profile = parse_profile(str());
        else if (key == "graphics.filter")
            c.filter = str();
        else if (key == "graphics.resolution_quality")
            c.resolution_quality = integer(1, 100);
        else if (key == "graphics.lighting_quality")
            c.lighting_quality = integer(1, 100);
        else if (key == "graphics.shadow_distance")
            c.shadow_distance = integer(4, 128);
        else if (key == "graphics.shadow_quality")
            c.shadow_quality = integer(1, 100);
        else if (key == "graphics.toon_bands")
            c.toon_bands = integer(2, 16);
        else if (key == "graphics.local_lights_per_object")
            c.local_lights_per_object = integer(0, 4);
        else if (key == "graphics.shadowed_lights")
            c.shadowed_lights = integer(0, 1);
        else if (key == "graphics.ps1_depth_test")
            c.ps1_depth_test = boolean();
        else if (key == "graphics.pixel_upscale")
            c.pixel_upscale = boolean();
        else if (key == "collision_cooking.quality")
            c.collision_quality = integer(1, 100);
        else if (key == "collision_cooking.static_shape")
            c.static_shape = str();
        else if (key == "collision_cooking.dynamic_shape")
            c.dynamic_shape = str();
        else if (key == "physics.solver_quality")
            c.solver_quality = integer(1, 100);
        else if (key == "physics.backend")
            c.physics_backend = str();
        else if (key == "physics.plane")
            c.physics_plane = str();
        else if (key == "physics.gravity_x")
            c.physics_gravity.x = number(-1000, 1000);
        else if (key == "physics.gravity_y")
            c.physics_gravity.y = number(-1000, 1000);
        else if (key == "physics.gravity_z")
            c.physics_gravity.z = number(-1000, 1000);
        else if (key == "physics.max_bodies")
            c.physics_max_bodies = integer(64, 1000000);
        else if (key == "physics.max_body_pairs")
            c.physics_max_body_pairs = integer(64, 4000000);
        else if (key == "physics.max_contacts")
            c.physics_max_contacts = integer(64, 1000000);
        else if (key == "physics.temporary_mb")
            c.physics_temporary_mb = integer(1, 1024);
        else if (key == "physics.fixed_hz")
            c.fixed_hz = integer(15, 240);
        else if (key == "physics.continuous_collision")
            c.continuous_collision = str();
        else if (key == "runtime.worker_threads")
            c.worker_threads = integer(-1, 64);
        else if (key == "runtime.streaming")
            c.streaming = boolean();
        else
            throw std::runtime_error("Unknown setting: " + key);
    }
    c.validate();
    return c;
}
} // namespace ember
