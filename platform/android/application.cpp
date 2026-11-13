#include "../common/application_internal.hpp"
#include "../common/application_state.hpp"
#include "context.hpp"
#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/native_window.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
namespace ember::app::detail {
class AndroidDriver final:public Driver {
    android_app *app_;ActivityState state_{false,false,true};game::Host *host_=nullptr;const Callbacks *callbacks_=nullptr;
    std::exception_ptr error_;int input_w_,input_h_;int32_t mouse_buttons_=0;
    static AndroidDriver &self(android_app *p){return *static_cast<AndroidDriver*>(p->userData);}
    static void command(android_app *app,int32_t code){auto &s=self(app);try{
        if(code==APP_CMD_RESUME)s.state_.resumed=true;
        if(code==APP_CMD_PAUSE||code==APP_CMD_STOP)s.state_.resumed=false;
        if(code==APP_CMD_LOST_FOCUS||code==APP_CMD_TERM_WINDOW)s.state_.focused=false;
        if(code==APP_CMD_GAINED_FOCUS)s.state_.focused=true;
        if(code==APP_CMD_PAUSE||code==APP_CMD_STOP||code==APP_CMD_LOST_FOCUS||code==APP_CMD_TERM_WINDOW){
            s.mouse_buttons_=0;
            if(s.host_){s.host_->reset_input();if(s.callbacks_&&s.callbacks_->suspend)s.callbacks_->suspend(*s.host_);}
        }
    }catch(...){s.error_=std::current_exception();}}
    static int key(int32_t k){
        if(k>=AKEYCODE_A&&k<=AKEYCODE_Z)return 'a'+k-AKEYCODE_A;
        if(k>=AKEYCODE_0&&k<=AKEYCODE_9)return '0'+k-AKEYCODE_0;
        switch(k){case AKEYCODE_SPACE:return 32;case AKEYCODE_ENTER:return 13;case AKEYCODE_ESCAPE:case AKEYCODE_BACK:return 27;
        case AKEYCODE_DPAD_LEFT:return 1073741904;case AKEYCODE_DPAD_RIGHT:return 1073741903;case AKEYCODE_DPAD_UP:return 1073741906;case AKEYCODE_DPAD_DOWN:return 1073741905;
        case AKEYCODE_SHIFT_LEFT:return 1073742049;case AKEYCODE_CTRL_LEFT:return 1073742048;default:return 0;}
    }
    static int32_t input(android_app *app,AInputEvent *event){auto &s=self(app);try{if(!s.host_)return 0;
        int source=AInputEvent_getSource(event),device=AInputEvent_getDeviceId(event);
        using Type=game::InputEvent::Type;
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY&&AKeyEvent_getKeyCode(event)>=AKEYCODE_BUTTON_A&&AKeyEvent_getKeyCode(event)<=AKEYCODE_BUTTON_MODE){
            int k=AKeyEvent_getKeyCode(event),button=-1;
            switch(k){case AKEYCODE_BUTTON_A:button=0;break;case AKEYCODE_BUTTON_B:button=1;break;case AKEYCODE_BUTTON_X:button=2;break;case AKEYCODE_BUTTON_Y:button=3;break;case AKEYCODE_BUTTON_SELECT:button=4;break;case AKEYCODE_BUTTON_MODE:button=5;break;case AKEYCODE_BUTTON_START:button=6;break;case AKEYCODE_BUTTON_THUMBL:button=7;break;case AKEYCODE_BUTTON_THUMBR:button=8;break;case AKEYCODE_BUTTON_L1:button=9;break;case AKEYCODE_BUTTON_R1:button=10;break;}
            if(button>=0){int action=AKeyEvent_getAction(event);s.host_->dispatch_input({action==AKEY_EVENT_ACTION_UP?Type::GamepadButtonUp:Type::GamepadButtonDown,s.host_->time(),button,device});return 1;}
        }
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY){int k=key(AKeyEvent_getKeyCode(event));if(!k)return 0;auto action=AKeyEvent_getAction(event);if(action!=AKEY_EVENT_ACTION_DOWN&&action!=AKEY_EVENT_ACTION_UP)return 0;
            s.host_->dispatch_input({action==AKEY_EVENT_ACTION_DOWN?game::InputEvent::Type::KeyDown:game::InputEvent::Type::KeyUp,s.host_->time(),k});return 1;}
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION&&(source&AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK){
            const int axes[]={AMOTION_EVENT_AXIS_X,AMOTION_EVENT_AXIS_Y,AMOTION_EVENT_AXIS_Z,AMOTION_EVENT_AXIS_RZ,AMOTION_EVENT_AXIS_LTRIGGER,AMOTION_EVENT_AXIS_RTRIGGER};
            for(int i=0;i<6;++i)s.host_->dispatch_input({Type::GamepadAxis,s.host_->time(),i,device,std::clamp(AMotionEvent_getAxisValue(event,axes[i],0),-1.f,1.f)});return 1;
        }
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION&&(source&AINPUT_SOURCE_MOUSE)==AINPUT_SOURCE_MOUSE&&app->window){
            int w=ANativeWindow_getWidth(app->window),h=ANativeWindow_getHeight(app->window);if(w<=0||h<=0)return 0;
            float x=AMotionEvent_getX(event,0)*s.input_w_/w,y=AMotionEvent_getY(event,0)*s.input_h_/h;auto old=s.host_->mouse_position();
            s.host_->dispatch_input({Type::MouseMove,s.host_->time(),0,device,x,y,x-old.x,y-old.y});
            int32_t buttons=AMotionEvent_getButtonState(event);const int masks[]={AMOTION_EVENT_BUTTON_PRIMARY,AMOTION_EVENT_BUTTON_TERTIARY,AMOTION_EVENT_BUTTON_SECONDARY};
            for(int i=0;i<3;++i)if((buttons&masks[i])!=(s.mouse_buttons_&masks[i]))s.host_->dispatch_input({buttons&masks[i]?Type::MouseDown:Type::MouseUp,s.host_->time(),i+1,device,x,y});s.mouse_buttons_=buttons;
            if((AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK)==AMOTION_EVENT_ACTION_SCROLL)s.host_->dispatch_input({Type::MouseWheel,s.host_->time(),0,device,AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HSCROLL,0),AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_VSCROLL,0)});return 1;
        }
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION&&s.callbacks_&&s.callbacks_->touch&&app->window){
            int action=AMotionEvent_getAction(event),kind=action&AMOTION_EVENT_ACTION_MASK;size_t index=size_t((action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            if(kind!=AMOTION_EVENT_ACTION_DOWN&&kind!=AMOTION_EVENT_ACTION_POINTER_DOWN&&kind!=AMOTION_EVENT_ACTION_MOVE&&kind!=AMOTION_EVENT_ACTION_UP&&kind!=AMOTION_EVENT_ACTION_POINTER_UP&&kind!=AMOTION_EVENT_ACTION_CANCEL)return 0;
            int w=ANativeWindow_getWidth(app->window),h=ANativeWindow_getHeight(app->window);if(w<=0||h<=0)return 0;
            for(size_t i=0;i<AMotionEvent_getPointerCount(event);++i){
                if(kind!=AMOTION_EVENT_ACTION_MOVE&&kind!=AMOTION_EVENT_ACTION_CANCEL&&i!=index)continue;
                int phase=kind==AMOTION_EVENT_ACTION_MOVE?1:((kind==AMOTION_EVENT_ACTION_UP||kind==AMOTION_EVENT_ACTION_POINTER_UP||kind==AMOTION_EVENT_ACTION_CANCEL)?2:0);
                s.callbacks_->touch(*s.host_,AMotionEvent_getPointerId(event,i),phase,AMotionEvent_getX(event,i)/w,AMotionEvent_getY(event,i)/h);
            }return 1;
        }return 0;}catch(...){s.error_=std::current_exception();return 1;}
    }
