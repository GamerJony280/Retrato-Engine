#include "ember/render.hpp"
#include "ember/runtime.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace ember;
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){try {
    Frame f;f.width=f.height=32;f.profile=Profile::PS1;
    f.settings.profile=int(f.profile);f.settings.height=f.height;
    auto triangle=[&](float z,Vec3 color){
        for(auto p:{Vec2{-1,-1},Vec2{1,-1},Vec2{0,1}})
            f.vertices.push_back({p.x,p.y,z,1,0,0,color.x,color.y,color.z,0});
    };
    triangle(.2f,{1,0,0});triangle(.8f,{0,1,0});
    auto cpu=make_cpu(0);
    std::unique_ptr<Backend> backend;
    if(const char *name=std::getenv("EMBER_TEST_BACKEND"))backend=make_backend(name,0);
    for(auto mode:{DepthMode::Automatic,DepthMode::Enabled,DepthMode::Disabled}) {
        f.depth_mode=mode;cpu->draw(f);auto pixels=cpu->readback();
        auto center=pixels[16*32+16];
        auto expected=finish_pixel(mode==DepthMode::Enabled?Vec3{1,0,0}:Vec3{0,1,0},16,16,f.profile,false);
        require(center==expected,"PS1 painter/depth semantics");
        if(backend){
            backend->draw(f);const auto &actual=backend->readback();
            require(actual[16*32+16]==center,"GPU depth semantics differ from CPU");
            require(!backend->direct_presentation()&&!backend->present(),"Offscreen backend must not present");
        }
    }
    Config cfg;cfg.profile=Profile::PS1;cfg.ps1_depth_test=true;
    SceneRenderer prepared;Frame output;prepared.prepare(Scene{},Camera{},cfg,output);
    require(output.depth_mode==DepthMode::Enabled,"Config depth mode lost during preparation");
    cfg.ps1_depth_test=false;prepared.prepare(Scene{},Camera{},cfg,output);
    require(output.depth_mode==DepthMode::Automatic,"Classic depth default changed");
    f.depth_mode=DepthMode(999);bool rejected=false;
    try{cpu->draw(f);}catch(const std::exception &){rejected=true;}
    require(rejected,"Invalid depth mode must be rejected");
    Runtime runtime(Config{});std::vector<int> cues;
    auto subscription=runtime.observe([&](Runtime &,const Event &e){
        if(e.type!=Event::Type::Cue)return;
        cues.push_back(e.code);if(e.code==1)runtime.add_cue(runtime.time(),3);
    });
    runtime.add_cue(0,1);runtime.add_cue(0,2);runtime.tick(runtime.config.fixed_delta());
    require(cues==std::vector<int>({1,2,3}),"Cue order and insertion during dispatch");
    for(int i=0;i<1000;++i){runtime.add_cue(runtime.time(),4);runtime.tick(runtime.config.fixed_delta());require(runtime.pending_cues()==0,"Consumed cues retained");}
    std::cout<<"PASS: frame depth contract, painter order, capture, cue lifecycle\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
