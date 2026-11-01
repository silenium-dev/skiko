#include <jawt_md.h>
#include <GL/gl.h>

#define EGL_EGL_PROTOTYPES 0

#include <EGL/egl.h>
#include <dlfcn.h>
#include <iostream>
#include "jni_helpers.h"

class EGLContextWrapper {
public:
    EGLContextWrapper(EGLDisplay _display, EGLContext _context, EGLConfig _config, EGLSurface _surface, Window _window,
                      bool _transparency, void *const egl) :
            eglDisplay(_display), eglContext(_context), eglConfig(_config), eglSurface(_surface), window(_window),
            transparency(_transparency), egl(egl) {};

    ~EGLContextWrapper() {
        const auto eglGetCurrentContext = getProc<PFNEGLGETCURRENTCONTEXTPROC>("eglGetCurrentContext");
        const auto eglMakeCurrent = getProc<PFNEGLMAKECURRENTPROC>("eglMakeCurrent");
        const auto eglDestroySurface = getProc<PFNEGLDESTROYSURFACEPROC>("eglDestroySurface");
        const auto eglDestroyContext = getProc<PFNEGLDESTROYCONTEXTPROC>("eglDestroyContext");
        // EGL defers destroying a context or surface while it is current, so release it first.
        if (eglGetCurrentContext() == eglContext) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        eglDestroySurface(eglDisplay, eglSurface);
        eglDestroyContext(eglDisplay, eglContext);
        dlclose(egl);
    };

    static EGLContextWrapper *create(Display *display, Window window, bool transparency) {
        auto egl = dlopen("libEGL.so.1", RTLD_LAZY);
        if (!egl) egl = dlopen("libEGL.so", RTLD_LAZY);
        if (!egl) {
            std::cerr << "Could not load EGL: " << dlerror() << std::endl;
            return nullptr;
        }

        const auto eglGetDisplay = (PFNEGLGETDISPLAYPROC) dlsym(egl, "eglGetDisplay");
        const auto eglInitialize = (PFNEGLINITIALIZEPROC) dlsym(egl, "eglInitialize");
        const auto eglBindAPI = (PFNEGLBINDAPIPROC) dlsym(egl, "eglBindAPI");
        const auto eglChooseConfig = (PFNEGLCHOOSECONFIGPROC) dlsym(egl, "eglChooseConfig");
        const auto eglCreateContext = (PFNEGLCREATECONTEXTPROC) dlsym(egl, "eglCreateContext");
        const auto eglCreateWindowSurface = (PFNEGLCREATEWINDOWSURFACEPROC) dlsym(egl, "eglCreateWindowSurface");
        const auto eglDestroyContext = (PFNEGLDESTROYCONTEXTPROC) dlsym(egl, "eglDestroyContext");

        const auto fail = [egl](const char *message) -> EGLContextWrapper * {
            std::cerr << message << std::endl;
            dlclose(egl);
            return nullptr;
        };

        // The display is shared by all windows on the same X11 display and eglTerminate isn't reference counted,
        // so it is never terminated.
        EGLDisplay eglDisplay = eglGetDisplay(display);
        if (eglDisplay == EGL_NO_DISPLAY) {
            return fail("Could not get egl display");
        }

        EGLint major, minor;
        if (!eglInitialize(eglDisplay, &major, &minor)) {
            return fail("Could not initialize egl");
        }

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            return fail("Could not bind OpenGL API");
        }

        EGLint configConstraints[] = {
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, transparency ? 8 : 0,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_CONFIG_CAVEAT, EGL_NONE,
                EGL_NONE,
        };

        EGLConfig eglConfig;
        EGLint numConfigs;
        if (!eglChooseConfig(eglDisplay, configConstraints, &eglConfig, 1, &numConfigs) || numConfigs < 1) {
            return fail("Could not choose egl config");
        }

