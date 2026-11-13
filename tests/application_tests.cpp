#include "ember/application.hpp"
#include "ember/runtime.hpp"
#include <stdexcept>
#include <iostream>
int main(){try{
    ember::app::Options options;options.headless=true;options.frames=3;options.graphics.backend="cpu";
    int starts=0,updates=0;ember::game::Entity e;
    ember::app::Callbacks callbacks;
    callbacks.start=[&](ember::game::Host &h,const ember::platform::Paths &p){++starts;e=h.create("player");if(!p.saves.is_absolute())throw std::runtime_error("Bad save path");};
    callbacks.update=[&](ember::game::Host &h,float dt){++updates;auto p=h.position(e);p.x+=dt;h.set_position(e,p);};
    ember::app::run(options,callbacks);
    if(starts!=1||updates!=3)throw std::runtime_error("Wrong application callback count");
    starts=updates=0;callbacks.running=[&]{return updates<2;};ember::app::run(options,callbacks);
    if(starts!=1||updates!=2)throw std::runtime_error("Game-controlled exit failed");
    starts=updates=0;callbacks.running={};auto world=std::make_shared<ember::Runtime>(options.graphics);
    auto native=world->add_object(ember::Object(ember::box({1,1,1},{1,1,1})));
    ember::app::run(options,callbacks,world);
    if(!world->object_alive(native)||world->time()<.049||updates!=3)throw std::runtime_error("Application did not preserve the supplied native world");
    options.frames=0;bool rejected=false;try{ember::app::run(options,callbacks);}catch(const std::invalid_argument&){rejected=true;}
    if(!rejected)throw std::runtime_error("Unbounded headless loop accepted");
    std::cout<<"PASS: portable application callbacks, exit policy, headless bounds\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
