#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ember {

// Fixed-step scheduling and independent render cadence. This class owns no
// clock and performs no sleep: callers provide elapsed time, execute the
// returned number of steps, render when requested and may use sleep_ms as a
// conservative pacing hint.
struct LoopTimer {
    double fixed_dt = 1.0 / 60.0;
    double render_dt = 1.0 / 60.0;
    double max_elapsed = 0.25;
    int max_steps = 8;
    double acc = 0;
    double render_acc = 0;

    struct Tick {
        int steps = 0;
        bool render = false;
        double sleep_ms = 0;
    };

    Tick update(double elapsed) {
        validate();
        if (!std::isfinite(elapsed) || elapsed < 0)
            throw std::invalid_argument("Elapsed time must be finite and non-negative");

        Tick out;
        elapsed = std::min(elapsed, max_elapsed);
        acc += elapsed;
        render_acc += elapsed;
        const double tolerance = std::min(1e-12, fixed_dt * 1e-9);
        while (acc + tolerance >= fixed_dt && out.steps < max_steps) {
            acc = std::max(0.0, acc - fixed_dt);
            ++out.steps;
        }
        if (out.steps == max_steps && acc >= fixed_dt)
            acc = std::fmod(acc, fixed_dt);

        if (render_acc + std::min(1e-12, render_dt * 1e-9) >= render_dt) {
            out.render = true;
            render_acc = render_acc > render_dt * 3 ? 0 : std::max(0.0, render_acc - render_dt);
        }
        // Rendering can be slower than simulation. Never sleep past the next
        // fixed step merely to wait for the next image.
        const double wait_ms = std::max(0.0, std::min(render_dt - render_acc, fixed_dt - acc) * 1000.0);
        out.sleep_ms = wait_ms >= 4 ? wait_ms - 2 : 0;
        return out;
    }

  private:
    void validate() const {
        if (!std::isfinite(fixed_dt) || fixed_dt <= 0 || !std::isfinite(render_dt) || render_dt <= 0 ||
            !std::isfinite(max_elapsed) || max_elapsed <= 0 || max_steps < 1 || max_steps > 100000 ||
            !std::isfinite(acc) || acc < 0 || !std::isfinite(render_acc) || render_acc < 0)
            throw std::invalid_argument("Invalid loop timing configuration");
    }
};

} // namespace ember
