#include "ember/runtime.hpp"
#include NOVA_MODULE_HEADER
#include <cmath>
#include <iostream>
int main(int argc,char **argv) {try {
    if(argc!=2) throw std::runtime_error("Lua fixture path required");
    ember::Config c;c.backend="cpu";c.worker_threads=0;
    auto world=std::make_shared<ember::Runtime>(c);
    ember::game::Host host(world);
    host.load_script(argv[1]);
    auto lua_ref=world->object_ref(0);
    auto actor=host.create("nova");nova_generated::Program program(host,actor);
    auto native_actor=world->add_object(ember::Object(ember::box({1,1,1},{1,1,1})));
    int ticks=0;world->on_event=[&](ember::Runtime &r,const ember::Event&e){if(e.type==ember::Event::Type::FixedUpdate){r.object(native_actor).position.x+=1./60;++ticks;}};
    for(int i=0;i<60;++i) host.tick(1./60);
    if(ticks!=60||std::abs(world->object(lua_ref).position.x-3)>.0001||std::abs(host.position(actor).x-2)>.0001||std::abs(world->object(native_actor).position.x-1)>.0001)
        throw std::runtime_error("C++/Lua/Nova did not coexist in the same world");
    std::cout<<"C++ + Lua + Nova share one runtime: three actors moved\n";return 0;
} catch(const std::exception&e) {std::cerr<<e.what()<<"\n";return 1;} }
