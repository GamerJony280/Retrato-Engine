#include "ember/render.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cstring>
namespace ember {
namespace {
struct RasterTriangle {
    DrawVertex v[3];
    float area;
    int x0, x1, y0, y1;
};
float edge(const DrawVertex &a, const DrawVertex &b, float x, float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}
// SceneRenderer's opaque constant quad needs no interpolation, texturing or
// lighting. Match every byte of the explicitly padding-free vertex contract;
// user-authored/mutated packets take the normal rasterization path.
bool constant_background(const Frame &f) {
    if (f.vertices.size() < 6)
        return false;
    const Vec2 corners[] = {{-1,-1}, {1,-1}, {1,1}, {-1,-1}, {1,1}, {-1,1}};
    for (size_t i = 0; i < 6; ++i) {
        const DrawVertex expected{corners[i].x, corners[i].y, .999999f, 1, 0, 0,
                                  .09f, .14f, .21f, 0};
        if (std::memcmp(&f.vertices[i], &expected, sizeof(expected)) != 0)
            return false;
    }
    return true;
}
class Cpu final : public Backend {
    std::vector<uint32_t> pixels_;
    std::vector<float> depth_;
    std::vector<RasterTriangle> triangles_;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable start_, done_;
    bool stop_ = false;
    unsigned epoch_ = 0;
    int pending_ = 0;
    const Frame *frame_ = nullptr;
    std::array<uint32_t, 48> background_{};
    float clear_depth_ = 1;
    void rows(int job, int jobs) {
        const auto &f = *frame_;
        int y0 = f.height * job / jobs, y1 = f.height * (job + 1) / jobs;
        for (int y = y0; y < y1; y++) {
            auto row = pixels_.begin() + size_t(y) * f.width;
            if (f.profile != Profile::PS1 && !f.crt) {
                std::fill_n(row, f.width, background_[0]);
            } else {
                // Bayer repeats every 4 pixels; CRT RGB mask every 3. Their
                // combined period is 12x4, independent of frame dimensions.
                const auto tile = background_.begin() + (y % 4) * 12;
                for (int x = 0; x < f.width; x += 12)
                    std::copy_n(tile, std::min(12, f.width - x), row + x);
            }
            if (frame_depth_test(f))
                std::fill_n(depth_.begin() + size_t(y) * f.width, f.width, clear_depth_);
        }
        for (const auto &t : triangles_) {
            int ya = std::max(y0, t.y0), yb = std::min(y1 - 1, t.y1);
            if (ya > yb)
                continue;
            for (int y = ya; y <= yb; y++)
                for (int x = t.x0; x <= t.x1; x++) {
                    float a = edge(t.v[1], t.v[2], x + .5f, y + .5f) / t.area,
                          b = edge(t.v[2], t.v[0], x + .5f, y + .5f) / t.area, c = 1 - a - b;
                    if (a < 0 || b < 0 || c < 0)
                        continue;
                    float z = a * t.v[0].z + b * t.v[1].z + c * t.v[2].z;
                    size_t index = size_t(y) * f.width + x;
                    if (frame_depth_test(f) && z >= depth_[index])
                        continue;
                    float iw = a * t.v[0].invw + b * t.v[1].invw + c * t.v[2].invw;
                    if (iw <= 0)
                        continue;
                    float w = 1 / iw;
                    Vec3 col{(a * t.v[0].r + b * t.v[1].r + c * t.v[2].r) * w,
                             (a * t.v[0].g + b * t.v[1].g + c * t.v[2].g) * w,
                             (a * t.v[0].b + b * t.v[1].b + c * t.v[2].b) * w};
                    auto blend = [&](Vec3 DrawVertex::*field) {
                        return (t.v[0].*field * a + t.v[1].*field * b + t.v[2].*field * c) * w;
                    };
                    Vec2 uv{(a * t.v[0].u + b * t.v[1].u + c * t.v[2].u) * w,
                            (a * t.v[0].v + b * t.v[1].v + c * t.v[2].v) * w};
                    float alpha = 1;
                    col = shade_fragment(f, t.v[0], col, blend(&DrawVertex::sun_color),
                                         blend(&DrawVertex::world), blend(&DrawVertex::normal), uv, alpha);
                    if (alpha < t.v[0].alpha_cutoff)
                        continue;
                    pixels_[index] = finish_pixel(col, x, y, f.profile, f.crt);
                    if (frame_depth_test(f)) depth_[index] = z;
                }
        }
    }

