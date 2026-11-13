#pragma once
namespace ember::app::detail {
// Independent OS states: restoring a window must not undo a lost focus/pause.
struct ActivityState {
    bool resumed=true,focused=true,visible=true;
    bool active() const noexcept {return resumed&&focused&&visible;}
};
}
