#include "ember/asset.hpp"
#include <iostream>
#include <stdexcept>
using namespace ember;
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout << "ember_import model.obj|fbx|gltf|glb|embasset [--out model.embasset] "
                         "[--max-vertices 100000] [--texture-limit 2048] [--bake-hz 30]\n";
            return 0;
        }
        ImportOptions opts;
        std::string output;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("Missing option value");
            std::string value = argv[++i];
            if (arg == "--out")
                output = value;
            else if (arg == "--max-vertices")
                opts.max_vertices = std::stoul(value);
            else if (arg == "--texture-limit")
                opts.max_texture_size = std::stoi(value);
            else if (arg == "--bake-hz")
                opts.bake_hz = std::stof(value);
            else
                throw std::runtime_error("Unknown import option: " + arg);
        }
        auto asset = import_asset(argv[1], opts);
        size_t vertices = 0, triangles = 0, skin = 0, texbytes = 0;
        for (auto &p : asset->primitives) {
            vertices += p.vertices.size();
            triangles += p.indices.size() / 3;
            skin += !p.joints.empty();
        }
        for (auto &i : asset->images)
            texbytes += i->rgba.size() * 4;
        std::cout << "vertices=" << vertices << " triangles=" << triangles << " nodes=" << asset->nodes.size()
                  << " skinned_parts=" << skin << " materials=" << asset->materials.size()
                  << " texture_bytes=" << texbytes << "\n";
        for (size_t i = 0; i < asset->nodes.size(); i++)
            std::cout << "node " << i << " parent=" << asset->nodes[i].parent
                      << " name=" << asset->nodes[i].name << "\n";
        for (auto &c : asset->clips)
            std::cout << "clip \"" << c.name << "\" duration=" << c.duration << " tracks=" << c.tracks.size()
                      << "\n";
        for (auto &w : asset->warnings)
            std::cerr << "Warning: " << w << "\n";
        if (!output.empty()) {
            save_asset(*asset, output);
            std::cout << "Saved " << output << "\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