  public:
    explicit Cpu(int workers) {
        if (workers < -1 || workers > 64)
            throw std::invalid_argument("CPU worker count must be -1..64");
        if (workers < 0)
            workers = std::min(3, std::max(0, int(std::thread::hardware_concurrency()) - 1));
        try {
            for (int i = 0; i < workers; i++)
                threads_.emplace_back([this, i, workers] {
                    unsigned seen = 0;
                    for (;;) {
                        std::unique_lock<std::mutex> lock(mutex_);
                        start_.wait(lock, [&] { return stop_ || epoch_ != seen; });
                        if (stop_)
                            return;
                        seen = epoch_;
                        lock.unlock();
                        rows(i + 1, workers + 1);
                        lock.lock();
                        if (--pending_ == 0)
                            done_.notify_one();
                    }
                });
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }
            start_.notify_all();
            for (auto &thread : threads_)
                thread.join();
            throw;
        }
    }
    ~Cpu() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        start_.notify_all();
        for (auto &t : threads_)
            t.join();
    }
    const char *name() const override { return "cpu"; }
    void draw(const Frame &f) override {
        validate_frame(f);
        pixels_.resize(size_t(f.width) * f.height);
        if (frame_depth_test(f)) depth_.resize(pixels_.size());
        triangles_.clear();
        triangles_.reserve(f.vertices.size() / 3);
        const bool background = constant_background(f);
        clear_depth_ = background ? .999999f : 1.f;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 12; ++x)
                background_[size_t(y) * 12 + x] = finish_pixel({.09f, .14f, .21f}, x, y, f.profile, f.crt);
        for (size_t i = background ? 6 : 0; i < f.vertices.size(); i += 3) {
            RasterTriangle t{};
            for (int k = 0; k < 3; k++) {
                t.v[k] = f.vertices[i + k];
                t.v[k].x = (t.v[k].x * .5f + .5f) * f.width;
                t.v[k].y = (t.v[k].y * .5f + .5f) * f.height;
            }
            t.area = edge(t.v[0], t.v[1], t.v[2].x, t.v[2].y);
            if (!std::isfinite(t.area) || std::abs(t.area) < 1e-7f)
                continue;
            // Clamp before conversion: finite vertices can still lie far
            // outside the viewport and exceed the range of an integer.
            t.x0 = int(std::floor(std::clamp(std::min({t.v[0].x, t.v[1].x, t.v[2].x}), 0.f, float(f.width))));
            t.x1 = int(std::ceil(std::clamp(std::max({t.v[0].x, t.v[1].x, t.v[2].x}), -1.f, float(f.width - 1))));
            t.y0 = int(std::floor(std::clamp(std::min({t.v[0].y, t.v[1].y, t.v[2].y}), 0.f, float(f.height))));
            t.y1 = int(std::ceil(std::clamp(std::max({t.v[0].y, t.v[1].y, t.v[2].y}), -1.f, float(f.height - 1))));
            triangles_.push_back(t);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            frame_ = &f;
            pending_ = int(threads_.size());
            ++epoch_;
        }
        start_.notify_all();
        rows(0, int(threads_.size()) + 1);
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
    }
    const std::vector<uint32_t> &readback() override { return pixels_; }
};
} // namespace
std::unique_ptr<Backend> make_cpu(int workers) {
    return std::make_unique<Cpu>(workers);
}
} // namespace ember
