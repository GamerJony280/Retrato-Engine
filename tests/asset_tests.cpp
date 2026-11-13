#include "ember/asset.hpp"
#include "ember/runtime.hpp"
#include "ufbx/ufbx.h"
#include "cgltf/cgltf.h"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ember;
void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
float mesh_difference(const Mesh &a, const Mesh &b) {
    check(a.triangles.size() == b.triangles.size(), "mesh sizes differ");
    float diff = 0;
    for (size_t i = 0; i < a.triangles.size(); i++)
        for (int k = 0; k < 3; k++)
            diff = std::max(diff, length(a.triangles[i].v[k].position - b.triangles[i].v[k].position));
    return diff;
}
void compare_gpu(const Scene &scene, Config cfg, Camera camera) {
    const char *name = std::getenv("EMBER_TEST_BACKEND");
    if (!name)
        return;
    auto cpu = make_cpu(0), gpu = make_backend(name, 0);
    SceneRenderer renderer;
    Frame f;
    for (auto profile : {Profile::Normal, Profile::Pixel, Profile::PS1, Profile::N64}) {
        cfg.profile = profile;
        renderer.prepare(scene, camera, cfg, f);
        cpu->draw(f);
        gpu->draw(f);
        auto &a = cpu->readback();
        auto &b = gpu->readback();
        double error = 0;
        for (size_t i = 0; i < a.size(); i++)
            for (int k = 0; k < 3; k++)
                error += std::abs(int((a[i] >> (8 * k)) & 255) - int((b[i] >> (8 * k)) & 255));
        error /= a.size() * 3;
        std::cout << name << " textured " << profile_name(profile) << " MAE=" << error << "\n";
        check(error < 1.0, "textured GPU/CPU mismatch");
    }
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Source path required");
        std::string root = argv[1];
        {
            // Explicitly misalign the backing address, not only the glTF byte offset.
            alignas(4) unsigned char storage[16]{};
            cgltf_buffer_view view{};
            view.data = storage + 1;
            view.size = 8;
            cgltf_accessor accessor{};
            accessor.buffer_view = &view;
            accessor.count = 1;
            accessor.type = cgltf_type_scalar;
            float out = 0;
            uint32_t integer = 0;
            auto scalar = [&](auto value, cgltf_component_type type) {
                std::memcpy(storage + 1, &value, sizeof(value));
                accessor.component_type = type;
                accessor.stride = sizeof(value);
                accessor.normalized = 0;
                check(cgltf_accessor_read_float(&accessor, 0, &out, 1), "unaligned scalar float read");
                check(out == float(value), "unaligned scalar value");
                if (type != cgltf_component_type_r_32f) {
                    check(cgltf_accessor_read_uint(&accessor, 0, &integer, 1), "unaligned integer read");
                    check(integer == uint32_t(value), "unaligned integer value");
                }
            };
            scalar(1.25f, cgltf_component_type_r_32f);
            scalar(int16_t(123), cgltf_component_type_r_16);
            scalar(uint16_t(321), cgltf_component_type_r_16u);
            check(cgltf_accessor_read_index(&accessor, 0) == 321, "unaligned 16-bit index");
            accessor.normalized = 1;
            check(cgltf_accessor_read_float(&accessor, 0, &out, 1) &&
                      std::abs(out - 321.f / 65535.f) < 1e-6f, "unaligned normalized component");
            scalar(uint32_t(54321), cgltf_component_type_r_32u);
            check(cgltf_accessor_read_index(&accessor, 0) == 54321, "unaligned 32-bit index");
            // Mutation fixture supplied by Sonnet: a normal importer exception is
            // acceptable for corrupt data; a sanitizer fault or process abort isn't.
            try { import_asset(root + "/tests/fixtures/cgltf_misaligned_read.glb"); }
            catch (const std::runtime_error &) {}
        }
        auto gltf = import_asset(root + "/assets/robot.gltf"), glb = import_asset(root + "/assets/robot.glb");
        check(glb->images.size() == 1 && glb->clips.size() == 2 && glb->nodes.size() == 8,
              "GLB skin/material/clip import");
        auto a = instantiate(gltf), b = instantiate(glb);
        Mesh second_destination;
        a.animator->mesh(second_destination);
        check(mesh_difference(a.mesh, second_destination) == 0,
              "Animator mesh can populate a second destination without a pose change");
        Object copied = a;
        check(copied.animator.get() != a.animator.get() && &copied.animator->asset() == &a.animator->asset(),
              "copy shares asset but owns independent pose");
        check(mesh_difference(a.mesh, b.mesh) < 1e-6, "glTF and GLB rest pose match");
        a.animator->play("Wave");
        a.animator->seek(.5);
        a.animator->mesh(a.mesh);
        check(mesh_difference(a.mesh, b.mesh) > .2f, "skeletal deformation changes geometry");
        Mesh wave = a.mesh;
        a.animator->pause(true);
        update_animation(a, .2f);
        check(mesh_difference(a.mesh, wave) == 0, "paused pose unchanged");
        a.animator->play("Walk", true, .25f);
        a.animator->mesh(a.mesh);
        check(mesh_difference(a.mesh, wave) < 1e-5, "crossfade starts from current pose");
        update_animation(a, .25f);
        b.animator->play("Walk");
        b.animator->seek(.25);
        b.animator->mesh(b.mesh);
        check(mesh_difference(a.mesh, b.mesh) < 1e-5, "crossfade reaches destination clip");
        save_asset(*glb, "robot-test.embasset");
        auto cooked = load_asset("robot-test.embasset");
        auto invalid_asset = *glb;
        invalid_asset.primitives[0].indices.push_back(0);
        bool save_rejected = false;
        try {
            save_asset(invalid_asset, "robot-test.embasset");
        } catch (...) {
            save_rejected = true;
        }
        check(save_rejected && load_asset("robot-test.embasset")->nodes.size() == cooked->nodes.size(),
              "failed asset save preserves previous cooked file");
        auto c = instantiate(cooked);
        c.animator->play("Walk");
        c.animator->seek(.25);
        c.animator->mesh(c.mesh);
        check(mesh_difference(c.mesh, b.mesh) < 1e-6, "cooked skin and clips roundtrip");
        check(cooked->images[0]->rgba == glb->images[0]->rgba, "embedded texture roundtrip");
        auto image_references = cooked->images[0].use_count();
        CollisionMesh compact_collision;
        compact_collision.cook(c.mesh);
        check(cooked->images[0].use_count() == image_references,
              "collision cooking does not retain render images");
        {
            std::ifstream in("robot-test.embasset", std::ios::binary);
            std::string s((std::istreambuf_iterator<char>(in)), {});
            std::ofstream out("truncated.embasset", std::ios::binary);
            out.write(s.data(), s.size() / 2);
        }
        bool rejected = false;
        try {
            load_asset("truncated.embasset");
        } catch (...) {
            rejected = true;
        }
        check(rejected, "truncated cooked asset rejected");
        ImportOptions tiny;
        tiny.max_vertices = 4;
        rejected = false;
        try {
            import_asset(root + "/assets/robot.glb", tiny);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "vertex budget enforced");
        // Analytic check for all glTF interpolation modes, including cubic tangents.
        auto curves = std::make_shared<Asset>(*glb);
        Track track;
        track.node = 1;
        track.path = 0;
        Key ka, kb;
        ka.time = 0;
        kb.time = 1;
        kb.value = {2, 0, 0, 0};
        track.keys = {ka, kb};
        curves->clips = {Clip{"Curve", 1, {track}}};
        for (int mode = 0; mode < 3; mode++) {
            curves->clips[0].tracks[0].interpolation = mode;
            auto instance = instantiate(curves);
            float rest = instance.mesh.triangles[0].v[0].position.x;
            instance.animator->play("Curve", false);
            instance.animator->seek(.25f);
            instance.animator->mesh(instance.mesh);
            float expected = mode == 0 ? 0 : mode == 1 ? .5f : .3125f;
            check(std::abs(instance.mesh.triangles[0].v[0].position.x - rest - expected) < 1e-5f,
                  "STEP/LINEAR/CUBIC analytic translation");
        }
        auto obj = import_asset(root + "/assets/textured.obj");
        check(obj->images.size() == 1 && obj->primitives[0].indices.size() == 6,
              "OBJ MTL texture and triangulation");
        auto fbx = import_asset(root + "/tests/fixtures/sausage.fbx");
        check(fbx->clips.size() == 3 && !fbx->primitives[0].joints.empty(), "FBX clips and skin imported");
        auto f = instantiate(fbx);
        f.animator->play("Skeleton|Wiggle");
        f.animator->seek(.4);
        f.animator->mesh(f.mesh);
        // Independent oracle: compare our baked runtime against ufbx's evaluated skin.
        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1;
        opts.generate_missing_normals = true;
        opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
        opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
        ufbx_error err{};
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> source(
            ufbx_load_file((root + "/tests/fixtures/sausage.fbx").c_str(), &opts, &err), ufbx_free_scene);
        check(bool(source), "FBX oracle load");
        ufbx_anim *anim = nullptr;
        for (auto *stack : source->anim_stacks)
            if (std::string(stack->name.data) == "Skeleton|Wiggle")
                anim = stack->anim;
        check(anim != nullptr, "FBX oracle clip");
        ufbx_evaluate_opts eo{};
        eo.evaluate_skinning = true;
        std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> evaluated(
            ufbx_evaluate_scene(source.get(), anim, anim->time_begin + .4, &eo, &err), ufbx_free_scene);
        check(bool(evaluated), "FBX oracle evaluate");
        float max_error = 0;
        for (auto *n : evaluated->nodes)
            if (n->mesh) {
                auto *m = n->mesh;
                for (size_t i = 0; i < m->num_indices; i++) {
                    auto p = ufbx_get_vertex_vec3(&m->skinned_position, i);
                    if (m->skinned_is_local)
                        p = ufbx_transform_position(&n->geometry_to_world, p);
                    Vec3 point{float(p.x), float(p.y), float(p.z)};
                    float nearest = 1e30f;
                    for (auto &t : f.mesh.triangles)
                        for (auto &v : t.v)
                            nearest = std::min(nearest, length(v.position - point));
                    max_error = std::max(max_error, nearest);
                }
            }
        std::cout << "FBX baked vs reference maximum distance=" << max_error << "\n";
        check(max_error < .02f, "FBX runtime skin differs from independent evaluator");
        Runtime runtime(Config{});
        auto first = runtime.add_model(root + "/assets/robot.glb"),
             second = runtime.add_model(root + "/assets/robot.glb");
        check(&runtime.scene.objects[first].animator->asset() ==
                  &runtime.scene.objects[second].animator->asset(),
              "model instances share assets");
        runtime.scene.objects.push_back({box({.1f, .4f, .1f}, {1, 1, 0}), {0, -.6f, 0}});
        runtime.attach(2, first, "RightArm");
        auto before = runtime.scene.objects[2].parent_transform.point({0, -.6f, 0});
        runtime.scene.objects[first].animator->play("Wave");
        runtime.scene.objects[first].animator->seek(.5);
        runtime.update_attachments();
        check(length(before - runtime.scene.objects[2].parent_transform.point({0, -.6f, 0})) > .2f,
              "bone attachment follows pose");
        {
            auto parent = runtime.object_ref(first);
            auto child = runtime.object_ref(2);
            const auto capacity = runtime.scene.objects.capacity();
            while (runtime.scene.objects.capacity() == capacity)
                runtime.scene.objects.push_back({box({.05f, .05f, .05f}, {1, 0, 1}), {20, 20, 20}});
            check(runtime.object_alive(parent) && runtime.object_alive(child),
                  "object identity survives vector reallocation");
            runtime.update_attachments();

            // Structural C++ mutation may invalidate a reference, but it must
            // never redirect an attachment to an unrelated replacement.
            runtime.scene.objects.erase(runtime.scene.objects.begin() + long(first));
            check(!runtime.object_alive(parent), "erased object reference expires");
            runtime.scene.objects.push_back({box({.1f, .1f, .1f}, {0, 1, 1}), {5, 5, 5}});
            auto &replacement = runtime.scene.objects.back();
            auto untouched = replacement.parent_transform.m;
            runtime.update_attachments();
            check(replacement.parent_transform.m == untouched,
                  "stale attachment cannot overwrite replacement object");
        }
        Scene scene;
        scene.objects.push_back(c);
        scene.objects.push_back({box({7, .03f, 7}, {.65f, .65f, .65f}), {0, -.07f, 0}});
        Config cfg;
        cfg.resolution_quality = 10;
        cfg.shadow_quality = 70;
        Camera camera;
        camera.position = {4, 3, -6};
        camera.target = {0, 1, 0};
        compare_gpu(scene, cfg, camera);
        auto cutout = std::make_shared<Asset>(*glb);
        auto cutout_image = std::make_shared<ImageData>(*glb->images[0]);
        for (int y = 0; y < cutout_image->height; y++)
            for (int x = 0; x < cutout_image->width / 2; x++)
                cutout_image->rgba[size_t(y) * cutout_image->width + x] &= 0x00ffffffu;
        cutout->images[0] = cutout_image;
        for (auto &m : cutout->materials)
            m.alpha_cutoff = .5f;
        Scene cutout_scene = scene;
        cutout_scene.objects[0] = instantiate(cutout);
        compare_gpu(cutout_scene, cfg, camera);

        SceneRenderer renderer;
        Frame frame;
        auto cpu = make_cpu(0);
        cfg.profile = Profile::Pixel;
        cfg.lighting_quality = 1;
        renderer.prepare(scene, camera, cfg, frame);
        cpu->draw(frame);
        auto coarse = cpu->readback();
        cfg.lighting_quality = 100;
        renderer.prepare(scene, camera, cfg, frame);
        cpu->draw(frame);
        check(coarse != cpu->readback(), "lighting grid independent of image resolution");
        // A single receiver quad must receive interior shadows, not vertex-only lighting.
        auto data = frame.resources.size();
        scene.objects.erase(scene.objects.begin());
        renderer.prepare(scene, camera, cfg, frame);
        check(frame.texture_words == 0 && frame.resources.size() < data,
              "unloaded textures leave frame resource pool");
        std::cout << "PASS: OBJ/MTL, glTF/GLB, FBX, skinning, clips, blend, pause, cook, limits, "
                     "attachments, textures, pixel shadows\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
