#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

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
    GLuint mvp_loc;
    struct timespec last_time;
    struct timespec start_time;
    int frame_count;
    float current_fps;
    int running;
} Engine;

static const char* vertex_shader_source =
    "uniform mat4 u_mvp;\n"
    "attribute vec4 position;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "  gl_Position = u_mvp * position;\n"
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
    engine->mvp_loc = glGetUniformLocation(engine->program, "u_mvp");
    clock_gettime(CLOCK_MONOTONIC, &engine->last_time);
    clock_gettime(CLOCK_MONOTONIC, &engine->start_time);
    engine->frame_count = 0;
    engine->current_fps = 0.0f;
    glEnable(GL_DEPTH_TEST);
}

void calculate_fps(Engine* engine) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    engine->frame_count++;
    double elapsed = (current_time.tv_sec - engine->last_time.tv_sec) + 
                     (current_time.tv_nsec - engine->last_time.tv_nsec) / 1000000000.0;
    if (elapsed >= 0.5) {
        engine->current_fps = (float)(engine->frame_count / elapsed);
        engine->frame_count = 0;
        engine->last_time = current_time;
    }
}

void make_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void multiply_matrix(float* res, float* a, float* b) {
    float tmp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i * 4 + j] = a[i * 4 + 0] * b[0 + j] +
                             a[i * 4 + 1] * b[4 + j] +
                             a[i * 4 + 2] * b[8 + j] +
                             a[i * 4 + 3] * b[12 + j];
        }
    }
    memcpy(res, tmp, 16 * sizeof(float));
}

void draw_cube_let(Engine* engine, float tx, float ty, float tz, float* view_projection) {
    float vertices[] = {
        -0.2f+tx, -0.2f+ty,  0.2f+tz,  0.2f+tx, -0.2f+ty,  0.2f+tz,  0.2f+tx,  0.2f+ty,  0.2f+tz, -0.2f+tx,  0.2f+ty,  0.2f+tz,
        -0.2f+tx, -0.2f+ty, -0.2f+tz, -0.2f+tx,  0.2f+ty, -0.2f+tz,  0.2f+tx,  0.2f+ty, -0.2f+tz,  0.2f+tx, -0.2f+ty, -0.2f+tz,
        -0.2f+tx,  0.2f+ty, -0.2f+tz, -0.2f+tx,  0.2f+ty,  0.2f+tz,  0.2f+tx,  0.2f+ty,  0.2f+tz,  0.2f+tx,  0.2f+ty, -0.2f+tz,
        -0.2f+tx, -0.2f+ty, -0.2f+tz,  0.2f+tx, -0.2f+ty, -0.2f+tz,  0.2f+tx, -0.2f+ty,  0.2f+tz, -0.2f+tx, -0.2f+ty,  0.2f+tz,
         0.2f+tx, -0.2f+ty, -0.2f+tz,  0.2f+tx,  0.2f+ty, -0.2f+tz,  0.2f+tx,  0.2f+ty,  0.2f+tz,  0.2f+tx, -0.2f+ty,  0.2f+tz,
        -0.2f+tx, -0.2f+ty, -0.2f+tz, -0.2f+tx, -0.2f+ty,  0.2f+tz, -0.2f+tx,  0.2f+ty,  0.2f+tz, -0.2f+tx,  0.2f+ty, -0.2f+tz
    };

    float colors[] = {
        1.0f,0.0f,0.0f,1.0f, 1.0f,0.0f,0.0f,1.0f, 1.0f,0.0f,0.0f,1.0f, 1.0f,0.0f,0.0f,1.0f,
        0.0f,0.0f,1.0f,1.0f, 0.0f,0.0f,1.0f,1.0f, 0.0f,0.0f,1.0f,1.0f, 0.0f,0.0f,1.0f,1.0f,
        1.0f,1.0f,1.0f,1.0f, 1.0f,1.0f,1.0f,1.0f, 1.0f,1.0f,1.0f,1.0f, 1.0f,1.0f,1.0f,1.0f,
        1.0f,0.5f,0.0f,1.0f, 1.0f,0.5f,0.0f,1.0f, 1.0f,0.5f,0.0f,1.0f, 1.0f,0.5f,0.0f,1.0f,
        1.0f,1.0f,0.0f,1.0f, 1.0f,1.0f,0.0f,1.0f, 1.0f,1.0f,0.0f,1.0f, 1.0f,1.0f,0.0f,1.0f,
        0.0f,1.0f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f
    };

    GLushort indices[] = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
    };

    glUniformMatrix4fv(engine->mvp_loc, 1, GL_FALSE, view_projection);
    glVertexAttribPointer(engine->position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(engine->position_loc);
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, colors);
    glEnableVertexAttribArray(engine->color_loc);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, indices);
}

void draw_hud_digit(Engine* engine, int digit, float x, float y) {
    float w = 0.03f, h = 0.06f;
    float segments[7][12] = {
        {x,y+h,0, x+w,y+h,0}, {x+w,y+h,0, x+w,y+h/2,0}, {x+w,y+h/2,0, x+w,y,0},
        {x,y,0, x+w,y,0}, {x,y,0, x,y+h/2,0}, {x,y+h/2,0, x,y+h,0}, {x,y+h/2,0, x+w,y+h/2,0}
    };
    int patterns[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
        {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
    };
    float color[24] = {0.0f,1.0f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f};
    float identity[16];
    make_identity(identity);
    glUniformMatrix4fv(engine->mvp_loc, 1, GL_FALSE, identity);
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, color);
    glEnableVertexAttribArray(engine->color_loc);

    for (int i = 0; i < 7; i++) {
        if (patterns[digit][i]) {
            glVertexAttribPointer(engine->position_loc, 3, GL_FLOAT, GL_FALSE, 0, segments[i]);
            glEnableVertexAttribArray(engine->position_loc);
            glDrawArrays(GL_LINES, 0, 2);
        }
    }
}

void draw_frame(Engine* engine) {
    if (engine->display == NULL || engine->surface == NULL) return;
    calculate_fps(engine);
    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(engine->program);

    struct timespec curr;
    clock_gettime(CLOCK_MONOTONIC, &curr);
    float time = (curr.tv_sec - engine->start_time.tv_sec) + (curr.tv_nsec - engine->start_time.tv_nsec) / 1000000000.0f;

    float r[16], identity[16];
    make_identity(identity);
    memcpy(r, identity, 16*sizeof(float));
    float angle = time * 1.0f;
    float c = cosf(angle), s = sinf(angle);
    r[0] = c; r[2] = s; r[5] = c; r[6] = -s; r[8] = -s; r[10] = c; // Custom 3D rotation layout

    float offsets[8][3] = {
        {-0.22f,-0.22f,-0.22f}, {0.22f,-0.22f,-0.22f}, {-0.22f,0.22f,-0.22f}, {0.22f,0.22f,-0.22f},
        {-0.22f,-0.22f,0.22f},  {0.22f,-0.22f,0.22f},  {-0.22f,0.22f,0.22f},  {0.22f,0.22f,0.22f}
    };

    for (int i = 0; i < 8; i++) {
        draw_cube_let(engine, offsets[i][0], offsets[i][1], offsets[i][2], r);
    }

    int fps = (int)engine->current_fps;
    draw_hud_digit(engine, (fps / 100) % 10, -0.9f, 0.8f);
    draw_hud_digit(engine, (fps / 10) % 10, -0.85f, 0.8f);
    draw_hud_digit(engine, fps % 10, -0.8f, 0.8f);

    eglSwapBuffers(engine->display, engine->surface);
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    const EGLint attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE };
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
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    engine->running = 0;
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
