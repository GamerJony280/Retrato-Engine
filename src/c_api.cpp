#include "ember/c_api.h"
#include "ember/runtime.hpp"
#include <stdexcept>
using namespace ember;
struct EmberHandle {
    Runtime runtime;
    SceneRenderer renderer;
    std::unique_ptr<Backend> backend;
    explicit EmberHandle(Config c) : runtime(c), backend(make_backend(c.backend, c.worker_threads)) {};
};
static thread_local std::string last_error;
template <class F> int protect(F fn) {
    try {
        last_error.clear();
        return fn();
    } catch (const std::exception &e) {
        last_error = e.what();
        return -1;
    } catch (...) {
        last_error = "Unknown native error";
        return -1;
    }
}
static void valid(EmberHandle *h) {
    if (!h)
        throw std::runtime_error("Null engine handle");
}
static void finite(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw std::runtime_error("Non-finite position");
}
extern "C" {
uint32_t ember_abi_version() {
    return 1;
}
EmberHandle *ember_create(const char *file, const char *backend) {
    EmberHandle *h = nullptr;
    protect([&] {
        if (!file)
            throw std::runtime_error("Null config path");
        auto c = Config::load(file);
        if (backend)
            c.backend = backend;
        h = new EmberHandle(c);
        return 0;
    });
    return h;
}
void ember_destroy(EmberHandle *h) {
    delete h;
}
const char *ember_last_error() {
    return last_error.c_str();
}
int ember_add_box(EmberHandle *h, float x, float y, float z, float sx, float sy, float sz) {
    return protect([&] {
        valid(h);
        finite(x, y, z);
        finite(sx, sy, sz);
        h->runtime.scene.objects.push_back({box({sx, sy, sz}, {.8f, .4f, .2f}), {x, y, z}});
        return int(h->runtime.scene.objects.size() - 1);
    });
}
int ember_set_position(EmberHandle *h, int object, float x, float y, float z) {
    return protect([&] {
        valid(h);
        finite(x, y, z);
        if (object < 0 || size_t(object) >= h->runtime.scene.objects.size())
            throw std::runtime_error("Invalid object");
        h->runtime.scene.objects[object].position = {x, y, z};
        return 0;
    });
}
int ember_set_camera(EmberHandle *h, float x, float y, float z, float tx, float ty, float tz) {
    return protect([&] {
        valid(h);
        finite(x, y, z);
        finite(tx, ty, tz);
        h->runtime.camera.position = {x, y, z};
        h->runtime.camera.target = {tx, ty, tz};
        return 0;
    });
}
int ember_add_model(EmberHandle *h, const char *path, float x, float y, float z) {
    return protect([&] {
        valid(h);
        finite(x, y, z);
        if (!path)
            throw std::runtime_error("Missing model path");
        return int(h->runtime.add_model(path, {x, y, z}));
    });
}
int ember_play(EmberHandle *h, int object, const char *clip, int loop, float fade) {
    return protect([&] {
        valid(h);
        if (!clip)
            throw std::runtime_error("Missing clip name");
        auto &o = h->runtime.scene.objects.at(size_t(object));
        if (!o.animator)
            throw std::runtime_error("Object has no animation");
        o.animator->play(clip, loop != 0, fade);
        o.animator->mesh(o.mesh);
        return 0;
    });
}
int ember_seek(EmberHandle *h, int object, float t) {
    return protect([&] {
        valid(h);
        auto &o = h->runtime.scene.objects.at(size_t(object));
        if (!o.animator)
            throw std::runtime_error("Object has no animation");
        o.animator->seek(t);
        o.animator->mesh(o.mesh);
        return 0;
    });
}
int ember_pause_animation(EmberHandle *h, int object, int pause) {
    return protect([&] {
        valid(h);
        auto &o = h->runtime.scene.objects.at(size_t(object));
        if (!o.animator)
            throw std::runtime_error("Object has no animation");
        o.animator->pause(pause != 0);
        return 0;
    });
}
int ember_tick(EmberHandle *h, double t) {
    return protect([&] {
        valid(h);
        h->runtime.tick(t);
        return 0;
    });
}
int ember_attach(EmberHandle *h, int child, int parent, const char *bone) {
    return protect([&] {
        valid(h);
        if (!bone)
            throw std::runtime_error("Missing bone name");
        h->runtime.attach(size_t(child), size_t(parent), bone);
        return 0;
    });
}
int ember_render(EmberHandle *h) {
    return protect([&] {
        valid(h);
        h->renderer.prepare(h->runtime.scene, h->runtime.camera, h->runtime.config, h->runtime.frame);
        h->backend->draw(h->runtime.frame);
        return 0;
    });
}
const uint32_t *ember_pixels(EmberHandle *h, int *w, int *height) {
    const uint32_t *p = nullptr;
    protect([&] {
        valid(h);
        if (!w || !height || h->runtime.frame.width == 0)
            throw std::runtime_error("Render first; supply output dimensions");
        *w = h->runtime.frame.width;
        *height = h->runtime.frame.height;
        p = h->backend->readback().data();
        return 0;
    });
    return p;
}
}
