#pragma once
#include "physics.hpp"
#include <string>
namespace ember {
enum class Profile { Normal, PS1, N64, Pixel };
struct Config {
    std::string backend = "auto", filter = "normal";
    Profile profile = Profile::Pixel;
    int lighting_quality = 35, shadow_distance = 32;
    int resolution_quality = 35, shadow_quality = 20, toon_bands = 4, local_lights_per_object = 2,
        shadowed_lights = 1;
    bool pixel_upscale = true;
    bool ps1_depth_test = false; // false: classic painter order; true: stable depth occlusion
    int collision_quality = 70, solver_quality = 40, fixed_hz = 60, worker_threads = -1;
    int physics_max_bodies = 4096, physics_max_body_pairs = 4096, physics_max_contacts = 2048,
        physics_temporary_mb = 4;
    Vec3 physics_gravity{0, -9.81f, 0};
    std::string static_shape = "mesh_bvh", dynamic_shape = "convex_compound",
                continuous_collision = "selected", physics_backend = "none", physics_plane = "3d";
    bool streaming = true;
    int width() const;
    int height() const;
    int shadow_size() const;
    // Validates public C++ configuration with the same limits as file input.
    void validate() const;
    double fixed_delta() const;
    PhysicsWorldSettings physics_settings() const;
    static Config load(const std::string &path);
};
const char *profile_name(Profile p);
Profile parse_profile(const std::string &s);
} // namespace ember
