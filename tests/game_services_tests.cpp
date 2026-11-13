#include "ember/runtime.hpp"
#include "ember/game.hpp"
#include "ember/projectiles.hpp"
#include "ember/particles.hpp"
#include <iostream>
using namespace ember;
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f) { try { f(); } catch(const std::exception&) {return;} throw std::runtime_error("Expected rejection"); }
static bool wall(Vec3 p,Vec3 d,float r,ProjectileContact &out,const void*) {
    Aabb b;b.add({.5f,-1,-1});b.add({.501f,1,1});float fraction;Vec3 normal;
    if(!sweep_sphere_box(p,d,r,b,fraction,normal)) return false;
    out={fraction,normal,7};return true;
}
int main(int argc,char **argv) { try {
    Config cfg;cfg.backend="cpu";cfg.worker_threads=0;
    auto world=std::make_shared<Runtime>(cfg);game::Host host(world),second(world);
    int legacy=0,a=0,b=0; world->on_event=[&](Runtime&,const Event&e){if(e.type==Event::Type::FixedUpdate)++legacy;};
    auto ca=host.on_update([&](float){++a;});auto cb=second.on_update([&](float){++b;});
    host.tick(1./60);check(legacy==1&&a==1&&b==1,"shared runtime preserves native and both Host listeners");
    Runtime::EventConnection late;int deliveries=0;bool installed=false;
    auto first=world->observe([&](Runtime&,const Event&){if(!installed) {installed=true;late=world->observe([&](Runtime&,const Event&){++deliveries;});}});
    world->dispatch({Event::Type::Cue});check(deliveries==0,"new observer waits for next event");
    world->dispatch({Event::Type::Cue});check(deliveries==1,"observer later executes");
    first.disconnect();late.disconnect();
    auto fails=world->observe([](Runtime&,const Event&){throw std::runtime_error("failure");});
    int peers=0;auto after=world->observe([&](Runtime&,const Event&){++peers;});
    rejects([&]{host.tick(1./60);});check(peers==1,"observer failure does not skip peers");fails.disconnect();after.disconnect();
    int state=0;auto persistent=host.on_update([count=0,&state](float) mutable {state=++count;});
    host.tick(1./60);host.tick(1./60);check(state==2,"mutable listener state persists across events");persistent.disconnect();
    {
        Runtime churn(cfg);
        std::array<ObjectRef, 200> live{};
        for (size_t i = 0; i < 1000000; ++i) {
            auto ref = churn.add_object(Object{});
            auto &old = live[i % live.size()];
            if (old.id) {
                churn.destroy_object(old);
                check(!churn.object_alive(old), "destroyed reference stays expired");
            }
            old = ref;
        }
        check(churn.scene.objects.size() == 201, "million managed spawns keep bounded slot count");
        auto stale = live[0];
        churn.destroy_object(stale);
        rejects([&]{churn.destroy_object(stale);});
        auto replacement = churn.add_object(Object{});
        check(replacement.index == stale.index && replacement.id != stale.id &&
              !churn.object_alive(stale), "reused slot does not revive old reference");
        churn.destroy_object(replacement);
        // Raw scene mutation must not allow a stale free-list entry to replace a live object.
        churn.scene.objects[replacement.index] = Object{};
        auto external = churn.object_ref(replacement.index);
        churn.add_object(Object{});
        check(churn.object_alive(external), "identity guards externally replaced slots");
        churn.destroy_object(external);
        Object foreign;
        foreign.active = false;
        churn.scene.objects[external.index] = std::move(foreign);
        const auto foreign_id = churn.scene.objects[external.index].id;
        auto managed = churn.add_object(Object{});
        check(managed.index != external.index && churn.scene.objects[external.index].id == foreign_id,
              "inactive foreign slot is not overwritten by stale free entry");
        churn.clear_scene();
        auto fresh = churn.add_object(Object{});
        check(fresh.index == 0 && !churn.object_alive(stale), "clear resets slot bookkeeping");
    }
    auto e=host.create("box");host.add_box_collider(e,{.01f,1,1},2);host.set_position(e,{2,0,0});host.set_visible(e,false);
    check(host.alive(e)&&host.sweep({0,0,0},{4,0,0},.02f,2).entity==e,"hidden entity remains collidable");
    check(!host.sweep({0,0,0},{4,0,0},.02f,1).hit,"query mask");
    SceneRenderer renderer;Frame f;renderer.prepare(world->scene,world->camera,cfg,f);check(f.vertices.size()==6,"hidden geometry excluded; only background remains");
    auto slide=host.slide({0,0,0},{4,0,0},.02f,2);check(slide.contacts>0&&slide.position.x<2,"slide blocks thin wall");
    auto old=e;world->clear_scene();rejects([&]{host.entity("box");});e=host.create("box");check(!host.alive(old)&&host.alive(e),"native clear allows name reuse without revival");
    host.dispatch_input({game::InputEvent::Type::KeyDown,0,32});host.dispatch_input({game::InputEvent::Type::KeyUp,0,32});
    check(host.key_pressed(32)&&host.key_released(32)&&!host.key_down(32),"short tap edges persist");host.tick(1./60);
    check(!host.key_pressed(32)&&!host.key_released(32),"edges expire per fixed step");
    host.dispatch_input({game::InputEvent::Type::KeyDown,0,32});host.tick(1./60);host.dispatch_input({game::InputEvent::Type::KeyDown,0,32});
    check(!host.key_pressed(32),"repeat is not another edge");host.reset_input();check(!host.key_down(32),"focus release");
    if(argc>1) {auto asset=host.load_asset(std::string(argv[1])+"/assets/robot.embasset");auto model=host.spawn(asset,"robot");host.unload_asset(asset);check(host.alive(model),"unload preserves live asset instances");rejects([&]{host.spawn(asset,"stale");});}
    auto temp=std::make_unique<game::Host>(world);auto release=temp->on_update([&](float){temp.reset();});world->tick(1./60);check(!temp,"Host may be released during update");
    ProjectilePool pool(2);Projectile p;p.velocity={100,0,0};p.life=.1f;
    auto id=pool.spawn(p);check(id&&pool.spawn(p)&&!pool.spawn(p),"bounded projectile spawn");
    pool.update(1,{wall,nullptr});check(pool.alive()==0&&pool.impacts().size()==2,"sweep covers last partial lifetime");
    check(pool.impacts()[0].target==7&&pool.impacts()[0].projectile.position.x<.5f,"thin wall continuous impact");pool.update(0);check(pool.impacts().empty(),"impact lifetime until next update");
    rejects([&]{pool.update(-1);});p.radius=-1;rejects([&]{pool.spawn(p);});
    p.radius=.05f;pool.spawn(p);
    auto mutate=[](Vec3,Vec3,float,ProjectileContact&,const void *ctx)->bool {static_cast<ProjectilePool*>(const_cast<void*>(ctx))->clear();return false;};
    rejects([&]{pool.update(.01f,{mutate,&pool});});pool.clear();check(pool.alive()==0,"query error releases update guard");
    ParticlePool particles(2);particles.spawn({},{1,0,0},{1,1,1},1);
    ParticleStep step;step.collisions=true;
    auto change=[](Vec3,Vec3,float,ParticleContact&,const void *ctx)->bool {static_cast<ParticlePool*>(const_cast<void*>(ctx))->clear();return false;};
    rejects([&]{particles.update(.01f,{},step,{change,&particles});});particles.clear();
    float fraction;Vec3 normal;Aabb valid;valid.add({-1,-1,-1});valid.add({1,1,1});
    rejects([&]{sweep_sphere_box({},{1,0,0},NAN,valid,fraction,normal);});
    rejects([&]{slide_sphere_boxes({},{},INFINITY,{});});
    CollisionMesh mesh;Hit hit;rejects([&]{mesh.raycast({NAN,0,0},{1,0,0},1,hit);});
    std::cout<<"Runtime/Host/projectile regression checks passed\n";return 0;
} catch(const std::exception&e) {std::cerr<<e.what()<<"\n";return 1;} }
