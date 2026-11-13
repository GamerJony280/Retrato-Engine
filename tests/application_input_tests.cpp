#include "../platform/common/application_internal.hpp"
#include <SDL.h>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void window(Uint8 kind){SDL_Event e{};e.type=SDL_WINDOWEVENT;e.window.event=kind;require(SDL_PushEvent(&e)==1,"Cannot queue window event");}
void app(Uint32 kind){SDL_Event e{};e.type=kind;require(SDL_PushEvent(&e)==1,"Cannot queue activity event");}
}
int main(){try{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
    ember::app::Options options;options.graphics.backend="cpu";
    auto driver=ember::app::detail::make_driver(options);
    ember::game::Host host(options.graphics);ember::app::Callbacks callbacks;
    int suspended=0;callbacks.suspend=[&](ember::game::Host &){++suspended;};
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
    auto poll=[&]{require(driver->poll(host,callbacks),"Unexpected close event");};
    window(SDL_WINDOWEVENT_FOCUS_GAINED);poll();require(driver->active(),"Initial focus");
    host.dispatch_input({ember::game::InputEvent::Type::KeyDown,0,'a'});
    window(SDL_WINDOWEVENT_FOCUS_LOST);poll();
    require(!host.key_down('a')&&suspended==1,"Focus loss must release input");
    window(SDL_WINDOWEVENT_MINIMIZED);window(SDL_WINDOWEVENT_RESTORED);poll();
    require(!driver->active(),"Restoring a window must not restore focus");
    window(SDL_WINDOWEVENT_FOCUS_GAINED);poll();require(driver->active(),"Focus gain");
    app(SDL_APP_WILLENTERBACKGROUND);poll();require(!driver->active(),"Background pause");
    window(SDL_WINDOWEVENT_FOCUS_GAINED);poll();
    require(!driver->active(),"Focus gain must not undo background pause");
    app(SDL_APP_DIDENTERFOREGROUND);poll();require(driver->active(),"Foreground plus focus");
    SDL_Event mouse{};mouse.type=SDL_MOUSEMOTION;mouse.motion.x=480;mouse.motion.y=270;
    require(SDL_PushEvent(&mouse)==1,"Cannot queue mouse event");poll();
    auto point=host.mouse_position();
    require(std::abs(point.x-options.graphics.width()/2.f)<1&&std::abs(point.y-options.graphics.height()/2.f)<1,"Logical mouse coordinates");
    std::cout<<"PASS: SDL focus, minimize/restore, pause, input release\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<"\n";return 1;}}
