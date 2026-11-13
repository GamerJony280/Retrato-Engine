#include "ember/asset.hpp"
#include "ember/runtime.hpp"
#include "ember/save.hpp"
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef EMBER_LUA
#include <lua.hpp>
namespace ember {
struct Script::Impl {
    lua_State *state = nullptr;
    Runtime *runtime;
    std::weak_ptr<void> lifetime;
    std::unordered_map<lua_Integer, ObjectRef> objects;
    std::unordered_map<lua_Integer, ColliderRef> collider_handles;
    SaveData save_data;
    lua_Integer next_object = 1, next_collider = 1;
    static Impl &self(lua_State *l) {
        auto *p = static_cast<Impl *>(lua_touserdata(l, lua_upvalueindex(1)));
        if (!p)
            throw std::runtime_error("Missing Lua engine context");
        return *p;
    }
    static Runtime &get(lua_State *l) {
        auto &p = self(l);
        if (p.lifetime.expired())
            throw std::runtime_error("Ember Runtime no longer exists");
        return *p.runtime;
    }
    static ObjectRef object_ref(lua_State *l, int argument = 1) {
        auto id = luaL_checkinteger(l, argument);
        auto &p = self(l);
        auto it = p.objects.find(id);
        if (it == p.objects.end() || !get(l).object_alive(it->second))
            throw std::runtime_error("invalid or expired object handle");
        return it->second;
    }
    static lua_Integer publish_object(lua_State *l, ObjectRef ref) {
        auto &p = self(l);
        if (p.next_object == std::numeric_limits<lua_Integer>::max())
            throw std::runtime_error("Lua object handle space exhausted");
        auto id = p.next_object++;
        p.objects.emplace(id, ref);
        return id;
    }
    static ColliderRef collider_ref(lua_State *l, int argument = 1) {
        auto id = luaL_checkinteger(l, argument);
        auto &p = self(l);
        auto it = p.collider_handles.find(id);
        if (it == p.collider_handles.end())
            throw std::runtime_error("invalid or expired collider handle");
        return it->second;
    }
    static float number(lua_State *l, int i) {
        float v = float(luaL_checknumber(l, i));
        if (!std::isfinite(v))
            luaL_error(l, "number must be finite");
        return v;
    }
    static float optional_number(lua_State *l, int i, float fallback) {
        float v = float(luaL_optnumber(l, i, fallback));
        if (!std::isfinite(v))
            luaL_error(l, "number must be finite");
        return v;
    }
    static int box_fn(lua_State *l) {
        auto &r = get(l);
        Object o;
        o.position = {number(l, 1), number(l, 2), number(l, 3)};
        Vec3 size{number(l, 4), number(l, 5), number(l, 6)}, color{number(l, 7), number(l, 8), number(l, 9)};
        o.mesh = box(size, color);
        auto ref = r.add_object(std::move(o));
        try {
            lua_pushinteger(l, publish_object(l, ref));
        } catch (...) {
            r.destroy_object(ref);
            throw;
        }
        return 1;
    }
    static Object &object(lua_State *l) {
        return get(l).object(object_ref(l));
    }
    static int position(lua_State *l) {
        float x = number(l, 2), y = number(l, 3), z = number(l, 4);
        auto ref = object_ref(l);
        auto yaw = get(l).object(ref).yaw;
        get(l).set_object_transform(ref, {x, y, z}, yaw);
        return 0;
    }
    static int rotation(lua_State *l) {
        float a = number(l, 2);
        auto ref = object_ref(l);
        auto position = get(l).object(ref).position;
        get(l).set_object_transform(ref, position, a);
        return 0;
    }
    static int camera(lua_State *l) {
        auto &r = get(l);
        r.camera.position = {number(l, 1), number(l, 2), number(l, 3)};
        r.camera.target = {number(l, 4), number(l, 5), number(l, 6)};
        return 0;
    }
    static int light(lua_State *l) {
        auto &r = get(l);
        Light light;
        light.position = {number(l, 1), number(l, 2), number(l, 3)};
        light.color = {number(l, 4), number(l, 5), number(l, 6)};
        light.radius = number(l, 7);
        if (light.radius <= 0)
            return luaL_error(l, "radius must be positive");
        r.scene.lights.push_back(light);
        return 0;
    }
    static int clear(lua_State *l) {
        auto &p = self(l);
        get(l).clear_scene();
        p.objects.clear();
        p.collider_handles.clear();
        return 0;
    }
    static int mesh_fn(lua_State *l) {
        const char *path = luaL_checkstring(l, 1);
        Vec3 pos{number(l, 2), number(l, 3), number(l, 4)};
        auto &r = get(l);
        auto index = r.add_model(path, pos);
        auto ref = r.object_ref(index);
        try {
            lua_pushinteger(l, publish_object(l, ref));
        } catch (...) {
            r.destroy_object(ref);
            throw;
        }
        return 1;
    }
    static int destroy(lua_State *l) {
        auto id = luaL_checkinteger(l, 1);
        auto &p = self(l);
        auto it = p.objects.find(id);
        if (it == p.objects.end() || !get(l).object_alive(it->second))
            throw std::runtime_error("invalid or expired object handle");
        get(l).destroy_object(it->second);
        p.objects.erase(it);
        return 0;
    }
    static int play(lua_State *l) {
        auto &o = object(l);
        const char *name = luaL_checkstring(l, 2);
        float fade = float(luaL_optnumber(l, 3, 0));
        bool loop = lua_isnoneornil(l, 4) || lua_toboolean(l, 4);
        if (!o.animator)
            throw std::runtime_error("Object has no animation asset");
        o.animator->play(name, loop, fade);
        o.animator->mesh(o.mesh);
        return 0;
    }
    static int seek(lua_State *l) {
        auto &o = object(l);
        float t = number(l, 2);
        if (!o.animator)
            throw std::runtime_error("Object has no animation asset");
        o.animator->seek(t);
        o.animator->mesh(o.mesh);
        return 0;
    }
    static int pause_animation(lua_State *l) {
        auto &o = object(l);
        if (!o.animator)
            throw std::runtime_error("Object has no animation asset");
        o.animator->pause(lua_toboolean(l, 2));
        return 0;
    }
    static int clips(lua_State *l) {
        auto &o = object(l);
        lua_newtable(l);
        if (o.animator) {
            int i = 1;
            for (auto &c : o.animator->asset().clips) {
                lua_pushlstring(l, c.name.data(), c.name.size());
                lua_rawseti(l, -2, i++);
            }
        }
        return 1;
    }
    static int bone(lua_State *l) {
        auto &o = object(l);
        const char *name = luaL_checkstring(l, 2);
        if (!o.animator)
            throw std::runtime_error("Object has no skeleton");
        auto m = o.animator->node_transform(name);
        auto p = o.parent_transform.point(rotate_y(m.point({}), o.yaw) + o.position);
        lua_pushnumber(l, p.x);
        lua_pushnumber(l, p.y);
        lua_pushnumber(l, p.z);
        return 3;
    }
    static int attach(lua_State *l) {
        auto child = object_ref(l, 1), parent = object_ref(l, 2);
        const char *bone = luaL_checkstring(l, 3);
        get(l).attach(child, parent, bone);
        return 0;
    }
    static int collider(lua_State *l) {
        auto &r = get(l);
        auto &p = self(l);
        if (p.next_collider == std::numeric_limits<lua_Integer>::max())
            throw std::runtime_error("Lua collider handle space exhausted");
        auto collider = r.add_collider(object_ref(l));
        auto handle = p.next_collider++;
        try {
            p.collider_handles.emplace(handle, collider);
        } catch (...) {
            r.remove_collider(collider);
            throw;
        }
        lua_pushinteger(l, handle);
        return 1;
    }
    static int raycast(lua_State *l) {
        auto &r = get(l);
        auto collider = collider_ref(l);
        Vec3 from{number(l, 2), number(l, 3), number(l, 4)}, dir{number(l, 5), number(l, 6), number(l, 7)};
        float distance = number(l, 8);
        Hit hit;
        if (!r.raycast(collider, from, dir, distance, hit)) {
            lua_pushnil(l);
            return 1;
        }
        for (float value : {hit.distance, hit.position.x, hit.position.y, hit.position.z, hit.normal.x,
                            hit.normal.y, hit.normal.z})
            lua_pushnumber(l, value);
        return 7;
    }
    static BodyMotion body_motion(lua_State *l, int argument) {
        const std::string value = luaL_checkstring(l, argument);
        if (value == "static") return BodyMotion::Static;
        if (value == "kinematic") return BodyMotion::Kinematic;
        if (value == "dynamic") return BodyMotion::Dynamic;
        throw std::runtime_error("body motion must be static, kinematic or dynamic");
    }
    static BodyDesc body_desc(lua_State *l, int motion_argument) {
        BodyDesc desc;
        desc.motion = body_motion(l, motion_argument);
        desc.mass = optional_number(l, motion_argument + 1, 1.f);
        desc.friction = optional_number(l, motion_argument + 2, .5f);
        desc.restitution = optional_number(l, motion_argument + 3, 0.f);
        desc.continuous = lua_toboolean(l, motion_argument + 4) != 0;
        desc.allow_rotation = lua_isnoneornil(l, motion_argument + 5) || lua_toboolean(l, motion_argument + 5);
        return desc;
    }
    static int physics_box(lua_State *l) {
        auto ref = object_ref(l);
        auto desc = body_desc(l, 5);
        desc.shape = BodyShape::box({number(l, 2), number(l, 3), number(l, 4)});
        get(l).add_rigid_body(ref, desc);
        return 0;
    }
    static int physics_sphere(lua_State *l) {
        auto ref = object_ref(l);
        auto desc = body_desc(l, 3);
        desc.shape = BodyShape::sphere(number(l, 2));
        get(l).add_rigid_body(ref, desc);
        return 0;
    }
    static int physics_capsule(lua_State *l) {
        auto ref = object_ref(l);
        auto desc = body_desc(l, 4);
        desc.shape = BodyShape::capsule(number(l, 2), number(l, 3));
        get(l).add_rigid_body(ref, desc);
        return 0;
    }
    static int physics_has(lua_State *l) {
        lua_pushboolean(l, get(l).has_rigid_body(object_ref(l)));
        return 1;
    }
    static int physics_remove(lua_State *l) {
        get(l).remove_rigid_body(object_ref(l));
        return 0;
    }
    static int physics_set_velocity(lua_State *l) {
        get(l).set_rigid_body_velocity(object_ref(l), {number(l, 2), number(l, 3), number(l, 4)});
        return 0;
    }
    static int physics_velocity(lua_State *l) {
        auto value = get(l).rigid_body_velocity(object_ref(l));
        lua_pushnumber(l, value.x); lua_pushnumber(l, value.y); lua_pushnumber(l, value.z);
        return 3;
    }
    static int physics_force(lua_State *l) {
        get(l).add_rigid_body_force(object_ref(l), {number(l, 2), number(l, 3), number(l, 4)});
        return 0;
    }
    static int physics_impulse(lua_State *l) {
        get(l).add_rigid_body_impulse(object_ref(l), {number(l, 2), number(l, 3), number(l, 4)});
        return 0;
    }
    static std::string save_key(lua_State *l, int argument = 1) {
        size_t size = 0;
        const char *value = luaL_checklstring(l, argument, &size);
        return std::string(value, size);
    }
    static int save_has(lua_State *l) {
        lua_pushboolean(l, self(l).save_data.contains(save_key(l)));
        return 1;
    }
    static int save_set_int(lua_State *l) {
        self(l).save_data.set(save_key(l), int64_t(luaL_checkinteger(l, 2)));
        return 0;
    }
    static int save_get_int(lua_State *l) {
        lua_pushinteger(l, lua_Integer(self(l).save_data.integer(save_key(l))));
        return 1;
    }
    static int save_set_float(lua_State *l) {
        double value = luaL_checknumber(l, 2);
        self(l).save_data.set(save_key(l), value);
        return 0;
    }
    static int save_get_float(lua_State *l) {
        lua_pushnumber(l, self(l).save_data.number(save_key(l)));
        return 1;
    }
    static int save_set_bool(lua_State *l) {
        luaL_checktype(l, 2, LUA_TBOOLEAN);
        self(l).save_data.set(save_key(l), lua_toboolean(l, 2) != 0);
        return 0;
    }
    static int save_get_bool(lua_State *l) {
        lua_pushboolean(l, self(l).save_data.boolean(save_key(l)));
        return 1;
    }
    static int save_set_string(lua_State *l) {
        size_t size = 0;
        const char *value = luaL_checklstring(l, 2, &size);
        self(l).save_data.set(save_key(l), std::string(value, size));
        return 0;
    }
    static int save_get_string(lua_State *l) {
        const auto &value = self(l).save_data.string(save_key(l));
        lua_pushlstring(l, value.data(), value.size());
        return 1;
    }
    static int save_set_vec3(lua_State *l) {
        self(l).save_data.set(save_key(l), Vec3{number(l, 2), number(l, 3), number(l, 4)});
        return 0;
    }
    static int save_get_vec3(lua_State *l) {
        auto value = self(l).save_data.vec3(save_key(l));
        lua_pushnumber(l, value.x); lua_pushnumber(l, value.y); lua_pushnumber(l, value.z);
        return 3;
    }
    static int save_write(lua_State *l) {
        self(l).save_data.write(luaL_checkstring(l, 1));
        return 0;
    }
    static int save_read(lua_State *l) {
        auto loaded = SaveData::read(luaL_checkstring(l, 1));
        self(l).save_data = std::move(loaded);
        return 0;
    }
    static int save_clear(lua_State *l) {
        self(l).save_data.clear();
        return 0;
    }
    template <lua_CFunction Fn> static int guarded(lua_State *l) {
        try {
            return Fn(l);
        } catch (const std::exception &e) {
            lua_pushstring(l, e.what());
        }
        return lua_error(l);
    }
    static int cue(lua_State *l) {
        double t = number(l, 1);
        int code = int(luaL_checkinteger(l, 2));
        if (t < get(l).time())
            return luaL_error(l, "cue is in the past");
        get(l).add_cue(t, code);
        return 0;
    }
    Impl(Runtime &r, const std::string &file) : runtime(&r), lifetime(r.lifetime_token()) {
        std::ifstream input(std::filesystem::u8path(file),std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open Lua script: "+file);
        std::string source{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        if(input.bad())throw std::runtime_error("Cannot read Lua script: "+file);
        if(source.compare(0,3,"\xef\xbb\xbf")==0)source.erase(0,3);
        if(!source.empty()&&source[0]=='#'){auto end=source.find('\n');source=end==std::string::npos?"":"\n"+source.substr(end+1);}
        auto chunk="@"+file;
        state = luaL_newstate();
        if (!state)
            throw std::runtime_error("Lua allocation failed");
        luaL_openlibs(state);
        lua_newtable(state);
        for (auto pair : {std::pair<const char *, lua_CFunction>{"box", guarded<box_fn>},
                          {"position", guarded<position>},
                          {"rotation", guarded<rotation>},
                          {"camera", guarded<camera>},
                          {"light", guarded<light>},
                          {"clear", guarded<clear>},
                          {"destroy", guarded<destroy>},
                          {"mesh", guarded<mesh_fn>},
                          {"play", guarded<play>},
                          {"seek", guarded<seek>},
                          {"pause_animation", guarded<pause_animation>},
                          {"clips", guarded<clips>},
                          {"bone", guarded<bone>},
                          {"attach", guarded<attach>},
                          {"collider", guarded<collider>},
                          {"raycast", guarded<raycast>},
                          {"physics_box", guarded<physics_box>},
                          {"physics_sphere", guarded<physics_sphere>},
                          {"physics_capsule", guarded<physics_capsule>},
                          {"physics_has", guarded<physics_has>},
                          {"physics_remove", guarded<physics_remove>},
                          {"physics_set_velocity", guarded<physics_set_velocity>},
                          {"physics_velocity", guarded<physics_velocity>},
                          {"physics_force", guarded<physics_force>},
                          {"physics_impulse", guarded<physics_impulse>},
                          {"save_has", guarded<save_has>},
                          {"save_set_int", guarded<save_set_int>},
                          {"save_get_int", guarded<save_get_int>},
                          {"save_set_float", guarded<save_set_float>},
                          {"save_get_float", guarded<save_get_float>},
                          {"save_set_bool", guarded<save_set_bool>},
                          {"save_get_bool", guarded<save_get_bool>},
                          {"save_set_string", guarded<save_set_string>},
                          {"save_get_string", guarded<save_get_string>},
                          {"save_set_vec3", guarded<save_set_vec3>},
                          {"save_get_vec3", guarded<save_get_vec3>},
                          {"save_write", guarded<save_write>},
                          {"save_read", guarded<save_read>},
                          {"save_clear", guarded<save_clear>},
                          {"cue", guarded<cue>}}) {
            lua_pushlightuserdata(state, this);
            lua_pushcclosure(state, pair.second, 1);
            lua_setfield(state, -2, pair.first);
        }
        lua_setglobal(state, "engine");
        if (luaL_loadbuffer(state,source.data(),source.size(),chunk.c_str()) != LUA_OK || lua_pcall(state,0,LUA_MULTRET,0) != LUA_OK) {
            std::string error = lua_tostring(state, -1);
            lua_close(state);
            state = nullptr;
            throw std::runtime_error(error);
        }
    }
    ~Impl() {
        if (state)
            lua_close(state);
    }
};
Script::Script(Runtime &r, const std::string &file) : impl_(std::make_unique<Impl>(r, file)) {}
Script::~Script() = default;
void Script::event(const Event &e) {
    if (impl_->lifetime.expired())
        throw std::runtime_error("Ember Runtime no longer exists");
    auto *l = impl_->state;
    const char *name = nullptr;
    switch (e.type) {
    case Event::Type::Start: name = "start"; break;
    case Event::Type::FixedUpdate: name = "update"; break;
    case Event::Type::KeyDown: name = "key_down"; break;
    case Event::Type::KeyUp: name = "key_up"; break;
    case Event::Type::Cue: name = "cue"; break;
    case Event::Type::MouseMove: name = "mouse_move"; break;
    case Event::Type::MouseDown: name = "mouse_down"; break;
    case Event::Type::MouseUp: name = "mouse_up"; break;
    case Event::Type::MouseWheel: name = "mouse_wheel"; break;
    case Event::Type::GamepadButtonDown: name = "gamepad_button_down"; break;
    case Event::Type::GamepadButtonUp: name = "gamepad_button_up"; break;
    case Event::Type::GamepadAxis: name = "gamepad_axis"; break;
    }
    lua_getglobal(l, name);
    if (!lua_isfunction(l, -1)) {
        lua_pop(l, 1);
        return;
    }
    int arguments = 0;
    lua_pushnumber(l, e.time);
    ++arguments;
    switch (e.type) {
    case Event::Type::MouseMove:
        for (float value : {e.x, e.y, e.dx, e.dy})
            lua_pushnumber(l, value);
        arguments += 4;
        break;
    case Event::Type::MouseDown:
    case Event::Type::MouseUp:
        lua_pushinteger(l, e.code);
        lua_pushnumber(l, e.x);
        lua_pushnumber(l, e.y);
        arguments += 3;
        break;
    case Event::Type::MouseWheel:
        lua_pushnumber(l, e.x);
        lua_pushnumber(l, e.y);
        arguments += 2;
        break;
    case Event::Type::GamepadButtonDown:
    case Event::Type::GamepadButtonUp:
        lua_pushinteger(l, e.device);
        lua_pushinteger(l, e.code);
        arguments += 2;
        break;
    case Event::Type::GamepadAxis:
        lua_pushinteger(l, e.device);
        lua_pushinteger(l, e.code);
        lua_pushnumber(l, e.x);
        arguments += 3;
        break;
    default:
        lua_pushinteger(l, e.code);
        ++arguments;
        break;
    }
    if (lua_pcall(l, arguments, 0, 0) != LUA_OK) {
        std::string error = lua_tostring(l, -1);
        lua_pop(l, 1);
        throw std::runtime_error(error);
    }
}
} // namespace ember
#else
namespace ember {
struct Script::Impl {};
Script::Script(Runtime &, const std::string &) {
    throw std::runtime_error("Lua was not compiled; use EMBER_LUA=ON");
}
Script::~Script() = default;
void Script::event(const Event &) {}
} // namespace ember
#endif
