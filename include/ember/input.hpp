#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ember {

// Platform-neutral key-to-action map. Several keys may drive one action;
// releasing one key does not release the action while another remains down.
// Repeated key-down events are ignored, so OS key repeat cannot create false
// press edges. Call next_frame() after each simulation step that consumes
// input, not after a render/poll iteration that may execute no simulation.
class ActionMap {
    std::unordered_map<int, int> key_to_action_;
    std::unordered_set<int> down_keys_;
    std::vector<unsigned> held_count_;
    std::vector<unsigned char> pressed_, released_;

    void grow(int action) {
        if (action < 0)
            throw std::invalid_argument("Action index cannot be negative");
        const size_t size = size_t(action) + 1;
        if (held_count_.size() < size) {
            held_count_.resize(size, 0);
            pressed_.resize(size, 0);
            released_.resize(size, 0);
        }
    }

    void press_action(int action) {
        grow(action);
        if (held_count_[size_t(action)]++ == 0)
            pressed_[size_t(action)] = 1;
    }

    void release_action(int action) {
        grow(action);
        auto &count = held_count_[size_t(action)];
        if (count > 0 && --count == 0)
            released_[size_t(action)] = 1;
    }

  public:
    ActionMap &bind(int action, int key) {
        grow(action);
        auto old = key_to_action_.find(key);
        if (old != key_to_action_.end() && old->second == action)
            return *this;
        const bool down = down_keys_.count(key) != 0;
        if (old != key_to_action_.end() && down)
            release_action(old->second);
        key_to_action_[key] = action;
        if (down)
            press_action(action);
        return *this;
    }

    void unbind_key(int key) {
        auto it = key_to_action_.find(key);
        if (it == key_to_action_.end())
            return;
        if (down_keys_.count(key))
            release_action(it->second);
        key_to_action_.erase(it);
    }

    void key_down(int key) {
        if (!down_keys_.insert(key).second)
            return;
        auto it = key_to_action_.find(key);
        if (it != key_to_action_.end())
            press_action(it->second);
    }

    void key_up(int key) {
        if (!down_keys_.erase(key))
            return;
        auto it = key_to_action_.find(key);
        if (it != key_to_action_.end())
            release_action(it->second);
    }

    void next_frame() {
        std::fill(pressed_.begin(), pressed_.end(), 0);
        std::fill(released_.begin(), released_.end(), 0);
    }

    // Focus loss may prevent the platform from delivering key-up events.
    // Keep release edges so a variable-height jump can still be cancelled.
    void release_all() {
        down_keys_.clear();
        std::fill(pressed_.begin(), pressed_.end(), 0);
        for (size_t i = 0; i < held_count_.size(); ++i) {
            if (held_count_[i])
                released_[i] = 1;
            held_count_[i] = 0;
        }
    }

    bool held(int action) const {
        return action >= 0 && size_t(action) < held_count_.size() && held_count_[size_t(action)] > 0;
    }
    bool pressed(int action) const {
        return action >= 0 && size_t(action) < pressed_.size() && pressed_[size_t(action)] != 0;
    }
    bool released(int action) const {
        return action >= 0 && size_t(action) < released_.size() && released_[size_t(action)] != 0;
    }
};

} // namespace ember
