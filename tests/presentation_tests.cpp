#include "../platform/common/application_internal.hpp"
#include <SDL.h>
#include <iostream>
#include <stdexcept>
int main(int argc,char **argv){try {
    using namespace ember;
    if(argc!=2)throw std::runtime_error("Expected backend name");
    app::Options options;options.graphics.backend=argv[1];options.uncapped=true;
    auto driver=app::detail::make_driver(options);
    auto backend=driver->graphics(options);
    const bool gpu=options.graphics.backend!="cpu";
    if(backend->direct_presentation()!=gpu)throw std::runtime_error("Wrong presentation path");
    Frame frame;frame.width=64;frame.height=36;frame.settings.height=36;frame.settings.profile=int(frame.profile);frame.resources={0};
    for(auto p:{Vec2{-1,-1},Vec2{1,-1},Vec2{0,1}})
        frame.vertices.push_back({p.x,p.y,.5f,1,0,0,1,.2f,.1f,0});
    auto cpu=make_cpu(0);SDL_Window *window=nullptr;SDL_Event event;
    while(SDL_PollEvent(&event))if(event.type==SDL_WINDOWEVENT)if(auto *w=SDL_GetWindowFromID(event.window.windowID))window=w;
    for(int i=0;i<8;++i){
        if(i==3&&window)SDL_SetWindowSize(window,700,500);
        if(i==4){frame.width=80;frame.height=frame.settings.height=45;}
        backend->draw(frame);driver->frame_size(frame.width,frame.height);
        if(!backend->present())driver->present(backend->readback(),frame.width,frame.height);
        const auto captured=backend->readback();cpu->draw(frame);const auto &reference=cpu->readback();
        if(captured.size()!=reference.size())throw std::runtime_error("Invalid capture size");
        double error=0;
        for(size_t p=0;p<captured.size();++p)for(int shift:{0,8,16})error+=std::abs(int((captured[p]>>shift)&255)-int((reference[p]>>shift)&255));
        if(error/(captured.size()*3)>1)throw std::runtime_error("Present corrupted captured frame");
        SDL_PumpEvents();
    }
    std::cout<<"PASS: "<<backend->name()<<" draw, present, capture and resize\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