public:
    explicit AndroidDriver(const Options &o):app_(platform::detail::android_context()),input_w_(o.graphics.width()),input_h_(o.graphics.height()) {app_->userData=this;app_->onAppCmd=command;app_->onInputEvent=input;}
    ~AndroidDriver() override {app_->onAppCmd=nullptr;app_->onInputEvent=nullptr;app_->userData=nullptr;}
    bool active() const override{return state_.active()&&app_->window;}
    bool poll(game::Host &host,const Callbacks &callbacks) override {host_=&host;callbacks_=&callbacks;
        int events;android_poll_source *source=nullptr;
        int id;while((id=ALooper_pollOnce(0,nullptr,&events,reinterpret_cast<void**>(&source)))>=0){if(source)source->process(app_,source);if(app_->destroyRequested)return false;}
        if(error_)std::rethrow_exception(error_);
        return !app_->destroyRequested;
    }
    void present(const std::vector<uint32_t> &pixels,int w,int h) override {
        if(!app_->window)return;ANativeWindow_setBuffersGeometry(app_->window,0,0,WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_Buffer buffer{};if(ANativeWindow_lock(app_->window,&buffer,nullptr))return;
        for(int y=0;y<buffer.height;++y){auto *dst=static_cast<uint32_t*>(buffer.bits)+size_t(y)*buffer.stride;const auto *src=pixels.data()+size_t(y)*h/buffer.height*w;
            for(int x=0;x<buffer.width;++x)dst[x]=src[size_t(x)*w/buffer.width];}
        ANativeWindow_unlockAndPost(app_->window);
    }
};
std::unique_ptr<Driver> make_driver(const Options &o){return std::make_unique<AndroidDriver>(o);}
}
