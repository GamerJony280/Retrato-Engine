#include "ember/render.hpp"
#include "shader_sources.hpp"
#include "../../platform/common/window_graphics.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdexcept>
#include "../../platform/common/gl_functions.hpp"
namespace ember {
namespace {
class OpenGL final : public Backend, private GlFunctions {
    bool functions_loaded_=false;
    bool video_ready_=false;
    bool owns_window_=true, nearest_=true;
    GLint max_texture_=0, max_resources_=0, resources_location_=-1;
    GLuint shader(GLenum type, const std::string &source) {
    GLuint s = glCreateShader(type);
    const char *p = source.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(log);
    }
    return s;
}

    SDL_Window *window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, fbo_ = 0, color_ = 0, depth_ = 0;
    GLuint resources_ = 0, resource_buffer_ = 0, settings_ = 0;
    size_t resource_size_ = 0;
    uint64_t revision_ = 0;
    int width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_, flipped_;

  public:
    explicit OpenGL(SDL_Window *target=nullptr, bool vsync=true, bool nearest=true)
        :owns_window_(!target),nearest_(nearest) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
            throw std::runtime_error(SDL_GetError());
        video_ready_=true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window_ = target ? target :
            SDL_CreateWindow("Retrato OpenGL offscreen", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window_){auto error=std::string(SDL_GetError());cleanup();throw std::runtime_error(error);}
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) {
            auto error=std::string(SDL_GetError());cleanup();throw std::runtime_error(error);
        }
        try {
            GlFunctions::load();functions_loaded_=true;
            if (target) SDL_GL_SetSwapInterval(vsync ? 1 : 0);
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_);
            glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_resources_);
            GLuint v = shader(GL_VERTEX_SHADER, gl_vertex_source), f = 0;
            try { f = shader(GL_FRAGMENT_SHADER, gl_fragment_source); }
            catch (...) { glDeleteShader(v); throw; }
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            glDeleteShader(v);
            glDeleteShader(f);
            GLint ok;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok)
                throw std::runtime_error("GL program link failed");
            resources_location_ = glGetUniformLocation(program_, "resources");
            glGenVertexArrays(1, &vao_);
            glBindVertexArray(vao_);
            glGenBuffers(1, &vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            for (int i = 0; i < 8; i++) {
                glEnableVertexAttribArray(i);
                glVertexAttribPointer(i, vertex_attributes[i].components, GL_FLOAT, GL_FALSE, sizeof(DrawVertex),
                                      reinterpret_cast<void *>(size_t(vertex_attributes[i].offset)));
            }
            glGenTextures(1, &resources_);
            glGenBuffers(1, &resource_buffer_);
            glGenBuffers(1, &settings_);
            glUniformBlockBinding(program_, glGetUniformBlockIndex(program_, "Settings"), 0);
            glGenFramebuffers(1, &fbo_);
            glGenTextures(1, &color_);
            glGenRenderbuffers(1, &depth_);
        } catch (...) {
            cleanup();
            throw;
        }
    }
    void cleanup() {
        if (context_) {
            SDL_GL_MakeCurrent(window_, context_);
            if(functions_loaded_) {
            glDeleteTextures(1, &resources_);
            glDeleteBuffers(1, &resource_buffer_);
            glDeleteBuffers(1, &settings_);
            glDeleteRenderbuffers(1, &depth_);
            glDeleteTextures(1, &color_);
            glDeleteFramebuffers(1, &fbo_);
            glDeleteBuffers(1, &vbo_);
            glDeleteVertexArrays(1, &vao_);
            glDeleteProgram(program_);
            }
            SDL_GL_DeleteContext(context_);
            context_ = nullptr;
        }
        if (window_) {
            if (owns_window_) SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if(video_ready_){SDL_QuitSubSystem(SDL_INIT_VIDEO);video_ready_=false;}
    }
    ~OpenGL() { cleanup(); }
    const char *name() const override { return "opengl"; }
    std::string device_description() const override {
        SDL_GL_MakeCurrent(window_,context_);
        const auto *text = glGetString(GL_RENDERER);
        return text ? reinterpret_cast<const char *>(text) : "unknown OpenGL device";
    }
    void draw(const Frame &f) override {
        validate_frame(f);
        if(f.width>max_texture_ || f.height>max_texture_)
            throw std::runtime_error("Frame dimensions exceed OpenGL device limits");
        SDL_GL_MakeCurrent(window_, context_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (f.width != width_ || f.height != height_) {
            width_ = f.width;
            height_ = f.height;
            glBindTexture(GL_TEXTURE_2D, color_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("GL framebuffer incomplete");
        }
        glViewport(0, 0, width_, height_);
        glClearColor(.09f, .14f, .21f, 1);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!frame_depth_test(f))
            glDisable(GL_DEPTH_TEST);
        else
            glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glUseProgram(program_);
        glBindBuffer(GL_UNIFORM_BUFFER, settings_);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(f.settings), &f.settings, GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, settings_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, resources_);
        glBindBuffer(GL_TEXTURE_BUFFER, resource_buffer_);
        if (f.resources.size() > size_t(max_resources_))
            throw std::runtime_error("Texture resources exceed this GPU's buffer limit");
        size_t bytes = std::max<size_t>(4, f.resources.size() * 4);
        const uint32_t empty_resource = 0;
        if (bytes != resource_size_ || revision_ != f.texture_revision) {
            glBufferData(GL_TEXTURE_BUFFER, bytes, f.resources.empty()?&empty_resource:f.resources.data(), GL_DYNAMIC_DRAW);
            resource_size_ = bytes;
            revision_ = f.texture_revision;
            glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, resource_buffer_);
        } else if (f.resources.size() > f.texture_words)
            glBufferSubData(GL_TEXTURE_BUFFER, f.texture_words * 4, (f.resources.size()-f.texture_words) * 4,
                            f.resources.data() + f.texture_words);
        glUniform1i(resources_location_, 0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, f.vertices.size() * sizeof(DrawVertex), f.vertices.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(f.vertices.size()));
    }
    bool direct_presentation() const noexcept override { return !owns_window_; }
    bool present() override {
        if (owns_window_) return false;
        if (!width_ || !height_) return true;
        SDL_GL_MakeCurrent(window_,context_);
        int w=0,h=0;SDL_GL_GetDrawableSize(window_,&w,&h);
        if(w<=0||h<=0)return true;
        double scale=std::min(double(w)/width_,double(h)/height_);
        int dw=std::max(1,int(width_*scale)),dh=std::max(1,int(height_*scale));
        int x=(w-dw)/2,y=(h-dh)/2;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo_);
        glBlitFramebuffer(0,0,width_,height_,x,y,x+dw,y+dh,GL_COLOR_BUFFER_BIT,nearest_?GL_NEAREST:GL_LINEAR);
        SDL_GL_SwapWindow(window_);return true;
    }
    const std::vector<uint32_t> &readback() override {
        if (width_ == 0 || height_ == 0)
            return pixels_;
        SDL_GL_MakeCurrent(window_, context_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        pixels_.resize(size_t(width_) * height_);
        flipped_.resize(pixels_.size());
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, flipped_.data());
        for (int y = 0; y < height_; y++)
            std::copy_n(flipped_.data() + size_t(height_ - 1 - y) * width_, width_,
                        pixels_.data() + size_t(y) * width_);
        return pixels_;
    }
};
} // namespace
std::unique_ptr<Backend> make_opengl() {
    return std::make_unique<OpenGL>();
}
namespace detail {
std::unique_ptr<Backend> make_opengl_window(SDL_Window *window, bool vsync, bool nearest) {
    if(!window)throw std::invalid_argument("Null OpenGL window");
    return std::make_unique<OpenGL>(window,vsync,nearest);
}
}
} // namespace ember
