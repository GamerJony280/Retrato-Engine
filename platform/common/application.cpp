#include "application_internal.hpp"
#include "ember/loop.hpp"
#include "ember/render.hpp"
#include "ember/runtime.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
namespace ember::app {
int run(const Options &requested,const Callbacks &callbacks,std::shared_ptr<Runtime> native_world) {
    Options o=requested;if(native_world)o.graphics=native_world->config;
    o.graphics.validate();
    if(o.frames<0 || (o.headless&&o.frames==0))throw std::invalid_argument("Headless application requires frames > 0");
    game::Host host=native_world?game::Host(native_world):game::Host(o.graphics);
    auto dirs=platform::paths(o.organization,o.name,o.content);
    // Driver outlives the GPU backend (SDL contexts must close before video).
    std::unique_ptr<detail::Driver> driver;
    std::unique_ptr<Backend> backend;
    if(!o.headless){driver=detail::make_driver(o);backend=driver->graphics(o);std::cout<<"backend="<<backend->name()<<" device="<<backend->device_description()<<" presentation="<<(backend->direct_presentation()?"gpu":"cpu")<<"\n";}
    if(callbacks.start)callbacks.start(host,dirs);
    auto update=host.on_update([&](float dt){if(callbacks.update)callbacks.update(host,dt);});
    SceneRenderer renderer;Frame frame;LoopTimer timer;timer.fixed_dt=o.graphics.fixed_delta();
    using Clock=std::chrono::steady_clock;auto previous=Clock::now();int ticks=0;
    while(o.frames==0||ticks<o.frames){
        if(callbacks.running&&!callbacks.running())break;
        if(o.headless){host.tick(timer.fixed_dt);++ticks;continue;}
        if(!driver->poll(host,callbacks))break;
        auto now=Clock::now();double elapsed=std::chrono::duration<double>(now-previous).count();previous=now;
        if(!driver->active()){timer.acc=timer.render_acc=0;std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
        auto step=timer.update(elapsed);
        for(int i=0;i<step.steps&&(o.frames==0||ticks<o.frames);++i){host.tick(timer.fixed_dt);++ticks;}
        if(o.uncapped||step.render){host.render(renderer,*backend,frame);driver->frame_size(frame.width,frame.height);if(!backend->present())driver->present(backend->readback(),frame.width,frame.height);}
        if(!o.uncapped&&step.sleep_ms>=1)std::this_thread::sleep_for(std::chrono::milliseconds(int(step.sleep_ms)));
    }
    return 0;
}
}
