#pragma once
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <cstdint>

namespace ember::detail {
// IDs must increase on insertion. No per-dispatch ID vector/allocation. Erasure,
// insertion and nested dispatch are safe: each invocation captures its own bound.
template<class T> struct Listeners {
    using Callback = std::function<void(T)>;
    std::map<uint64_t, std::shared_ptr<const Callback>> entries;
    void send(T value) {
        if (entries.empty()) return;
        const uint64_t end = entries.rbegin()->first;
        uint64_t last = 0;
        std::exception_ptr error;
        while (true) {
            auto it = entries.upper_bound(last);
            if (it == entries.end() || it->first > end) break;
            last = it->first;
            auto callback = it->second; // keep self-disconnecting callbacks alive
            try { (*callback)(value); } catch (...) { if (!error) error = std::current_exception(); }
        }
        if (error) std::rethrow_exception(error);
    }
};
}
