#include "ember/platform.hpp"
#include "context.hpp"
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace ember::platform {
namespace detail {
thread_local android_app *context=nullptr;
android_app *android_context(){if(!context)throw std::runtime_error("Android NativeActivity context is not initialized");return context;}
void set_android_context(android_app *p){context=p;}
std::filesystem::path executable_directory(){return std::filesystem::u8path(android_context()->activity->internalDataPath);}
}
const char *operating_system() noexcept{return "android";}
Paths paths(const std::string &org,const std::string &app,const std::filesystem::path &root){
    detail::validate_component(org);detail::validate_component(app);
    auto base=std::filesystem::u8path(detail::android_context()->activity->internalDataPath);
    Paths p{root,base/"saves",base/"resource-cache"};
    std::filesystem::create_directories(p.saves);std::filesystem::create_directories(p.cache);return p;
}
std::filesystem::path resource(const Paths &p,const std::filesystem::path &relative){
    detail::validate_relative(relative);if(!p.content.empty())return p.content/relative;
    auto name=relative.generic_u8string();
    std::unique_ptr<AAsset,decltype(&AAsset_close)> asset(AAssetManager_open(detail::android_context()->activity->assetManager,name.c_str(),AASSET_MODE_STREAMING),AAsset_close);
    if(!asset)throw std::runtime_error("APK asset not found: "+name);
    auto outpath=p.cache/relative;auto temporary=outpath;temporary+=".tmp";std::filesystem::create_directories(outpath.parent_path());
    try{
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot materialize APK asset");
        char chunk[16384];int n;while((n=AAsset_read(asset.get(),chunk,sizeof(chunk)))>0)if(!out.write(chunk,n))throw std::runtime_error("Cannot write APK asset cache");
        if(n<0||!out.flush())throw std::runtime_error("Cannot read APK asset");out.close();std::filesystem::rename(temporary,outpath);
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
    return outpath;
}
}
