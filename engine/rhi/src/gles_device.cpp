// Nimbus OpenGL ES 3.0+ backend with VAO state, FBO render targets, full blend/depth/primitive
// state mapping, viewport resize, and EGL context-loss recovery.
#include "nimbus/rhi.hpp"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#endif

#include <algorithm>
#include <cstring>
#include <vector>

#include "nimbus/log.hpp"
#include "nimbus/time.hpp"

namespace nimbus::rhi {
namespace {

GLenum toGlCompare(CompareOp op) {
  switch (op) {
    case CompareOp::Never: return GL_NEVER;
    case CompareOp::Less: return GL_LESS;
    case CompareOp::Equal: return GL_EQUAL;
    case CompareOp::LessEqual: return GL_LEQUAL;
    case CompareOp::Greater: return GL_GREATER;
    case CompareOp::NotEqual: return GL_NOTEQUAL;
    case CompareOp::Always: return GL_ALWAYS;
  }
  return GL_LESS;
}

GLenum toGlBlend(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
  }
  return GL_ONE;
}

GLenum toGlPrimitive(Primitive prim) {
  switch (prim) {
    case Primitive::Triangles: return GL_TRIANGLES;
    case Primitive::Lines: return GL_LINES;
    case Primitive::Points: return GL_POINTS;
  }
  return GL_TRIANGLES;
}

}  // namespace

class GlesDevice final : public Device {
 public:
  ~GlesDevice() override { destroyEgl(); }

  bool initialize(void* nativeWindow, uint32_t width, uint32_t height) {
    if (!nativeWindow || width == 0 || height == 0) {
      log::write(log::Level::Error, "gles: a valid native window and non-zero size are required");
      return false;
    }
    nativeWindow_ = nativeWindow;
    width_ = width;
    height_ = height;

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
      log::write(log::Level::Error, "gles: no EGL display");
      return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display_, &major, &minor)) {
      log::write(log::Level::Error, "gles: eglInitialize failed");
      return false;
    }
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                       EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (eglChooseConfig(display_, configAttributes, &config, 1, &numConfigs) != EGL_TRUE ||
        numConfigs == 0) {
      const EGLint fallbackAttributes[] = {
          EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
          EGL_DEPTH_SIZE, 16, EGL_NONE};
      numConfigs = 0;
      if (eglChooseConfig(display_, fallbackAttributes, &config, 1, &numConfigs) != EGL_TRUE ||
          numConfigs == 0) {
        log::write(log::Level::Error, "gles: no suitable EGL ES3 window config (0x%x)",
                   eglGetError());
        return false;
      }
    }
    config_ = config;
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
#ifdef __ANDROID__
    EGLint nativeVisualId = 0;
    if (eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &nativeVisualId) == EGL_TRUE) {
      const int geometryResult = ANativeWindow_setBuffersGeometry(
          static_cast<ANativeWindow*>(nativeWindow_), static_cast<int32_t>(width_),
          static_cast<int32_t>(height_), nativeVisualId);
      if (geometryResult != 0) {
        log::write(log::Level::Warning,
                   "gles: ANativeWindow_setBuffersGeometry failed (%d)", geometryResult);
      }
    }
