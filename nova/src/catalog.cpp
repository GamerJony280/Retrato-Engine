#include "catalog.hpp"

namespace nova {
namespace {
struct TypeSpec {
    Type type;
    const char *nova_name;
    const char *cpp_name;
    bool source_visible;
};
constexpr TypeSpec types[] = {
    {Type::Unknown, "unknown", "INVALID", false},
    {Type::Void, "void", "void", false},
    {Type::Int, "int", "std::int64_t", true},
    {Type::Float, "float", "double", true},
    {Type::Bool, "bool", "bool", true},
    {Type::String, "string", "std::string", true},
    {Type::Vec2, "Vec2", "ember::Vec2", true},
    {Type::Vec3, "Vec3", "ember::Vec3", true},
    {Type::Entity, "Entity", "ember::game::Entity", true},
    {Type::Asset, "Asset", "ember::game::AssetHandle", true},
    {Type::Transform, "Transform", "ember::game::Transform", false},
    {Type::Slide, "Slide", "ember::game::Slide", true},
    {Type::Sweep, "Sweep", "ember::game::Sweep", true},
    {Type::IntArray, "int[]", "nova::Array<std::int64_t>", false},
    {Type::FloatArray, "float[]", "nova::Array<double>", false},
    {Type::BoolArray, "bool[]", "nova::Array<bool>", false},
    {Type::StringArray, "string[]", "nova::Array<std::string>", false},
    {Type::Vec2Array, "Vec2[]", "nova::Array<ember::Vec2>", false},
    {Type::Vec3Array, "Vec3[]", "nova::Array<ember::Vec3>", false},
    {Type::EntityArray, "Entity[]", "nova::Array<ember::game::Entity>", false},
    {Type::AssetArray, "Asset[]", "nova::Array<ember::game::AssetHandle>", false},
};
static_assert(sizeof(types) / sizeof(types[0]) == static_cast<size_t>(Type::AssetArray) + 1,
              "Type catalog must cover every Type value");
} // namespace

std::string type_name(Type type) {
    auto index = static_cast<size_t>(type);
    return index < sizeof(types) / sizeof(types[0]) ? types[index].nova_name : "unknown";
}

namespace detail {
constexpr BuiltinSpec builtins[] = {
    {"create_box", Type::Entity, {Type::String, Type::Vec3, Type::Vec3}, 3, "host_.create_box"},
    {"set_box", Type::Void, {Type::Entity, Type::Vec3, Type::Vec3}, 3, "host_.set_box"},
    {"set_visible", Type::Void, {Type::Entity, Type::Bool}, 2, "host_.set_visible"},
    {"camera", Type::Void, {Type::Vec3, Type::Vec3}, 2, "host_.set_camera"},
    {"light", Type::Void, {Type::Int, Type::Vec3, Type::Vec3, Type::Float}, 4, "light_"},
    {"collider_box", Type::Void, {Type::Entity, Type::Vec3, Type::Int}, 3, "collider_box_"},
    {"collider_remove", Type::Void, {Type::Entity}, 1, "host_.remove_box_collider"},
    {"sweep", Type::Sweep, {Type::Vec3, Type::Vec3, Type::Float, Type::Int}, 4, "sweep_"},
    {"slide", Type::Slide, {Type::Vec3, Type::Vec3, Type::Float, Type::Int}, 4, "slide_"},
    {"unload_asset", Type::Void, {Type::Asset}, 1, "host_.unload_asset"},
    {"play_animation", Type::Void, {Type::Entity, Type::String, Type::Bool}, 3, "host_.play_animation"},
    {"key_pressed", Type::Bool, {Type::Int}, 1, "key_pressed_"},
    {"key_released", Type::Bool, {Type::Int}, 1, "key_released_"},
    {"abs", Type::Float, {Type::Float}, 1, "nova::absolute"},
    {"sin", Type::Float, {Type::Float}, 1, "nova::sine"},
    {"cos", Type::Float, {Type::Float}, 1, "nova::cosine"},
    {"sqrt", Type::Float, {Type::Float}, 1, "nova::square_root"},
    {"min", Type::Float, {Type::Float, Type::Float}, 2, "nova::minimum"},
    {"max", Type::Float, {Type::Float, Type::Float}, 2, "nova::maximum"},
    {"clamp", Type::Float, {Type::Float, Type::Float, Type::Float}, 3, "nova::clamped"},
    {"length", Type::Float, {Type::Vec3}, 1, "nova::magnitude"},
    {"normalize", Type::Vec3, {Type::Vec3}, 1, "nova::unit"},
    {"dot", Type::Float, {Type::Vec3, Type::Vec3}, 2, "nova::inner"},
    {"len", Type::Int, {}, 1, "nova::array_size", BuiltinKind::Length},
    {"str", Type::String, {}, 1, "nova::text", BuiltinKind::Text},
    {"array_int", Type::IntArray, {Type::Int, Type::Int}, 2, "nova::make_array<std::int64_t>"},
    {"array_float", Type::FloatArray, {Type::Int, Type::Float}, 2, "nova::make_array<double>"},
    {"array_bool", Type::BoolArray, {Type::Int, Type::Bool}, 2, "nova::make_array<bool>"},
    {"array_string", Type::StringArray, {Type::Int, Type::String}, 2, "nova::make_array<std::string>"},
    {"array_vec2", Type::Vec2Array, {Type::Int, Type::Vec2}, 2, "nova::make_array<ember::Vec2>"},
    {"array_vec3", Type::Vec3Array, {Type::Int, Type::Vec3}, 2, "nova::make_array<ember::Vec3>"},
    {"array_entity", Type::EntityArray, {Type::Int, Type::Entity}, 2, "nova::make_array<ember::game::Entity>"},
    {"array_asset", Type::AssetArray, {Type::Int, Type::Asset}, 2, "nova::make_array<ember::game::AssetHandle>"},

    {"print", Type::Void, {}, 1, "nova::print", BuiltinKind::Print},
    {"Vec2", Type::Vec2, {Type::Float, Type::Float}, 2, "nova::vec2"},
    {"Vec3", Type::Vec3, {Type::Float, Type::Float, Type::Float}, 3, "nova::vec3"},
    {"create", Type::Entity, {Type::String}, 1, "host_.create"},
    {"entity", Type::Entity, {Type::String}, 1, "host_.entity"},
    {"load_asset", Type::Asset, {Type::String}, 1, "host_.load_asset"},
    {"spawn", Type::Entity, {Type::Asset, Type::String}, 2, "host_.spawn"},
    {"alive", Type::Bool, {Type::Entity}, 1, "host_.alive"},
    {"destroy", Type::Void, {Type::Entity}, 1, "destroy_"},
    {"position", Type::Vec3, {Type::Entity}, 1, "get_position_"},
    {"set_position", Type::Void, {Type::Entity, Type::Vec3}, 2, "set_position_"},
    {"physics_box", Type::Void, {Type::Entity, Type::Vec3, Type::Bool}, 3, "physics_box_"},
    {"physics_sphere", Type::Void, {Type::Entity, Type::Float, Type::Bool}, 3, "physics_sphere_"},
    {"physics_has", Type::Bool, {Type::Entity}, 1, "host_.has_rigid_body"},
    {"physics_remove", Type::Void, {Type::Entity}, 1, "host_.remove_rigid_body"},
    {"physics_velocity", Type::Vec3, {Type::Entity}, 1, "host_.velocity"},
    {"physics_set_velocity", Type::Void, {Type::Entity, Type::Vec3}, 2, "host_.set_velocity"},
    {"physics_force", Type::Void, {Type::Entity, Type::Vec3}, 2, "host_.add_force"},
    {"physics_impulse", Type::Void, {Type::Entity, Type::Vec3}, 2, "host_.add_impulse"},
    {"key_down", Type::Bool, {Type::Int}, 1, "key_down_"},
    {"mouse_position", Type::Vec2, {}, 0, "host_.mouse_position"},
    {"mouse_delta", Type::Vec2, {}, 0, "host_.mouse_delta"},
    {"mouse_down", Type::Bool, {Type::Int}, 1, "mouse_down_"},
    {"gamepad_down", Type::Bool, {Type::Int, Type::Int}, 2, "gamepad_down_"},
    {"gamepad_axis", Type::Float, {Type::Int, Type::Int}, 2, "gamepad_axis_"},
    {"save_has", Type::Bool, {Type::String}, 1, "save_.contains"},
    {"save_set_int", Type::Void, {Type::String, Type::Int}, 2, "save_.set"},
    {"save_get_int", Type::Int, {Type::String}, 1, "save_.integer"},
    {"save_set_float", Type::Void, {Type::String, Type::Float}, 2, "save_.set"},
    {"save_get_float", Type::Float, {Type::String}, 1, "save_.number"},
    {"save_set_bool", Type::Void, {Type::String, Type::Bool}, 2, "save_.set"},
    {"save_get_bool", Type::Bool, {Type::String}, 1, "save_.boolean"},
    {"save_set_string", Type::Void, {Type::String, Type::String}, 2, "save_.set"},
    {"save_get_string", Type::String, {Type::String}, 1, "save_.string"},
    {"save_set_vec2", Type::Void, {Type::String, Type::Vec2}, 2, "save_.set"},
    {"save_get_vec2", Type::Vec2, {Type::String}, 1, "save_.vec2"},
    {"save_set_vec3", Type::Void, {Type::String, Type::Vec3}, 2, "save_.set"},
    {"save_get_vec3", Type::Vec3, {Type::String}, 1, "save_.vec3"},
    {"save_write", Type::Void, {Type::String}, 1, "save_.write"},
    {"save_read", Type::Void, {Type::String}, 1, "load_save_"},
    {"time", Type::Float, {}, 0, "host_.time"},
    {"delta", Type::Float, {}, 0, "host_.delta"},
    {"emit", Type::Void, {Type::Int}, 1, "host_.emit"},
};
constexpr LifecycleSpec lifecycles[] = {
    {"start", Type::Void, 0}, {"update", Type::Float, 1}, {"on_signal", Type::Int, 1}};

const BuiltinSpec *builtin(const std::string &name) {
    for (const auto &entry : builtins)
        if (name == entry.name)
            return &entry;
    return nullptr;
}
const LifecycleSpec *lifecycle(const std::string &name) {
    for (const auto &entry : lifecycles)
        if (name == entry.name)
            return &entry;
    return nullptr;
}
Type source_type(const std::string &name) {
    for (const auto &entry : types)
        if (entry.source_visible && name == entry.nova_name)
            return entry.type;
    return Type::Unknown;
}
Type array_type(Type t) {
    switch (t) {
    case Type::Int: return Type::IntArray;
    case Type::Float: return Type::FloatArray;
    case Type::Bool: return Type::BoolArray;
    case Type::String: return Type::StringArray;
    case Type::Vec2: return Type::Vec2Array;
    case Type::Vec3: return Type::Vec3Array;
    case Type::Entity: return Type::EntityArray;
    case Type::Asset: return Type::AssetArray;
    default: return Type::Unknown;
    }
}
Type array_element(Type t) {
    switch (t) {
    case Type::IntArray: return Type::Int;
    case Type::FloatArray: return Type::Float;
    case Type::BoolArray: return Type::Bool;
    case Type::StringArray: return Type::String;
    case Type::Vec2Array: return Type::Vec2;
    case Type::Vec3Array: return Type::Vec3;
    case Type::EntityArray: return Type::Entity;
    case Type::AssetArray: return Type::Asset;
    default: return Type::Unknown;
    }
}
const char *cpp_type(Type type) {
    auto index = static_cast<size_t>(type);
    return index < sizeof(types) / sizeof(types[0]) ? types[index].cpp_name : "INVALID";
}
} // namespace detail
} // namespace nova
