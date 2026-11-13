#include "ember/collision.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc < 3) {
            std::cout << "ember_cook model.obj collision.embcol [quality 1..100]\n";
            return 0;
        }
        auto m = ember::load_obj(argv[1]);
        ember::CollisionMesh c;
        c.cook(m, argc > 3 ? std::stoi(argv[3]) : 70);
        c.save(argv[2]);
        std::cout << c.triangle_count() << " triangles, BVH cooked\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