#endif
    surface_ = eglCreateWindowSurface(display_, config_,
                                      reinterpret_cast<EGLNativeWindowType>(nativeWindow_), nullptr);
    if (surface_ == EGL_NO_SURFACE) {
      log::write(log::Level::Error, "gles: eglCreateWindowSurface failed (0x%x)", eglGetError());
      return false;
    }
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
      log::write(log::Level::Error, "gles: eglCreateContext failed (0x%x)", eglGetError());
      return false;
    }
    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
      log::write(log::Level::Error, "gles: eglMakeCurrent failed (0x%x)", eglGetError());
      return false;
    }

    glGenVertexArrays(1, &defaultVao_);
    glBindVertexArray(defaultVao_);

    EGLint actualWidth = static_cast<EGLint>(width_);
    EGLint actualHeight = static_cast<EGLint>(height_);
    (void)eglQuerySurface(display_, surface_, EGL_WIDTH, &actualWidth);
    (void)eglQuerySurface(display_, surface_, EGL_HEIGHT, &actualHeight);
    width_ = actualWidth > 0 ? static_cast<uint32_t>(actualWidth) : width_;
    height_ = actualHeight > 0 ? static_cast<uint32_t>(actualHeight) : height_;
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));

    info_.api = Api::OpenGlEs;
    const GLubyte* rendererName = glGetString(GL_RENDERER);
    info_.deviceName = rendererName ? reinterpret_cast<const char*>(rendererName) : "Unknown GLES device";
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    info_.maxTextureSize = maxTextureSize > 0 ? static_cast<uint32_t>(maxTextureSize) : 0u;
    info_.uniformBufferAlignment = 256;
    info_.computeShaders = false;
    info_.maxDrawBuffers = 1;
    contextLost_ = false;
    log::write(log::Level::Info, "gles: %s (ES %d.%d, max tex %u)",
               info_.deviceName.c_str(), major, minor, info_.maxTextureSize);
    return true;
  }

  const DeviceInfo& info() const override { return info_; }
  void* nativeHandle() override { return context_; }

  uint32_t createBuffer(const BufferDesc& desc, const void* initialData) override {
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    GLenum target = GL_ARRAY_BUFFER;
    if (desc.index) target = GL_ELEMENT_ARRAY_BUFFER;
    else if (desc.uniform || desc.storage) target = GL_UNIFORM_BUFFER;
    glBindBuffer(target, buffer);
    glBufferData(target, static_cast<GLsizeiptr>(desc.size), initialData,
                 desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    buffers_.push_back({buffer, target, desc.size});
    return static_cast<uint32_t>(buffers_.size());
  }

  void destroyBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    BufferEntry& entry = buffers_[handle - 1];
    if (entry.buffer) glDeleteBuffers(1, &entry.buffer);
    entry.buffer = 0;
  }

  void* mapBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return nullptr;
    BufferEntry& entry = buffers_[handle - 1];
    glBindBuffer(entry.target, entry.buffer);
    return glMapBufferRange(entry.target, 0, static_cast<GLsizeiptr>(entry.size),
                            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
  }

  void unmapBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    glBindBuffer(buffers_[handle - 1].target, buffers_[handle - 1].buffer);
    glUnmapBuffer(buffers_[handle - 1].target);
  }

  uint32_t createTexture(const TextureDesc& desc, const void* initialData) override {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLenum internalFormat = toGlInternalFormat(desc.format);
    const GLenum uploadFormat = desc.depthTarget ? GL_DEPTH_COMPONENT : GL_RGBA;
    const GLenum type = desc.depthTarget ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                 static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                 uploadFormat, type, initialData);
    if (desc.mipLevels > 1 && initialData) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    (desc.mipLevels > 1 && initialData) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    textures_.push_back({texture, desc});
    return static_cast<uint32_t>(textures_.size());
  }

  void destroyTexture(uint32_t handle) override {
    if (handle == 0 || handle > textures_.size()) return;
    TextureEntry& entry = textures_[handle - 1];
    if (entry.texture) glDeleteTextures(1, &entry.texture);
    entry.texture = 0;
  }

  void updateTexture(uint32_t handle, uint32_t mipLevel, const void* data, uint32_t size) override {
    if (handle == 0 || handle > textures_.size() || !data) return;
    TextureEntry& entry = textures_[handle - 1];
    glBindTexture(GL_TEXTURE_2D, entry.texture);
    glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(mipLevel), 0, 0,
                    static_cast<GLsizei>(entry.desc.width),
                    static_cast<GLsizei>(entry.desc.height), GL_RGBA, GL_UNSIGNED_BYTE, data);
    (void)size;
  }

  uint32_t createPipeline(const PipelineDesc& desc) override {
    const GLuint program = glCreateProgram();
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, desc.vertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, desc.fragmentShader);
    if (!vertex || !fragment) {
      if (vertex) glDeleteShader(vertex);
      if (fragment) glDeleteShader(fragment);
      glDeleteProgram(program);
      return 0;
    }
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!linked) {
      char logBuf[1024];
      glGetProgramInfoLog(program, sizeof(logBuf), nullptr, logBuf);
      log::write(log::Level::Error, "gles: program link failed: %s", logBuf);
      glDeleteProgram(program);
      return 0;
    }
    pipelines_.push_back({program, desc});
    return static_cast<uint32_t>(pipelines_.size());
  }

  void destroyPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size()) return;
    PipelineEntry& entry = pipelines_[handle - 1];
    if (entry.program) glDeleteProgram(entry.program);
    entry.program = 0;
  }

  uint32_t createShaderUniform(uint32_t size) override {
    return createBuffer(BufferDesc{size, false, false, true, false, true}, nullptr);
  }

  void destroyShaderUniform(uint32_t handle) override { destroyBuffer(handle); }

  uint32_t createRenderTarget(uint32_t width, uint32_t height, Format colorFormat,
                              bool hasDepth) override {
    if (width == 0 || height == 0) return 0;
    const uint32_t colorTex =
        createTexture(TextureDesc{width, height, 1, 1, colorFormat, true, false}, nullptr);
    if (colorTex == 0) return 0;

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textures_[colorTex - 1].texture, 0);

    GLuint depthRbo = 0;
    if (hasDepth) {
      glGenRenderbuffers(1, &depthRbo);
      glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                            static_cast<GLsizei>(width), static_cast<GLsizei>(height));
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                depthRbo);
    }
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
      glDeleteFramebuffers(1, &fbo);
      destroyTexture(colorTex);
      return 0;
    }
    renderTargets_.push_back({fbo, depthRbo, colorTex, width, height});
    return static_cast<uint32_t>(renderTargets_.size());
  }

  void destroyRenderTarget(uint32_t handle) override {
    if (handle == 0 || handle > renderTargets_.size()) return;
    RenderTargetEntry& rt = renderTargets_[handle - 1];
    if (rt.fbo) glDeleteFramebuffers(1, &rt.fbo);
    if (rt.depthRbo) glDeleteRenderbuffers(1, &rt.depthRbo);
    if (rt.colorTexture) destroyTexture(rt.colorTexture);
    rt.fbo = 0;
    rt.depthRbo = 0;
    rt.colorTexture = 0;
  }

  void beginRenderTarget(uint32_t targetHandle, const RenderPassDesc& desc) override {
    if (targetHandle != 0 && targetHandle <= renderTargets_.size() &&
        renderTargets_[targetHandle - 1].fbo != 0) {
      const RenderTargetEntry& rt = renderTargets_[targetHandle - 1];
      glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
      glViewport(0, 0, static_cast<GLsizei>(rt.width), static_cast<GLsizei>(rt.height));
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    }
    beginPass(desc);
  }

  uint32_t renderTargetColorTexture(uint32_t targetHandle) const override {
    if (targetHandle == 0 || targetHandle > renderTargets_.size()) return 0;
    return renderTargets_[targetHandle - 1].colorTexture;
  }

  void beginFrame() override {
    stats_ = time::FrameStats{};
    if (defaultVao_) glBindVertexArray(defaultVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void beginPass(const RenderPassDesc& desc) override {
    if (desc.clearColor) {
      glClearColor(desc.clearColorValue[0], desc.clearColorValue[1],
                   desc.clearColorValue[2], desc.clearColorValue[3]);
    }
    glClearDepthf(desc.clearDepthValue);
    if (desc.depthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    if (desc.cullMode != CullMode::None) {
      glEnable(GL_CULL_FACE);
      glCullFace(desc.cullMode == CullMode::Back ? GL_BACK : GL_FRONT);
    } else {
      glDisable(GL_CULL_FACE);
    }
    glClear((desc.clearColor ? GL_COLOR_BUFFER_BIT : 0) |
            (desc.clearDepth ? GL_DEPTH_BUFFER_BIT : 0));
  }

  void bindPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size()) return;
    PipelineEntry& entry = pipelines_[handle - 1];
    glUseProgram(entry.program);
    currentPrimitive_ = toGlPrimitive(entry.desc.primitive);
    if (defaultVao_) glBindVertexArray(defaultVao_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    if (entry.desc.depthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(entry.desc.depthWrite ? GL_TRUE : GL_FALSE);
    glDepthFunc(toGlCompare(entry.desc.depthCompare));
    if (entry.desc.cullMode != CullMode::None) {
      glEnable(GL_CULL_FACE);
      glCullFace(entry.desc.cullMode == CullMode::Back ? GL_BACK : GL_FRONT);
    } else {
      glDisable(GL_CULL_FACE);
    }
    if (entry.desc.blendEnable) {
      glEnable(GL_BLEND);
      glBlendFunc(toGlBlend(entry.desc.srcFactor), toGlBlend(entry.desc.dstFactor));
    } else {
      glDisable(GL_BLEND);
    }
  }

  void bindVertexBuffer(uint32_t handle, uint32_t binding) override {
    if (handle == 0 || handle > buffers_.size()) return;
    glBindBuffer(GL_ARRAY_BUFFER, buffers_[handle - 1].buffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 8, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
                          reinterpret_cast<void*>(sizeof(float) * 3));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8,
                          reinterpret_cast<void*>(sizeof(float) * 6));
    (void)binding;
  }

  void bindIndexBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers_[handle - 1].buffer);
  }

  void bindUniform(uint32_t handle, uint32_t slot, const void* data, uint32_t size) override {
    if (handle == 0 || handle > buffers_.size() || !data) return;
    BufferEntry& entry = buffers_[handle - 1];
    glBindBuffer(GL_UNIFORM_BUFFER, entry.buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
    const GLuint prog = currentProgram();
    if (prog != 0) glUniformBlockBinding(prog, slot, slot);
    glBindBufferBase(GL_UNIFORM_BUFFER, slot, entry.buffer);
  }

  void bindTexture(uint32_t handle, uint32_t slot) override {
    if (handle == 0 || handle > textures_.size()) return;
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textures_[handle - 1].texture);
  }

  void draw(uint32_t vertexCount, uint32_t firstVertex) override {
    glDrawArrays(currentPrimitive_, static_cast<GLint>(firstVertex),
                 static_cast<GLsizei>(vertexCount));
    ++stats_.drawCalls;
    stats_.triangles += vertexCount / 3u;
  }

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t vertexOffset) override {
    glDrawElements(currentPrimitive_, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(static_cast<size_t>(firstIndex) * sizeof(uint32_t)));
    ++stats_.drawCalls;
    stats_.triangles += indexCount / 3u;
    (void)vertexOffset;
  }

  void endPass() override { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
  void endFrame() override {}

  void present() override {
    if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
      const EGLint err = eglGetError();
      if (err == EGL_CONTEXT_LOST) {
        contextLost_ = true;
        log::write(log::Level::Warning, "gles: EGL_CONTEXT_LOST detected during present");
      } else if (!presentFailureLogged_) {
        log::write(log::Level::Warning, "gles: eglSwapBuffers failed (0x%x)", err);
        presentFailureLogged_ = true;
      }
    }
  }

  void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width),
               static_cast<GLsizei>(height));
  }

  void resize(uint32_t width, uint32_t height) override {
    if (width == 0 || height == 0) return;
    width_ = width;
    height_ = height;
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  }

  bool isContextLost() const override { return contextLost_; }

  bool recoverContext() override {
    if (!nativeWindow_ || width_ == 0 || height_ == 0) return false;
    destroyEgl();
    buffers_.clear();
    textures_.clear();
    pipelines_.clear();
    renderTargets_.clear();
    return initialize(nativeWindow_, width_, height_);
  }

  const time::FrameStats& frameStats() const override { return stats_; }

 private:
  struct BufferEntry {
    GLuint buffer = 0;
    GLenum target = GL_ARRAY_BUFFER;
    uint32_t size = 0;
  };
  struct TextureEntry {
    GLuint texture = 0;
    TextureDesc desc{};
  };
  struct PipelineEntry {
    GLuint program = 0;
    PipelineDesc desc{};
  };
  struct RenderTargetEntry {
    GLuint fbo = 0;
    GLuint depthRbo = 0;
    uint32_t colorTexture = 0;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  void destroyEgl() {
    if (display_ == EGL_NO_DISPLAY) return;
    if (eglGetCurrentDisplay() == display_)
      (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (defaultVao_) {
      glDeleteVertexArrays(1, &defaultVao_);
      defaultVao_ = 0;
    }
    if (surface_ != EGL_NO_SURFACE) {
      (void)eglDestroySurface(display_, surface_);
      surface_ = EGL_NO_SURFACE;
    }
    if (context_ != EGL_NO_CONTEXT) {
      (void)eglDestroyContext(display_, context_);
      context_ = EGL_NO_CONTEXT;
    }
    (void)eglTerminate(display_);
    (void)eglReleaseThread();
    display_ = EGL_NO_DISPLAY;
  }

  GLuint currentProgram() const {
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    return static_cast<GLuint>(program);
  }

  static GLenum toGlInternalFormat(Format format) {
    switch (format) {
      case Format::R8Unorm: return GL_R8;
      case Format::R8G8B8A8Unorm:
      case Format::R8G8B8A8Srgb:
      case Format::R8G8B8A8Uint: return GL_RGBA8;
      case Format::B8G8R8A8Unorm: return GL_RGBA;
      case Format::R16Float: return GL_R16F;
      case Format::R32Float: return GL_R32F;
      case Format::R32G32Float: return GL_RG32F;
      case Format::R32G32B32A32Float: return GL_RGBA32F;
      case Format::D24UnormS8Uint: return GL_DEPTH24_STENCIL8;
      case Format::D32Float: return GL_DEPTH_COMPONENT32F;
    }
    return GL_RGBA8;
  }

  GLuint compileShader(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const char* code = source.c_str();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &code, &length);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      char logBuf[1024];
      glGetShaderInfoLog(shader, sizeof(logBuf), nullptr, logBuf);
      log::write(log::Level::Error, "gles: shader compile failed: %s", logBuf);
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  }

  DeviceInfo info_;
  void* nativeWindow_ = nullptr;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLConfig config_ = nullptr;
  EGLSurface surface_ = EGL_NO_SURFACE;
  EGLContext context_ = EGL_NO_CONTEXT;
  GLuint defaultVao_ = 0;
  GLenum currentPrimitive_ = GL_TRIANGLES;
  bool presentFailureLogged_ = false;
  bool contextLost_ = false;
  std::vector<BufferEntry> buffers_;
  std::vector<TextureEntry> textures_;
  std::vector<PipelineEntry> pipelines_;
  std::vector<RenderTargetEntry> renderTargets_;
  time::FrameStats stats_;
};

Device* createGlesDevice(void* nativeWindow, uint32_t width, uint32_t height) {
  auto* device = new GlesDevice();
  if (!device->initialize(nativeWindow, width, height)) {
    delete device;
    return nullptr;
  }
  return device;
}

}  // namespace nimbus::rhi
