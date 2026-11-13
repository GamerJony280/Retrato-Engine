#include "application_internal.hpp"
#include "application_state.hpp"
#include "window_graphics.hpp"
#ifdef EMBER_VULKAN
#include "../../src/render/vulkan_window.hpp"
#include <SDL_vulkan.h>
#endif
#include <iostream>
#include <SDL.h>
#include <stdexcept>
#include <vector>
namespace ember::app::detail {
class SdlDriver final:public Driver {
    SDL_Window *window_=nullptr;SDL_Renderer *renderer_=nullptr;SDL_Texture *texture_=nullptr;
    std::vector<SDL_GameController*> controllers_;
    std::string title_;
    int logical_width_=1,logical_height_=1;
    SDL_Rect view() const {
        int w=0,h=0;SDL_GetWindowSize(window_,&w,&h);
        double scale=std::min(double(w)/logical_width_,double(h)/logical_height_);
        int dw=std::max(1,int(logical_width_*scale)),dh=std::max(1,int(logical_height_*scale));
        return {(w-dw)/2,(h-dh)/2,dw,dh};
    }
    Vec2 logical(float x,float y) const {
        auto v=view();return {(x-v.x)*logical_width_/v.w,(y-v.y)*logical_height_/v.h};
    }
    void window(Uint32 graphics_flags) {
        SDL_DestroyTexture(texture_);texture_=nullptr;
        SDL_DestroyRenderer(renderer_);renderer_=nullptr;width_=height_=0;
        SDL_DestroyWindow(window_);window_=nullptr;
        window_=SDL_CreateWindow(title_.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,540,
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|graphics_flags);
        if(!window_)throw std::runtime_error(SDL_GetError());
    }
    ActivityState state_;bool initialized_=false;int width_=0,height_=0;
    static constexpr Uint32 subsystems=SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER;
    void cleanup() noexcept {
        for(auto *c:controllers_)SDL_GameControllerClose(c);
        SDL_DestroyTexture(texture_);SDL_DestroyRenderer(renderer_);SDL_DestroyWindow(window_);
        if(initialized_)SDL_QuitSubSystem(subsystems);
    }
public:
    explicit SdlDriver(const Options &o):title_(o.name),logical_width_(o.graphics.width()),logical_height_(o.graphics.height()){
        SDL_SetMainReady();
        if(SDL_InitSubSystem(subsystems))throw std::runtime_error(SDL_GetError());
        initialized_=true;
        try{
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,o.graphics.pixel_upscale?"0":"1");
            SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
            window(0);
            for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i))if(auto *c=SDL_GameControllerOpen(i))controllers_.push_back(c);
        }catch(...){cleanup();throw;}
    }
    ~SdlDriver() override {cleanup();}
    std::unique_ptr<Backend> graphics(const Options &o) override {
        const auto &requested=o.graphics.backend;
        if(requested=="auto"||requested=="vulkan") {
            try {
#ifdef EMBER_VULKAN
                window(SDL_WINDOW_VULKAN);
                ember::detail::VulkanWindow target;unsigned count=0;
                if(!SDL_Vulkan_GetInstanceExtensions(window_,&count,nullptr))throw std::runtime_error(SDL_GetError());
                target.extensions.resize(count);
                if(!SDL_Vulkan_GetInstanceExtensions(window_,&count,target.extensions.data()))throw std::runtime_error(SDL_GetError());
                target.vsync=!o.uncapped;target.nearest=o.graphics.pixel_upscale;
                target.create=[this](VkInstance instance,PFN_vkGetInstanceProcAddr get_proc){
                    if(reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())!=get_proc)
                        throw std::runtime_error("SDL and renderer use different Vulkan loaders");
                    VkSurfaceKHR surface=VK_NULL_HANDLE;
                    if(!SDL_Vulkan_CreateSurface(window_,instance,&surface))throw std::runtime_error(SDL_GetError());
                    return surface;
                };
                target.size=[this]{int w=0,h=0;SDL_Vulkan_GetDrawableSize(window_,&w,&h);return std::make_pair(w,h);};
                return ember::detail::make_vulkan_window(std::move(target));
#else
                throw std::runtime_error("Vulkan backend was not compiled");
#endif
            } catch(const std::exception &e) {
                if(requested!="auto")throw;
                std::cerr<<"Vulkan unavailable: "<<e.what()<<"\n";
            }
        }
        if(requested=="auto"||requested=="opengl") {
            try {
#ifdef EMBER_OPENGL
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
                window(SDL_WINDOW_OPENGL);
                return ember::detail::make_opengl_window(window_,!o.uncapped,o.graphics.pixel_upscale);
#else
                throw std::runtime_error("OpenGL backend was not compiled");
#endif
            } catch(const std::exception &e) {
                if(requested!="auto")throw;
                std::cerr<<"OpenGL unavailable: "<<e.what()<<"\n";
            }
        }
        if(requested!="auto"&&requested!="cpu")throw std::runtime_error("Unknown graphics backend");
        window(0);return make_cpu(o.graphics.worker_threads);
    }
    void frame_size(int w,int h) override {logical_width_=w;logical_height_=h;}
    bool active() const override{return state_.active();}
    bool poll(game::Host &host,const Callbacks &callbacks) override {
        using Type=game::InputEvent::Type;SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT)return false;
            if(e.type==SDL_WINDOWEVENT){
                if(e.window.windowID && e.window.windowID!=SDL_GetWindowID(window_))continue;
                if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)state_.focused=false;
                if(e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)state_.focused=true;
                if(e.window.event==SDL_WINDOWEVENT_MINIMIZED)state_.visible=false;
                if(e.window.event==SDL_WINDOWEVENT_RESTORED)state_.visible=true;
                if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST||e.window.event==SDL_WINDOWEVENT_MINIMIZED){host.reset_input();if(callbacks.suspend)callbacks.suspend(host);}
            }
            if(e.type==SDL_APP_WILLENTERBACKGROUND){state_.resumed=false;host.reset_input();if(callbacks.suspend)callbacks.suspend(host);}
            if(e.type==SDL_APP_DIDENTERFOREGROUND)state_.resumed=true;
            if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP)host.dispatch_input({e.type==SDL_KEYDOWN?Type::KeyDown:Type::KeyUp,host.time(),e.key.keysym.sym});
            if(e.type==SDL_MOUSEMOTION){
                auto p=logical(float(e.motion.x),float(e.motion.y));auto v=view();
                host.dispatch_input({Type::MouseMove,host.time(),0,0,p.x,p.y,
                    float(e.motion.xrel)*logical_width_/v.w,float(e.motion.yrel)*logical_height_/v.h});
            }
            if(e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP){
                auto p=logical(float(e.button.x),float(e.button.y));
                host.dispatch_input({e.type==SDL_MOUSEBUTTONDOWN?Type::MouseDown:Type::MouseUp,host.time(),e.button.button,0,p.x,p.y});
            }
            if(e.type==SDL_MOUSEWHEEL)host.dispatch_input({Type::MouseWheel,host.time(),0,0,float(e.wheel.x)*(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1),float(e.wheel.y)*(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1:1)});
            if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP)host.dispatch_input({e.type==SDL_CONTROLLERBUTTONDOWN?Type::GamepadButtonDown:Type::GamepadButtonUp,host.time(),e.cbutton.button,e.cbutton.which});
            if(e.type==SDL_CONTROLLERAXISMOTION)host.dispatch_input({Type::GamepadAxis,host.time(),e.caxis.axis,e.caxis.which,std::max(-1.f,e.caxis.value/32767.f)});
            if(e.type==SDL_CONTROLLERDEVICEADDED&&SDL_IsGameController(e.cdevice.which))if(auto *c=SDL_GameControllerOpen(e.cdevice.which))controllers_.push_back(c);
            if(e.type==SDL_CONTROLLERDEVICEREMOVED){for(auto it=controllers_.begin();it!=controllers_.end();)if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it))==e.cdevice.which){SDL_GameControllerClose(*it);it=controllers_.erase(it);}else ++it;host.reset_input();}
            if((e.type==SDL_FINGERDOWN||e.type==SDL_FINGERMOTION||e.type==SDL_FINGERUP)&&callbacks.touch){
                int w=0,h=0;SDL_GetWindowSize(window_,&w,&h);auto p=logical(e.tfinger.x*w,e.tfinger.y*h);
                callbacks.touch(host,e.tfinger.fingerId,e.type==SDL_FINGERDOWN?0:e.type==SDL_FINGERUP?2:1,p.x/logical_width_,p.y/logical_height_);
            }
        }
        return true;
    }
    void present(const std::vector<uint32_t> &pixels,int w,int h) override {
        frame_size(w,h);
        if(!renderer_){renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);if(!renderer_)throw std::runtime_error(SDL_GetError());}
        if(w!=width_||h!=height_){SDL_DestroyTexture(texture_);texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,w,h);if(!texture_)throw std::runtime_error(SDL_GetError());width_=w;height_=h;}
        if(SDL_UpdateTexture(texture_,nullptr,pixels.data(),w*4))throw std::runtime_error(SDL_GetError());
        int output_w=0,output_h=0;SDL_GetRendererOutputSize(renderer_,&output_w,&output_h);
        double scale=std::min(double(output_w)/w,double(output_h)/h);
        SDL_Rect destination{0,0,std::max(1,int(w*scale)),std::max(1,int(h*scale))};
        destination.x=(output_w-destination.w)/2;destination.y=(output_h-destination.h)/2;
        SDL_SetRenderDrawColor(renderer_,0,0,0,255);SDL_RenderClear(renderer_);
        if(SDL_RenderCopy(renderer_,texture_,nullptr,&destination))throw std::runtime_error(SDL_GetError());
        SDL_RenderPresent(renderer_);
    }
};
std::unique_ptr<Driver> make_driver(const Options &o){return std::make_unique<SdlDriver>(o);}
}
