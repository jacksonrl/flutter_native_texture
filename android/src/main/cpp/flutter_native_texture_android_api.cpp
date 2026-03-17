#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

//

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

//
#include <GLES2/gl2ext.h>

#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#include "flutter_native_texture_api.h"

static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
static EGLContext g_eglContext = EGL_NO_CONTEXT;
static EGLConfig g_eglConfig = nullptr;
static GLuint g_blitProgram = 0;

static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = nullptr;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;

// Blit Shaders
const char* kVertexShader = R"(#version 300 es
out vec2 v_uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_uv = vec2((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 300 es
#extension GL_OES_EGL_image_external : require
precision mediump float;
in vec2 v_uv;
out vec4 fragColor;
uniform samplerExternalOES u_tex;
void main() {
    // Note: If image is upside down, change v_uv.y to (1.0 - v_uv.y)
    fragColor = texture(u_tex, v_uv);
}
)";

GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

bool InitializeEGL() {
    if (g_eglDisplay != EGL_NO_DISPLAY) return true;

    g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_eglDisplay, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE};

    EGLint numConfigs;
    eglChooseConfig(g_eglDisplay, attribs, &g_eglConfig, 1, &numConfigs);

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribs);

    eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    // Compile Shaders
    // Create a temporary 1x1 pbuffer surface just to make context current for compilation
    const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface tempSurface = eglCreatePbufferSurface(g_eglDisplay, g_eglConfig, pbufferAttribs);
    eglMakeCurrent(g_eglDisplay, tempSurface, tempSurface, g_eglContext);

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    g_blitProgram = glCreateProgram();
    glAttachShader(g_blitProgram, vs);
    glAttachShader(g_blitProgram, fs);
    glLinkProgram(g_blitProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(g_blitProgram);
    glUniform1i(glGetUniformLocation(g_blitProgram, "u_tex"), 0);

    eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_eglDisplay, tempSurface);

    return true;
}

struct NativeTextureObject {
    int handle;
    int64_t flutter_texture_id;
    ANativeWindow* window = nullptr;
    AHardwareBuffer* hw_buffer = nullptr;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    GLuint gl_texture = 0;
    int32_t width;
    int32_t height;

