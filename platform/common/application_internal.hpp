#pragma once
#include "ember/application.hpp"
#include "ember/render.hpp"
#include <memory>
#include <vector>
namespace ember::app::detail {
struct Driver {
    virtual ~Driver()=default;
    virtual std::unique_ptr<Backend> graphics(const Options &o) { return make_backend(o.graphics.backend,o.graphics.worker_threads); }
    virtual void frame_size(int,int) {}
    virtual bool poll(game::Host &,const Callbacks &)=0;
    virtual bool active() const=0;
    virtual void present(const std::vector<uint32_t> &,int,int)=0;
};
std::unique_ptr<Driver> make_driver(const Options &);
}
