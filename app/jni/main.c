#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define LOG_TAG "Box3D"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

typedef struct {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    GLuint program;
    GLuint position_loc;
    GLuint color_loc;
    struct timespec last_time;
    int frame_count;
    float current_fps;
    int running;
} Engine;

static const char* vertex_shader_source =
    "attribute vec4 position;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  v_color = color;\n"
    "}\n";

static const char* fragment_shader_source =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_FragColor = v_color;\n"
    "}\n";

GLuint load_shader(GLenum type, const char* shader_src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shader_src, NULL);
    glCompileShader(shader);
    return shader;
}

void init_gl(Engine* engine) {
    GLuint vertex_shader = load_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = load_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    engine->program = glCreateProgram();
    glAttachShader(engine->program, vertex_shader);
    glAttachShader(engine->program, fragment_shader);
    glLinkProgram(engine->program);
    engine->position_loc = glGetAttribLocation(engine->program, "position");
    engine->color_loc = glGetAttribLocation(engine->program, "color");
    clock_gettime(CLOCK_MONOTONIC, &engine->last_time);
    engine->frame_count = 0;
    engine->current_fps = 0.0f;
}

void calculate_fps(Engine* engine) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    engine->frame_count++;
    double elapsed = (current_time.tv_sec - engine->last_time.tv_sec) + 
                     (current_time.tv_nsec - engine->last_time.tv_nsec) / 1000000000.0;
    if (elapsed >= 0.5) {
        engine->current_fps = (float)(engine->frame_count / elapsed);
        LOGI("MangoHUD style overlay -> FPS: %.1f", engine->current_fps);
        engine->frame_count = 0;
        engine->last_time = current_time;
    }
}

void draw_frame(Engine* engine) {
    if (engine->display == NULL || engine->surface == NULL) return;
    calculate_fps(engine);
    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(engine->program);

    float vertices[] = {
        -0.25f,  0.25f, 0.0f,
        -0.25f, -0.25f, 0.0f,
         0.25f, -0.25f, 0.0f,
        -0.25f,  0.25f, 0.0f,
         0.25f, -0.25f, 0.0f,
         0.25f,  0.25f, 0.0f,

        -0.25f,  0.55f, 0.0f,
        -0.25f, -0.25f, 0.0f,
         0.25f,  0.30f, 0.0f,
        -0.25f,  0.55f, 0.0f,
         0.25f,  0.30f, 0.0f,
         0.25f,  0.55f, 0.0f,

        -0.55f,  0.25f, 0.0f,
        -0.55f, -0.25f, 0.0f,
        -0.30f, -0.25f, 0.0f,
        -0.55f,  0.25f, 0.0f,
        -0.30f, -0.25f, 0.0f,
        -0.30f,  0.25f, 0.0f,

        -0.55f,  0.55f, 0.0f,
        -0.55f,  0.30f, 0.0f,
        -0.30f,  0.30f, 0.0f,
        -0.55f,  0.55f, 0.0f,
        -0.30f,  0.30f, 0.0f,
        -0.30f,  0.55f, 0.0f
    };

    float colors[] = {
        1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,

        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,

        1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f,
        1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f,

        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
    };

    glVertexAttribPointer(engine->position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(engine->position_loc);
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, colors);
    glEnableVertexAttribArray(engine->color_loc);

    glDrawArrays(GL_TRIANGLES, 0, 24);
    eglSwapBuffers(engine->display, engine->surface);
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    const EGLint attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_NONE };
    EGLint numConfigs;
    EGLConfig config;
    engine->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(engine->display, 0, 0);
    eglChooseConfig(engine->display, attribs, &config, 1, &numConfigs);
    engine->surface = eglCreateWindowSurface(engine->display, config, window, NULL);
    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    engine->context = eglCreateContext(engine->display, config, NULL, contextAttribs);
    eglMakeCurrent(engine->display, engine->surface, engine->surface, engine->context);
    eglQuerySurface(engine->display, engine->surface, EGL_WIDTH, &engine->width);
    eglQuerySurface(engine->display, engine->surface, EGL_HEIGHT, &engine->height);
    init_gl(engine);
    engine->running = 1;
    draw_frame(engine);
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    engine->running = 0;
    if (engine->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->context != EGL_NO_CONTEXT) eglDestroyContext(engine->display, engine->context);
        if (engine->surface != EGL_NO_SURFACE) eglDestroySurface(engine->display, engine->surface);
        eglTerminate(engine->display);
    }
    engine->display = EGL_NO_DISPLAY;
    engine->context = EGL_NO_CONTEXT;
    engine->surface = EGL_NO_SURFACE;
}

static void onDestroy(ANativeActivity* activity) {
    Engine* engine = (Engine*)activity->instance;
    free(engine);
}

void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    Engine* engine = malloc(sizeof(Engine));
    memset(engine, 0, sizeof(Engine));
    activity->instance = engine;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onDestroy = onDestroy;
}