    NativeTextureObject(int h, int64_t id, ANativeWindow* win, AHardwareBuffer* hb, int32_t w, int32_t h_val)
        : handle(h), flutter_texture_id(id), window(win), hw_buffer(hb), width(w), height(h_val) {
        egl_surface = eglCreateWindowSurface(g_eglDisplay, g_eglConfig, window, nullptr);

        eglMakeCurrent(g_eglDisplay, egl_surface, egl_surface, g_eglContext);
        glGenTextures(1, &gl_texture);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, gl_texture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    ~NativeTextureObject() {
        if (egl_surface != EGL_NO_SURFACE) {
            eglMakeCurrent(g_eglDisplay, egl_surface, egl_surface, g_eglContext);
            glDeleteTextures(1, &gl_texture);
            eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(g_eglDisplay, egl_surface);
        }
        if (window) ANativeWindow_release(window);
        if (hw_buffer) AHardwareBuffer_release(hw_buffer);
    }
};

static std::map<void*, std::unique_ptr<NativeTextureObject>> g_textures;
static std::mutex g_mutex;
static JavaVM* g_vm = nullptr;
static jclass g_plugin_class = nullptr;
static jmethodID g_create_texture_mid = nullptr;
static jmethodID g_get_id_mid = nullptr;
static jmethodID g_get_surface_mid = nullptr;
static jmethodID g_dispose_mid = nullptr;

JNIEnv* GetEnv() {
    JNIEnv* env;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        g_vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

extern "C" {

API_EXPORT FlutterNativeTextureTexture flutter_native_texture_create_texture(int32_t width, int32_t height) {
    std::lock_guard<std::mutex> lock(g_mutex);
    InitializeEGL();
    JNIEnv* env = GetEnv();

    int handle = env->CallStaticIntMethod(g_plugin_class, g_create_texture_mid, width, height);
    if (handle <= 0) return nullptr;

    int64_t texture_id = env->CallStaticLongMethod(g_plugin_class, g_get_id_mid, handle);
    jobject jSurface = env->CallStaticObjectMethod(g_plugin_class, g_get_surface_mid, handle);
    ANativeWindow* window = ANativeWindow_fromSurface(env, jSurface);
    env->DeleteLocalRef(jSurface);

    AHardwareBuffer_Desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;

    AHardwareBuffer* hw_buffer = nullptr;
    AHardwareBuffer_allocate(&desc, &hw_buffer);

    ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);

    auto tex = std::make_unique<NativeTextureObject>(handle, texture_id, window, hw_buffer, width, height);
    void* ptr = tex.get();
    g_textures[ptr] = std::move(tex);
    return (FlutterNativeTextureTexture)ptr;
}

API_EXPORT void flutter_native_texture_present_texture(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_textures.count(ref)) return;
    auto& tex = g_textures[ref];

    eglMakeCurrent(g_eglDisplay, tex->egl_surface, tex->egl_surface, g_eglContext);

    // Bind AHardwareBuffer to an EGLImage
    EGLClientBuffer clientBuf = eglGetNativeClientBufferANDROID(tex->hw_buffer);
    EGLImageKHR image = eglCreateImageKHR(g_eglDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuf, nullptr);

    // Bind EGLImage to our GL texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->gl_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)image);

    // Draw full screen quad
    glViewport(0, 0, tex->width, tex->height);
    glUseProgram(g_blitProgram);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Swap buffers and cleanup
    eglSwapBuffers(g_eglDisplay, tex->egl_surface);
    eglDestroyImageKHR(g_eglDisplay, image);

    eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

API_EXPORT void flutter_native_texture_upload_texture(FlutterNativeTextureTexture ref, const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_textures.count(ref)) return;
    auto& tex = g_textures[ref];

    void* buffer_data = nullptr;
    if (AHardwareBuffer_lock(tex->hw_buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &buffer_data) == 0) {
        AHardwareBuffer_Desc desc;
        AHardwareBuffer_describe(tex->hw_buffer, &desc);
        for (int i = 0; i < h; i++) {
            uint8_t* dst_row = (uint8_t*)buffer_data + ((y + i) * desc.stride * 4) + (x * 4);
            const uint8_t* src_row = data + (i * w * 4);
            std::memcpy(dst_row, src_row, w * 4);
        }
        AHardwareBuffer_unlock(tex->hw_buffer, nullptr);
    }
}

API_EXPORT void flutter_native_texture_download_texture(FlutterNativeTextureTexture ref, uint8_t* out_data) {
    // Blank per request
}

API_EXPORT int64_t flutter_native_texture_get_texture_id(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_textures.count(ref) ? g_textures[ref]->flutter_texture_id : -1;
}

API_EXPORT void* flutter_native_texture_get_hardware_buffer(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_textures.count(ref) ? (void*)g_textures[ref]->hw_buffer : nullptr;
}

API_EXPORT void* flutter_native_texture_get_android_native_window(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_textures.count(ref) ? (void*)g_textures[ref]->window : nullptr;
}

API_EXPORT void flutter_native_texture_dispose_texture(FlutterNativeTextureTexture ref) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_textures.count(ref)) {
        JNIEnv* env = GetEnv();
        env->CallStaticVoidMethod(g_plugin_class, g_dispose_mid, g_textures[ref]->handle);
        g_textures.erase(ref);
    }
}

API_EXPORT void* flutter_native_texture_get_dxgi_shared_handle(FlutterNativeTextureTexture ref) {
    return nullptr;
}

}  // extern "C"

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    jclass clazz = env->FindClass("com/funguscow/flutter_native_texture/FlutterNativeTexturePlugin");
    if (!clazz) return JNI_ERR;
    g_plugin_class = (jclass)env->NewGlobalRef(clazz);

    g_create_texture_mid = env->GetStaticMethodID(clazz, "createTexture", "(II)I");
    g_get_id_mid = env->GetStaticMethodID(clazz, "getTextureId", "(I)J");
    g_get_surface_mid = env->GetStaticMethodID(clazz, "getSurface", "(I)Landroid/view/Surface;");
    g_dispose_mid = env->GetStaticMethodID(clazz, "disposeTexture", "(I)V");

    return JNI_VERSION_1_6;
}