#include "context.hpp"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <exception>
int retrato_game_main(int,char**);
extern "C" void android_main(android_app *app){
    app_dummy();ember::platform::detail::set_android_context(app);
    char name[]="game";char *args[]={name,nullptr};
    try{if(retrato_game_main(1,args)!=0)__android_log_print(ANDROID_LOG_ERROR,"Retrato","Game startup failed; inspect configuration/assets");}catch(const std::exception &e){__android_log_print(ANDROID_LOG_ERROR,"Retrato","%s",e.what());}
    if(!app->destroyRequested)ANativeActivity_finish(app->activity);
    // Keep glue alive until Java destroys the Activity; never leave callbacks
    // pointing at a finished game or return before lifecycle synchronization.
    while(!app->destroyRequested){int events;android_poll_source *source=nullptr;int id=ALooper_pollOnce(-1,nullptr,&events,reinterpret_cast<void**>(&source));if(id>=0&&source)source->process(app,source);}
    ember::platform::detail::set_android_context(nullptr);
}
