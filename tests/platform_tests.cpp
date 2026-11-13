#include "ember/platform.hpp"
#include "ember/config.hpp"
#include "ember/save.hpp"
#include "ember/asset.hpp"
#include "../platform/common/application_state.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
void require(bool b,const char *msg){if(!b)throw std::runtime_error(msg);}
int main(){std::filesystem::path root;try{
    ember::app::detail::ActivityState activity;
    activity.focused=false;activity.visible=false;activity.visible=true;
    require(!activity.active(),"Window restore must not restore lost focus");
    activity.resumed=false;activity.focused=true;
    require(!activity.active(),"Focus gain must not resume a paused application");
    activity.resumed=true;require(activity.active(),"Resume plus focus reactivates");
    activity.focused=false;activity.resumed=false;activity.resumed=true;
    require(!activity.active(),"Resume must wait for focus");
    root=std::filesystem::temp_directory_path()/std::filesystem::u8path(std::string(u8"Retrato prueba-ñ-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    auto p=ember::platform::paths("RetratoTests","Portable",root);
    require(p.content==std::filesystem::absolute(root),"Explicit content directory wins");
    require(p.saves.is_absolute()&&p.cache.is_absolute(),"User directories must be absolute");
    require(ember::platform::resource(p,"assets/robot.glb")==p.content/"assets/robot.glb","Resource path");
    bool bad=false;try{ember::platform::resource(p,"../escape");}catch(const std::invalid_argument&){bad=true;}require(bad,"Reject resource traversal");
    bad=false;try{ember::platform::paths("../escape","Game",root);}catch(const std::invalid_argument&){bad=true;}require(bad,"Reject unsafe application name");
    auto file=root/std::filesystem::u8path(u8"partida-árbol.embsave");ember::SaveData data;data.set("value",int64_t(42));data.write(file.u8string());require(ember::SaveData::read(file.u8string()).integer("value")==42,"UTF-8 save path roundtrip");
    auto config=root/std::filesystem::u8path(u8"configuración.toml");{std::ofstream out(config);out<<"[graphics]\nbackend=\"cpu\"\nps1_depth_test=true\n";}
    auto settings=ember::Config::load(config.u8string());
    require(settings.backend=="cpu"&&settings.ps1_depth_test,"UTF-8 config path and PS1 depth option");
    auto asset=root/std::filesystem::u8path(u8"modelo-árbol.embasset");
    std::filesystem::copy_file(std::filesystem::u8path(RETRATO_TEST_ROOT)/"assets/robot.embasset",asset);
    auto loaded=ember::load_asset(asset.u8string());require(!loaded->nodes.empty(),"UTF-8 cooked asset");
    ember::save_asset(*loaded,asset.u8string());require(!ember::load_asset(asset.u8string())->nodes.empty(),"UTF-8 atomic asset replacement");
#ifdef RETRATO_TEST_IMPORTERS
    auto glb=root/std::filesystem::u8path(u8"robot-árbol.glb");std::filesystem::copy_file(std::filesystem::u8path(RETRATO_TEST_ROOT)/"assets/robot.glb",glb);
    require(!ember::import_asset(glb.u8string())->nodes.empty(),"UTF-8 glTF importer callback");
#endif
    require(std::string(ember::platform::architecture())!="generic","Recognized architecture");
    std::cout<<ember::platform::version()<<" "<<ember::platform::operating_system()<<" "<<ember::platform::architecture()<<"\n";
    std::filesystem::remove_all(root);return 0;
}catch(const std::exception &e){if(!root.empty()){std::error_code ignored;std::filesystem::remove_all(root,ignored);}std::cerr<<e.what()<<"\n";return 1;}}
