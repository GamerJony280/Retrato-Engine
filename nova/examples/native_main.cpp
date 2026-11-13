#include "ember/config.hpp"
#include "ember/render.hpp"
#include NOVA_MODULE_HEADER
#include <cmath>
#include <iostream>
int main(int argc, char **argv) {
    try {
        ember::Config config;
        config.backend = "cpu";
        config.worker_threads = 0;
        ember::game::Host host(config);
        auto object = host.create("player");
        {
            nova_generated::Program program(host, object);
            for (int i = 0; i < 60; ++i)
                host.tick(1.0 / 60);
        } // disconnect scripts; host keeps its scene
        auto final = host.position(object);
        std::cout << "position=(" << final.x << "," << final.y << "," << final.z << ")\n";
        if (std::abs(final.x - NOVA_EXPECT_X) > 0.0001)
            throw std::runtime_error("Unexpected native position");
#ifdef NOVA_CHECK_ROBOT
        if (std::abs(host.position(host.entity("robot")).x - 1.f) > 0.0001f)
            throw std::runtime_error("Nova did not move the imported robot");
#endif
        host.tick(1.0 / 60);
        if (host.position(object).x != final.x)
            throw std::runtime_error("Script callback survived destruction");
        if (argc > 1) {
            ember::SceneRenderer renderer;
            ember::Frame frame;
            auto backend = ember::make_cpu(0);
            host.render(renderer, *backend, frame);
            ember::write_ppm(argv[1], frame.width, frame.height, backend->readback());
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