        static const EGLint contextAttribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_NONE
        };
        EGLContext eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            return fail("Could not create egl context");
        }

        EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, window, nullptr);
        if (eglSurface == EGL_NO_SURFACE) {
            eglDestroyContext(eglDisplay, eglContext);
            return fail("Could not create egl surface");
        }

        return new EGLContextWrapper(eglDisplay, eglContext, eglConfig, eglSurface, window, transparency, egl);
    };

    template<typename T>
    T getProc(const std::string &name) {
        const auto func = (T) dlsym(egl, name.c_str());
        return func;
    }

    bool makeCurrent() {
        const auto eglMakeCurrent = getProc<PFNEGLMAKECURRENTPROC>("eglMakeCurrent");
        return eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    }

    bool swapBuffers() {
        const auto eglSwapBuffers = getProc<PFNEGLSWAPBUFFERSPROC>("eglSwapBuffers");
        return eglSwapBuffers(eglDisplay, eglSurface);
    }

    bool setSwapInterval(int interval) {
        const auto eglSwapInterval = getProc<PFNEGLSWAPINTERVALPROC>("eglSwapInterval");
        return eglSwapInterval(eglDisplay, interval);
    }

    EGLDisplay getDisplay() const {
        return eglDisplay;
    }

    EGLContext getContext() const {
        return eglContext;
    }

    EGLConfig getConfig() const {
        return eglConfig;
    }

private:
    EGLDisplay eglDisplay{EGL_NO_DISPLAY};
    EGLContext eglContext{EGL_NO_CONTEXT};
    EGLConfig eglConfig{nullptr};
    EGLSurface eglSurface{EGL_NO_SURFACE};
    Window window;
    bool transparency;
    void *const egl;
};

// The handle stays open for the lifetime of the process, as Skia keeps the resolved GL functions.
static PFNEGLGETPROCADDRESSPROC loadEglGetProcAddress() {
    static const auto proc = [] {
        void *egl = dlopen("libEGL.so.1", RTLD_LAZY);
        if (!egl) egl = dlopen("libEGL.so", RTLD_LAZY);
        if (!egl) {
            std::cerr << "Could not load EGL: " << dlerror() << std::endl;
            return (PFNEGLGETPROCADDRESSPROC) nullptr;
        }
        return (PFNEGLGETPROCADDRESSPROC) dlsym(egl, "eglGetProcAddress");
    }();
    return proc;
}

extern "C"
{
JNIEXPORT jlong JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_getGLProc(JNIEnv *env, jobject renderer) {
    if (!loadEglGetProcAddress()) return 0;

    return toJavaPointer(+[](void *, const char name[]) {
        return loadEglGetProcAddress()(name);
    });
}

JNIEXPORT void JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_setSwapInterval(JNIEnv *env, jobject renderer, jlong contextPtr,
                                                                        jint interval) {
    auto *context = fromJavaPointer<EGLContextWrapper *>(contextPtr);

    context->setSwapInterval(interval);
}

JNIEXPORT void JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_swapBuffers(JNIEnv *env, jobject renderer, jlong contextPtr) {
    auto *context = fromJavaPointer<EGLContextWrapper *>(contextPtr);

    context->swapBuffers();
}

JNIEXPORT void JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_makeCurrent(JNIEnv *env, jobject renderer, jlong contextPtr) {
    auto *context = fromJavaPointer<EGLContextWrapper *>(contextPtr);

    context->makeCurrent();
}

JNIEXPORT jlong JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_createContext(JNIEnv *env, jobject renderer, jlong displayPtr,
                                                                      jlong windowPtr, jboolean transparency) {
    auto *display = fromJavaPointer<Display *>(displayPtr);
    auto window = fromJavaPointer<Window>(windowPtr);
    if (!display) return 0;

    auto eglContext = EGLContextWrapper::create(display, window, transparency);
    return toJavaPointer(eglContext);
}

JNIEXPORT void JNICALL
Java_org_jetbrains_skiko_renderer_LinuxOpenGLRendererKt_destroyContext(JNIEnv *env, jobject renderer,
                                                                       jlong contextPtr) {
    auto *context = fromJavaPointer<EGLContextWrapper *>(contextPtr);

    if (context) {
        delete context;
    }
}
}
