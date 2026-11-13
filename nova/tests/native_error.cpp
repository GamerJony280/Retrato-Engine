#include "ember/config.hpp"
#include NOVA_MODULE_HEADER
#include <iostream>
int main() {
    try {
        ember::Config c;
        ember::game::Host host(c);
        nova_generated::Program program(host, host.create("player"));
    } catch (const nova::Fault &e) {
        std::string message = e.what();
        std::cout << message << '\n';
        return message.find(".nova:2:") != std::string::npos &&
                       message.find(NOVA_EXPECT_ERROR) != std::string::npos
                   ? 0
                   : 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 1;
}
