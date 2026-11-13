#include "ember/config.hpp"

#define NOVA_PROGRAM_NAME FirstProgram
#include NOVA_MODULE_HEADER
#undef NOVA_PROGRAM_NAME

#define NOVA_PROGRAM_NAME SecondProgram
#include NOVA_MODULE_HEADER
#undef NOVA_PROGRAM_NAME

int main() {
    ember::Config config;
    config.backend = "cpu";
    config.worker_threads = 0;
    ember::game::Host host(config);
    auto first = host.create("first"), second = host.create("second");
    nova_generated::FirstProgram a(host, first);
    nova_generated::SecondProgram b(host, second);
    return 0;
}
