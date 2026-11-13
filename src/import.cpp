#include "cgltf/cgltf.h"
#include "ember/asset.hpp"
#include "stb/stb_image.h"
#include "ufbx/ufbx.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <functional>
#include <set>
#include <stdexcept>
#include <unordered_map>
namespace ember {
namespace {
std::vector<unsigned char> bytes(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot open: " + p.string());
    auto n = f.tellg();
    if (n < 0 || n > 128 * 1024 * 1024)
        throw std::runtime_error("Resource exceeds 128 MiB import limit");
    std::vector<unsigned char> b((size_t(n)));
    f.seekg(0);
    if (!f.read((char *)b.data(), n))
        throw std::runtime_error("Cannot read resource");
    return b;
}
std::vector<unsigned char> uri_data(const std::string &u, const std::filesystem::path &dir) {
    if (u.rfind("data:", 0) == 0) {
        auto start = u.find(",");
        if (start == std::string::npos || u.substr(0, start).find(";base64") == std::string::npos)
            throw std::runtime_error("Only base64 data URIs supported");
        std::vector<unsigned char> b;
        uint32_t acc = 0;
        int bits = 0;
        const std::string abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = start + 1; i < u.size(); i++) {
            if (u[i] == '=')
                break;
            auto v = abc.find(u[i]);
            if (v == std::string::npos)
                throw std::runtime_error("Invalid base64");
            acc = (acc << 6) | uint32_t(v);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                b.push_back((acc >> bits) & 255);
            }
        }
        return b;
    }
    if (u.find("://") != std::string::npos)
        throw std::runtime_error("Network resources are not supported");
    std::string s = u;
    cgltf_decode_uri(s.data());
    s.resize(std::char_traits<char>::length(s.c_str()));
    std::replace(s.begin(), s.end(), '\\', '/');
    return bytes(dir / std::filesystem::u8path(s));
}
int image(Asset &a, const std::vector<unsigned char> &b, const ImportOptions &o) {
    int w, h, c;
    if (b.size() > 128 * 1024 * 1024 || !stbi_info_from_memory(b.data(), int(b.size()), &w, &h, &c))
        throw std::runtime_error("Unsupported/corrupt image (PNG/JPEG/BMP/TGA supported)");
    if (w < 1 || h < 1 || w > o.max_texture_size || h > o.max_texture_size)
        throw std::runtime_error(
            "Texture dimensions exceed import limit; resize the source or raise max_texture_size");
    size_t used = 0;
    for (auto &i : a.images)
        used += i->rgba.size() * 4;
    if (size_t(w) * h * 4 > o.max_image_bytes - used)
        throw std::runtime_error("Texture memory budget exceeded");
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> p(
        stbi_load_from_memory(b.data(), int(b.size()), &w, &h, &c, 4), stbi_image_free);
    if (!p)
        throw std::runtime_error("Image decoding failed");
    auto im = std::make_shared<ImageData>();
    im->width = w;
    im->height = h;
    im->rgba.resize(size_t(w) * h);
    for (size_t i = 0; i < im->rgba.size(); i++)
        im->rgba[i] = uint32_t(p.get()[i * 4]) | (uint32_t(p.get()[i * 4 + 1]) << 8) |
                      (uint32_t(p.get()[i * 4 + 2]) << 16) | (uint32_t(p.get()[i * 4 + 3]) << 24);
    a.images.push_back(im);
    return int(a.images.size() - 1);
}
void warn(Asset &a, const std::string &s) {
    if (std::find(a.warnings.begin(), a.warnings.end(), s) == a.warnings.end())
        a.warnings.push_back(s);
}
Vec3 cv(ufbx_vec3 v) {
    return {float(v.x), float(v.y), float(v.z)};
}
Mat4 cm(ufbx_matrix m) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 3; row++)
            r.m[c * 4 + row] = float(m.cols[c].v[row]);
    return r;
}
Transform ct(ufbx_transform t) {
    return {cv(t.translation),
            cv(t.scale),
            {float(t.rotation.x), float(t.rotation.y), float(t.rotation.z), float(t.rotation.w)}};
}
void normals(Primitive &p) {
    std::vector<Vec3> sum(p.vertices.size());
    for (size_t i = 0; i < p.indices.size(); i += 3) {
        auto a = p.indices[i], b = p.indices[i + 1], c = p.indices[i + 2];
        auto n = cross(p.vertices[b].position - p.vertices[a].position,
                       p.vertices[c].position - p.vertices[a].position);
        for (auto j : {a, b, c})
            sum[j] = sum[j] + n;
    }
    for (size_t i = 0; i < p.vertices.size(); i++)
        if (length(p.vertices[i].normal) < 1e-8f)
            p.vertices[i].normal = normalized(sum[i]);
}
std::shared_ptr<Asset> gltf(const std::string &path, const ImportOptions &o) {
    cgltf_options opts{};
    // cgltf's default fopen accepts an ANSI path on Windows. Keep UTF-8 at
    // the engine boundary and convert through filesystem, including .bin files.
    opts.file.read=[](const cgltf_memory_options *mem,const cgltf_file_options *,const char *file,cgltf_size *size,void **data)->cgltf_result {
        try {
            std::ifstream input(std::filesystem::u8path(file),std::ios::binary|std::ios::ate);
            if(!input)return cgltf_result_file_not_found;
            auto length=input.tellg();if(length<0||length>256*1024*1024)return cgltf_result_io_error;
            auto count=size&&*size?*size:size_t(length);if(count>size_t(length))return cgltf_result_io_error;
            void *buffer=mem->alloc_func?mem->alloc_func(mem->user_data,count):std::malloc(count);
            if(!buffer)return cgltf_result_out_of_memory;
            input.seekg(0);if(!input.read(static_cast<char*>(buffer),std::streamsize(count))){if(mem->free_func)mem->free_func(mem->user_data,buffer);else std::free(buffer);return cgltf_result_io_error;}
            if(size)*size=count;
            *data=buffer;
            return cgltf_result_success;
        }catch(...){return cgltf_result_io_error;}
    };
    cgltf_data *raw = nullptr;
    auto result = cgltf_parse_file(&opts, path.c_str(), &raw);
    if (result != cgltf_result_success)
        throw std::runtime_error("glTF parse error: " + std::to_string(result));
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> d(raw, cgltf_free);
    if (cgltf_load_buffers(&opts, d.get(), path.c_str()) != cgltf_result_success ||
        cgltf_validate(d.get()) != cgltf_result_success)
        throw std::runtime_error("glTF buffer/validation error");
    if (d->nodes_count > o.max_nodes)
        throw std::runtime_error("Node budget exceeded");
    auto a = std::make_shared<Asset>();
    auto dir = std::filesystem::u8path(path).parent_path();
    for (size_t i = 0; i < d->extensions_required_count; i++) {
        std::string e = d->extensions_required[i];
        if (e != "KHR_materials_unlit" && e != "KHR_texture_transform")
            throw std::runtime_error("Required glTF extension unsupported: " + e);
    }
    std::unordered_map<cgltf_image *, int> images;
    auto load_image = [&](cgltf_image *im) {
        if (!im)
            return -1;
        auto it = images.find(im);
        if (it != images.end())
            return it->second;
        std::vector<unsigned char> b;
        if (im->uri)
            b = uri_data(im->uri, dir);
        else if (im->buffer_view) {
            auto *v = im->buffer_view;
            auto *p = (const unsigned char *)v->buffer->data + v->offset;
            b.assign(p, p + v->size);
        } else
            throw std::runtime_error("glTF image has no data");
        int id = image(*a, b, o);
        images[im] = id;
        return id;
    };
    for (size_t i = 0; i < d->materials_count; i++) {
        auto &m = d->materials[i];
        Material t;
        t.name = m.name ? m.name : "material_" + std::to_string(i);
        auto &b = m.pbr_metallic_roughness;
        t.base_color = {b.base_color_factor[0], b.base_color_factor[1], b.base_color_factor[2]};
        t.opacity = b.base_color_factor[3];
        t.emissive = {m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
        t.unlit = m.unlit;
        t.double_sided = m.double_sided;
        t.alpha_cutoff = m.alpha_mode == cgltf_alpha_mode_mask ? m.alpha_cutoff : 0;
        if (m.alpha_mode == cgltf_alpha_mode_blend) {
            t.alpha_cutoff = .5f;
            warn(*a, "Alpha BLEND approximated with cutout; sorted translucency is not implemented");
        }
        if (b.base_color_texture.texture) {
            auto *tex = b.base_color_texture.texture;
            t.image = load_image(tex->image);
            if (tex->sampler) {
                auto wrap = [](cgltf_wrap_mode v) {
                    return v == cgltf_wrap_mode_clamp_to_edge     ? 1
                           : v == cgltf_wrap_mode_mirrored_repeat ? 2
                                                                  : 0;
                };
                t.wrap_u = wrap(tex->sampler->wrap_s);
                t.wrap_v = wrap(tex->sampler->wrap_t);
            }
        }
        if (m.normal_texture.texture || m.occlusion_texture.texture || m.emissive_texture.texture ||
            b.metallic_roughness_texture.texture)
            warn(*a, "Normal/occlusion/emissive/metallic-roughness texture maps are not rendered by the "
                     "retro material");
        a->materials.push_back(t);
    }
    a->materials.push_back(Material{});
    std::vector<int> ids(d->nodes_count, -1);
    std::vector<int> vis(d->nodes_count, 0);
    std::function<int(cgltf_node *)> node = [&](cgltf_node *n) {
        size_t i = n - d->nodes;
        if (ids[i] >= 0)
            return ids[i];
        if (vis[i])
            throw std::runtime_error("glTF node cycle");
        vis[i] = 1;
        int parent = n->parent ? node(n->parent) : -1;
        AssetNode out;
        out.parent = parent;
        out.name = n->name ? n->name : "node_" + std::to_string(i);
        if (n->has_matrix) {
            ufbx_matrix m{};
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 3; r++)
                    m.cols[c].v[r] = n->matrix[c * 4 + r];
            out.local = ct(ufbx_matrix_to_transform(&m));
        } else {
            out.local.translation = {n->translation[0], n->translation[1], n->translation[2]};
            out.local.scale = {n->scale[0], n->scale[1], n->scale[2]};
            out.local.rotation = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
        }
        ids[i] = int(a->nodes.size());
        a->nodes.push_back(out);
        return ids[i];
    };
    for (size_t i = 0; i < d->nodes_count; i++)
        node(&d->nodes[i]);
    std::set<cgltf_node *> active;
    std::function<void(cgltf_node *)> activate = [&](cgltf_node *n) {
        if (!active.insert(n).second)
            return;
        for (size_t i = 0; i < n->children_count; i++)
            activate(n->children[i]);
    };
    if (d->scene)
        for (size_t i = 0; i < d->scene->nodes_count; i++)
            activate(d->scene->nodes[i]);
    else
        for (size_t i = 0; i < d->nodes_count; i++)
            activate(&d->nodes[i]);
    size_t vertices = 0, triangles = 0;
    for (size_t ni = 0; ni < d->nodes_count; ni++) {
        auto &n = d->nodes[ni];
        if (!n.mesh || !active.count(&n))
            continue;
        for (size_t pi = 0; pi < n.mesh->primitives_count; pi++) {
            auto &p = n.mesh->primitives[pi];
            if (p.type != cgltf_primitive_type_triangles && p.type != cgltf_primitive_type_triangle_strip &&
                p.type != cgltf_primitive_type_triangle_fan) {
                warn(*a, "Point/line primitives skipped");
                continue;
            }
            Primitive out;
            out.node = ids[ni];
            out.material = p.material ? int(p.material - d->materials) : int(a->materials.size() - 1);
            const cgltf_accessor *pos = nullptr, *norm = nullptr, *uv = nullptr, *col = nullptr,
                                 *joints = nullptr, *weights = nullptr;
            int uvset = p.material ? p.material->pbr_metallic_roughness.base_color_texture.texcoord : 0;
            auto *view = p.material ? &p.material->pbr_metallic_roughness.base_color_texture : nullptr;
            if (view && view->has_transform && view->transform.has_texcoord)
                uvset = view->transform.texcoord;
            for (size_t k = 0; k < p.attributes_count; k++) {
                auto &at = p.attributes[k];
                if (at.type == cgltf_attribute_type_position)
                    pos = at.data;
                else if (at.type == cgltf_attribute_type_normal)
                    norm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == uvset)
                    uv = at.data;
                else if (at.type == cgltf_attribute_type_color && at.index == 0)
                    col = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0)
                    joints = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0)
                    weights = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index > 0)
                    warn(*a, "Only the first four joint influences are retained and normalized");
            }
            if (!pos)
                throw std::runtime_error("glTF primitive has no positions");
            vertices += pos->count;
            if (vertices > o.max_vertices)
                throw std::runtime_error("Vertex budget exceeded");
            out.vertices.resize(pos->count);
            if (n.skin) {
                for (size_t j = 0; j < n.skin->joints_count; j++) {
                    out.joints.push_back(ids[n.skin->joints[j] - d->nodes]);
                    Mat4 m;
                    if (n.skin->inverse_bind_matrices &&
                        !cgltf_accessor_read_float(n.skin->inverse_bind_matrices, j, m.m.data(), 16))
                        throw std::runtime_error("Invalid bind matrix");
                    out.inverse_bind.push_back(m);
                }
            }
            auto read = [](const cgltf_accessor *x, size_t i, float *v, size_t count) {
                if (x && !cgltf_accessor_read_float(x, i, v, count))
                    throw std::runtime_error("Invalid glTF attribute");
            };
            for (size_t v = 0; v < pos->count; v++) {
                auto &dst = out.vertices[v];
                float data[4]{};
                read(pos, v, data, 3);
                dst.position = {data[0], data[1], data[2]};
                if (norm) {
                    read(norm, v, data, 3);
                    dst.normal = {data[0], data[1], data[2]};
                }
                if (uv) {
                    read(uv, v, data, 2);
                    dst.uv = {data[0], data[1]};
                    if (view && view->has_transform) {
                        auto &t = view->transform;
                        float x = dst.uv.x * t.scale[0], y = dst.uv.y * t.scale[1];
                        dst.uv = {std::cos(t.rotation) * x - std::sin(t.rotation) * y + t.offset[0],
                                  std::sin(t.rotation) * x + std::cos(t.rotation) * y + t.offset[1]};
                    }
                }
                if (col) {
                    read(col, v, data, 4);
                    dst.color = {data[0], data[1], data[2]};
                }
                if (joints && weights && n.skin) {
                    cgltf_uint ji[4];
                    if (!cgltf_accessor_read_uint(joints, v, ji, 4))
                        throw std::runtime_error("Invalid joints");
                    read(weights, v, data, 4);
                    float total = 0;
                    for (int k = 0; k < 4; k++) {
                        if (ji[k] >= out.joints.size() && data[k] > 0)
                            throw std::runtime_error("Joint index outside skin");
                        dst.joints[k] = ji[k];
                        dst.weights[k] = std::max(0.f, data[k]);
                        total += dst.weights[k];
                    }
                    if (total > 0)
                        for (auto &w : dst.weights)
                            w /= total;
                }
            }
            size_t count = p.indices ? p.indices->count : pos->count;
            std::vector<uint32_t> ix(count);
            for (size_t k = 0; k < count; k++) {
                ix[k] = p.indices ? uint32_t(cgltf_accessor_read_index(p.indices, k)) : uint32_t(k);
                if (ix[k] >= pos->count)
                    throw std::runtime_error("Index out of bounds");
            }
            if (p.type == cgltf_primitive_type_triangles) {
                if (count % 3)
                    throw std::runtime_error("Invalid triangle count");
                out.indices = std::move(ix);
            } else
                for (size_t k = 2; k < count; k++) {
                    if (p.type == cgltf_primitive_type_triangle_fan)
                        out.indices.insert(out.indices.end(), {ix[0], ix[k - 1], ix[k]});
                    else if (k & 1)
                        out.indices.insert(out.indices.end(), {ix[k - 1], ix[k - 2], ix[k]});
                    else
                        out.indices.insert(out.indices.end(), {ix[k - 2], ix[k - 1], ix[k]});
                }
            triangles += out.indices.size() / 3;
            if (triangles > o.max_triangles)
                throw std::runtime_error("Triangle budget exceeded");
            if (p.targets_count)
                warn(*a, "Morph targets are not implemented; skeletal and node animations are supported");
            normals(out);
            a->primitives.push_back(std::move(out));
        }
    }
    for (size_t i = 0; i < d->animations_count; i++) {
        auto &anim = d->animations[i];
        Clip c;
        c.name = anim.name ? anim.name : "clip_" + std::to_string(i);
        for (size_t k = 0; k < anim.channels_count; k++) {
            auto &ch = anim.channels[k];
            if (!ch.target_node)
                continue;
            if (ch.target_path == cgltf_animation_path_type_weights) {
                warn(*a, "Morph weight animation skipped");
                continue;
            }
            Track t;
            t.node = ids[ch.target_node - d->nodes];
            t.path = ch.target_path == cgltf_animation_path_type_translation ? 0
                     : ch.target_path == cgltf_animation_path_type_rotation  ? 1
                                                                             : 2;
            auto &s = *ch.sampler;
            t.interpolation = s.interpolation == cgltf_interpolation_type_step           ? 0
                              : s.interpolation == cgltf_interpolation_type_cubic_spline ? 2
                                                                                         : 1;
            size_t stride = t.interpolation == 2 ? 3 : 1;
            for (size_t ki = 0; ki < s.input->count; ki++) {
                Key key;
                cgltf_accessor_read_float(s.input, ki, &key.time, 1);
                cgltf_accessor_read_float(s.output, ki * stride + (stride == 3 ? 1 : 0), key.value.data(),
                                          t.path == 1 ? 4 : 3);
                if (stride == 3) {
                    cgltf_accessor_read_float(s.output, ki * 3, key.in.data(), t.path == 1 ? 4 : 3);
                    cgltf_accessor_read_float(s.output, ki * 3 + 2, key.out.data(), t.path == 1 ? 4 : 3);
                }
                c.duration = std::max(c.duration, key.time);
                t.keys.push_back(key);
            }
            c.tracks.push_back(std::move(t));
        }
        a->clips.push_back(std::move(c));
    }
    return a;
}
std::shared_ptr<Asset> fbx(const std::string &path, const ImportOptions &o) {
    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;
    opts.load_external_files = true;
    opts.ignore_missing_external_files = false;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.temp_allocator.memory_limit = 256 * 1024 * 1024;
    opts.result_allocator.memory_limit = 256 * 1024 * 1024;
    ufbx_error error{};
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> s(ufbx_load_file(path.c_str(), &opts, &error),
                                                              ufbx_free_scene);
    if (!s)
        throw std::runtime_error("FBX/OBJ import: " +
                                 std::string(error.description.data, error.description.length));
    if (s->nodes.count > o.max_nodes)
        throw std::runtime_error("Node budget exceeded");
    auto a = std::make_shared<Asset>();
    auto dir = std::filesystem::u8path(path).parent_path();
    std::unordered_map<ufbx_texture *, int> images;
    for (auto *m : s->materials) {
        Material t;
        t.name = m->name.data;
        auto &b = m->pbr.base_color;
        t.base_color = b.has_value ? cv(b.value_vec3) : Vec3{1, 1, 1};
        if (m->pbr.base_factor.has_value)
            t.base_color = t.base_color * float(m->pbr.base_factor.value_real);
        if (m->pbr.emission_color.has_value)
            t.emissive = cv(m->pbr.emission_color.value_vec3);
        if (m->pbr.opacity.has_value) {
            t.opacity = float(m->pbr.opacity.value_real);
            if (t.opacity < 1) {
                t.alpha_cutoff = .5f;
                warn(*a, "FBX/OBJ partial opacity approximated by cutout");
            }
        }
        if (b.texture && b.texture_enabled) {
            auto *tex = b.texture;
            if (tex->file_textures.count)
                tex = tex->file_textures.data[0];
            t.wrap_u = tex->wrap_u == UFBX_WRAP_CLAMP ? 1 : 0;
            t.wrap_v = tex->wrap_v == UFBX_WRAP_CLAMP ? 1 : 0;
            auto it = images.find(tex);
            if (it != images.end())
                t.image = it->second;
            else {
                std::vector<unsigned char> data;
                if (tex->content.size) {
                    auto *p = (const unsigned char *)tex->content.data;
                    data.assign(p, p + tex->content.size);
                } else {
                    std::string rel(tex->relative_filename.data, tex->relative_filename.length);
                    if (rel.empty())
                        rel = std::filesystem::u8path(std::string(tex->filename.data, tex->filename.length))
                                  .filename()
                                  .string();
                    data = uri_data(rel, dir);
                }
                t.image = image(*a, data, o);
                images[tex] = t.image;
            }
        }
        a->materials.push_back(t);
    }
    a->materials.push_back(Material{});
    std::vector<int> ids(s->nodes.count, -1);
    std::function<int(ufbx_node *)> node = [&](ufbx_node *n) {
        if (ids[n->typed_id] >= 0)
            return ids[n->typed_id];
        int p = n->parent ? node(n->parent) : -1;
        int id = int(a->nodes.size());
        ids[n->typed_id] = id;
        a->nodes.push_back({n->name.data, p, ct(n->local_transform)});
        return id;
    };
    for (auto *n : s->nodes)
        node(n);
    size_t vertices = 0, triangles = 0;
    for (auto *n : s->nodes) {
        auto *m = n->mesh;
        if (!m)
            continue;
        auto *skin = m->skin_deformers.count ? m->skin_deformers.data[0] : nullptr;
        if (skin && skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR &&
            skin->skinning_method != UFBX_SKINNING_METHOD_RIGID)
            warn(*a, "Dual-quaternion skinning approximated by linear blend skinning");
        if (m->blend_deformers.count)
            warn(*a, "Morph targets are not implemented");
        std::unordered_map<int, size_t> parts;
        std::vector<Primitive> out;
        std::vector<std::unordered_map<uint32_t, uint32_t>> vertex_maps;
        for (size_t fi = 0; fi < m->faces.count; fi++) {
            auto face = m->faces.data[fi];
            if (face.num_indices < 3)
                continue;
            int material = int(a->materials.size() - 1);
            if (m->face_material.count) {
                auto mi = m->face_material.data[fi];
                if (mi < n->materials.count)
                    material = int(n->materials.data[mi]->typed_id);
            }
            auto it = parts.find(material);
            size_t part;
            if (it == parts.end()) {
                part = out.size();
                parts[material] = part;
                Primitive p;
                p.node = ids[n->typed_id];
                p.material = material;
                p.geometry = cm(n->geometry_to_node);
                if (skin)
                    for (auto *c : skin->clusters) {
                        p.joints.push_back(ids[c->bone_node->typed_id]);
                        p.inverse_bind.push_back(cm(c->geometry_to_bone));
                    }
                out.push_back(std::move(p));
                vertex_maps.emplace_back();
            } else
                part = it->second;
            auto &p = out[part];
            auto &map = vertex_maps[part];
            std::vector<uint32_t> ix((face.num_indices - 2) * 3);
            uint32_t nt = ufbx_triangulate_face(ix.data(), ix.size(), m, face);
            triangles += nt;
            if (triangles > o.max_triangles)
                throw std::runtime_error("Triangle budget exceeded");
            for (size_t k = 0; k < size_t(nt) * 3; k++) {
                uint32_t vi = ix[k];
                auto found = map.find(vi);
                if (found != map.end()) {
                    p.indices.push_back(found->second);
                    continue;
                }
                if (++vertices > o.max_vertices)
                    throw std::runtime_error("Vertex budget exceeded");
                AssetVertex v;
                v.position = cv(ufbx_get_vertex_vec3(&m->vertex_position, vi));
                v.normal = cv(ufbx_get_vertex_vec3(&m->vertex_normal, vi));
                if (m->vertex_uv.exists) {
                    auto uv = ufbx_get_vertex_vec2(&m->vertex_uv, vi);
                    if (material < int(s->materials.count)) {
                        auto *tex = s->materials.data[material]->pbr.base_color.texture;
                        if (tex) {
                            if (tex->file_textures.count)
                                tex = tex->file_textures.data[0];
                            if (tex->has_uv_transform) {
                                ufbx_vec3 tuv{uv.x, uv.y, 0};
                                tuv = ufbx_transform_position(&tex->uv_to_texture, tuv);
                                uv.x = tuv.x;
                                uv.y = tuv.y;
                            }
                        }
                    }
                    v.uv = {float(uv.x), 1 - float(uv.y)};
                }
                if (m->vertex_color.exists) {
                    auto c = ufbx_get_vertex_vec4(&m->vertex_color, vi);
                    v.color = {float(c.x), float(c.y), float(c.z)};
                }
                if (skin) {
                    auto sv = skin->vertices.data[m->vertex_indices.data[vi]];
                    if (sv.num_weights > 4)
                        warn(*a, "More than four joint weights reduced to strongest four and normalized");
                    float sum = 0;
                    for (uint32_t j = 0; j < std::min(4u, sv.num_weights); j++) {
                        auto w = skin->weights.data[sv.weight_begin + j];
                        v.joints[j] = w.cluster_index;
                        v.weights[j] = float(w.weight);
                        sum += v.weights[j];
                    }
                    if (sum > 0)
                        for (auto &w : v.weights)
                            w /= sum;
                }
                uint32_t id = uint32_t(p.vertices.size());
                map[vi] = id;
                p.vertices.push_back(v);
                p.indices.push_back(id);
            }
        }
        for (auto &p : out)
            a->primitives.push_back(std::move(p));
    }
    for (auto *stack : s->anim_stacks) {
        ufbx_bake_opts bo{};
        bo.trim_start_time = true;
        bo.resample_rate = o.bake_hz;
        bo.maximum_sample_rate = o.bake_hz;
        bo.key_reduction_enabled = true;
        bo.key_reduction_rotation = true;
        std::unique_ptr<ufbx_baked_anim, decltype(&ufbx_free_baked_anim)> b(
            ufbx_bake_anim(s.get(), stack->anim, &bo, &error), ufbx_free_baked_anim);
        if (!b)
            throw std::runtime_error("FBX animation baking failed");
        Clip c;
        c.name = stack->name.data;
        c.duration = float(b->playback_duration);
        for (auto &n : b->nodes) {
            for (int path = 0; path < 3; path++) {
                Track t;
                t.node = ids[n.typed_id];
                t.path = path;
                if (path == 1) {
                    for (auto &k : n.rotation_keys) {
                        Key key;
                        key.time = float(k.time);
                        key.value = {float(k.value.x), float(k.value.y), float(k.value.z), float(k.value.w)};
                        t.keys.push_back(key);
                    }
                } else {
                    auto &list = path == 0 ? n.translation_keys : n.scale_keys;
                    for (auto &k : list) {
                        Key key;
                        key.time = float(k.time);
                        key.value = {float(k.value.x), float(k.value.y), float(k.value.z), 0};
                        t.keys.push_back(key);
                    }
                }
                if (!t.keys.empty())
                    c.tracks.push_back(std::move(t));
            }
        }
        a->clips.push_back(std::move(c));
    }
    if (s->constraints.count)
        warn(*a, "Rig constraints must be baked in the authoring tool; no runtime constraint solver");
    return a;
}
} // namespace
std::shared_ptr<const Asset> import_asset(const std::string &path, const ImportOptions &o) {
    if (!std::isfinite(o.bake_hz) || o.bake_hz < 1 || o.bake_hz > 240 || o.max_texture_size < 1)
        throw std::runtime_error("Invalid import options");
    auto ext = std::filesystem::u8path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".embasset")
        return load_asset(path, o);
    if (ext == ".gltf" || ext == ".glb")
        return gltf(path, o);
    if (ext == ".fbx" || ext == ".obj")
        return fbx(path, o);
    throw std::runtime_error("Supported formats: OBJ, FBX, glTF, GLB, embasset");
}
} // namespace ember
