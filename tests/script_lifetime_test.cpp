#include "ember/runtime.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Source path required");
        {
            ember::Config config;
            ember::Runtime runtime(config);
            ember::Script script(runtime, std::string(argv[1]) + "/tests/stale_handle_test.lua");
            script.event({ember::Event::Type::Start, 0, 0});
            if (runtime.scene.objects.size() != 2)
                throw std::runtime_error("stale handle fixture did not create two objects");
            const auto survivor = runtime.scene.objects[1].id;
            runtime.scene.objects.erase(runtime.scene.objects.begin());
            if (runtime.scene.objects[0].id != survivor)
                throw std::runtime_error("object move did not preserve identity");
            bool object_rejected = false, collider_rejected = false;
            try {
                script.event({ember::Event::Type::FixedUpdate, 0, 0});
            } catch (const std::exception &error) {
                object_rejected = std::string(error.what()).find("expired object") != std::string::npos;
            }
            try {
                script.event({ember::Event::Type::Cue, 0, 0});
            } catch (const std::exception &error) {
                collider_rejected = std::string(error.what()).find("expired") != std::string::npos;
            }
            if (!object_rejected || !collider_rejected || runtime.scene.objects[0].position.x != 10)
                throw std::runtime_error("stale Lua handle reached a replacement object");
        }
        {
            ember::Config config;
            ember::Runtime runtime(config);
            ember::Script script(runtime, std::string(argv[1]) + "/tests/input_test.lua");
            script.event({ember::Event::Type::MouseMove, 1.5, 0, 0, 10, 20, 3, -4});
            script.event({ember::Event::Type::MouseDown, 2, 1, 0, 8, 9});
            script.event({ember::Event::Type::MouseWheel, 2.5, 0, 0, -1, 2});
            script.event({ember::Event::Type::GamepadButtonDown, 3, 2, 7});
            script.event({ember::Event::Type::GamepadAxis, 4, 1, 7, -.5f});
        }
        std::unique_ptr<ember::Script> script;
        {
            ember::Config config;
            ember::Runtime runtime(config);
            script = std::make_unique<ember::Script>(runtime, std::string(argv[1]) + "/tests/lifetime_test.lua");
        }
        try {
            script->event({ember::Event::Type::FixedUpdate, 0, 0});
        } catch (const std::exception &error) {
            if (std::string(error.what()).find("Runtime no longer exists") != std::string::npos) {
                std::cout << "PASS: Lua input and references reject replaced objects/colliders and expired Runtime\n";
                return 0;
            }
            throw;
        }
        throw std::runtime_error("Script accepted an expired Runtime");
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
