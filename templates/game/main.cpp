#include "ember/application.hpp"
#include "ember/save.hpp"
#include "ember/runtime.hpp"
#include <iostream>
#include <cmath>
#include <memory>
#include <algorithm>
#ifdef GAME_NOVA_HEADER
#include GAME_NOVA_HEADER
#endif
int retrato_game_main(int argc,char **argv){try{
    ember::app::Options options;options.name="Retrato Starter";
    auto dirs=ember::platform::paths(options.organization,options.name);
    options.graphics=ember::Config::load(ember::platform::resource(dirs,"engine.toml").u8string());
    for(int i=1;i<argc;++i){std::string arg=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing value: "+arg);return std::string(argv[i]);};
        if(arg=="--headless")options.headless=true;
        else if(arg=="--uncapped")options.uncapped=true;
        else if(arg=="--frames")options.frames=std::stoi(value());
        else if(arg=="--backend")options.graphics.backend=value();
        else if(arg=="--help"){std::cout<<"--backend auto|vulkan|cpu|opengl --uncapped --headless --frames N\n";return 0;}
        else throw std::runtime_error("Unknown option: "+arg);
    }
    // C++ retains the full native world; portable application code only coordinates it.
    auto world=std::make_shared<ember::Runtime>(options.graphics);
    ember::game::Entity player;float touch_x=0;int64_t finger=-1;bool running=true;
#ifdef GAME_NOVA_HEADER
    std::unique_ptr<nova_generated::Program> script;
#endif
    ember::app::Callbacks callbacks;
    callbacks.start=[&](ember::game::Host &host,const ember::platform::Paths &paths){
        auto floor=host.create_box("floor",{12,.2f,12},{.26f,.34f,.31f});host.set_position(floor,{0,-.1f,0});
        player=host.create_box("player",{1,1,1},{.85f,.36f,.17f});host.set_position(player,{0,.5f,0});host.set_camera({5,4,-7},{0,.5f,0});
#ifdef GAME_NOVA_HEADER
        script=std::make_unique<nova_generated::Program>(host,player);
#endif
#ifdef GAME_USE_LUA
        host.load_script(ember::platform::resource(paths,"events.lua").u8string());
#endif
        ember::SaveData save;save.set("launch",true);save.write((paths.saves/"starter.embsave").u8string());
        std::cout<<"Retrato "<<ember::platform::version()<<" / "<<ember::platform::operating_system()<<"-"<<ember::platform::architecture()<<"\n";
    };
    callbacks.update=[&](ember::game::Host &host,float dt){if(host.key_pressed(27))running=false;auto t=host.transform(player);t.yaw+=dt;
#ifndef GAME_NOVA_HEADER
        float dx=(host.key_down('d')?1.f:0.f)-(host.key_down('a')?1.f:0.f)+touch_x;
        t.position.x=std::clamp(t.position.x+dx*dt*2,-4.f,4.f);
#endif
        host.set_transform(player,t);
    };
    callbacks.touch=[&](ember::game::Host &,int64_t id,int phase,float x,float){
        if(phase==0&&finger<0)finger=id;
        if(id==finger){touch_x=phase==2?0:(x<.5f?-1.f:1.f);if(phase==2)finger=-1;}
    };
    callbacks.suspend=[&](ember::game::Host &){touch_x=0;finger=-1;};
    callbacks.running=[&]{return running;};
    return ember::app::run(options,callbacks,world);
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
