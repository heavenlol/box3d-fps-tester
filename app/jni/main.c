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
#include <pthread.h>

#define LOG_TAG "Box3D"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define ATLAS_W 512
#define ATLAS_H 512

typedef struct {
    ANativeWindow* window;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    GLuint program;
    GLuint position_loc;
    GLuint color_loc;
    GLuint mvp_loc;
    
    GLuint text_program;
    GLuint text_pos_loc;
    GLuint text_tex_loc;
    GLuint text_color_uniform;
    GLuint font_texture;
    stbtt_bakedchar baked_chars[96];

    struct timespec last_time;
    struct timespec start_time;
    int frame_count;
    float current_fps;
    int running;
    pthread_t render_thread;
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

static const char* text_vertex_shader =
    "attribute vec2 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "  v_texcoord = texcoord;\n"
    "}\n";

static const char* text_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "  float mask = texture2D(u_texture, v_texcoord).r;\n"
    "  gl_FragColor = vec4(u_color.rgb, u_color.a * mask);\n"
    "}\n";

static const unsigned char ttf_fallback[164] = {
    0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x20,0x00,0x00,0x00,0x04,0x63,0x6d,0x61,0x70,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x00,0x00,0x00,0x24,0x67,0x6c,0x79,0x66,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x00,0x00,0x00,0x24,0x68,0x65,0x61,0x64,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x84,0x00,0x00,0x00,0x36,0x00,0x01,0x00,0x00,
    0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x14,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x10,
    0x00,0x04,0x00,0x0c,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x01,0x00,0x00,
    0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x04,
    0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00
};

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

    GLuint text_vs = load_shader(GL_VERTEX_SHADER, text_vertex_shader);
    GLuint text_fs = load_shader(GL_FRAGMENT_SHADER, text_fragment_shader);
    engine->text_program = glCreateProgram();
    glAttachShader(engine->text_program, text_vs);
    glAttachShader(engine->text_program, text_fs);
    glLinkProgram(engine->text_program);
    engine->text_pos_loc = glGetAttribLocation(engine->text_program, "position");
    engine->text_tex_loc = glGetAttribLocation(engine->text_program, "texcoord");
    engine->text_color_uniform = glGetUniformLocation(engine->text_program, "u_color");

    unsigned char* atlas_pixels = (unsigned char*)malloc(ATLAS_W * ATLAS_H);
    memset(atlas_pixels, 0, ATLAS_W * ATLAS_H);
    
    unsigned char font_data[2048];
    memset(font_data, 0, sizeof(font_data));
    memcpy(font_data, ttf_fallback, sizeof(ttf_fallback));
    
    stbtt_BakeFontBitmap(font_data, 0, 32.0f, atlas_pixels, ATLAS_W, ATLAS_H, 32, 96, engine->baked_chars);

    glGenTextures(1, &engine->font_texture);
    glBindTexture(GL_TEXTURE_2D, engine->font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, ATLAS_W, ATLAS_H, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, atlas_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(atlas_pixels);

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

void draw_cube_let(Engine* engine, float tx, float ty, float tz, float* view_projection) {
    float vertices[] = {
        -0.15f+tx, -0.15f+ty,  0.15f+tz,  0.15f+tx, -0.15f+ty,  0.15f+tz,  0.15f+tx,  0.15f+ty,  0.15f+tz, -0.15f+tx,  0.15f+ty,  0.15f+tz,
        -0.15f+tx, -0.15f+ty, -0.15f+tz, -0.15f+tx,  0.15f+ty, -0.15f+tz,  0.15f+tx,  0.15f+ty, -0.15f+tz,  0.15f+tx, -0.15f+ty, -0.15f+tz,
        -0.15f+tx,  0.15f+ty, -0.15f+tz, -0.15f+tx,  0.15f+ty,  0.15f+tz,  0.15f+tx,  0.15f+ty,  0.15f+tz,  0.15f+tx,  0.15f+ty, -0.15f+tz,
        -0.15f+tx, -0.15f+ty, -0.15f+tz,  0.15f+tx, -0.15f+ty, -0.15f+tz,  0.15f+tx, -0.15f+ty,  0.15f+tz, -0.15f+tx, -0.15f+ty,  0.15f+tz,
         0.15f+tx, -0.15f+ty, -0.15f+tz,  0.15f+tx,  0.15f+ty, -0.15f+tz,  0.15f+tx,  0.15f+ty,  0.15f+tz,  0.15f+tx, -0.15f+ty,  0.15f+tz,
        -0.15f+tx, -0.15f+ty, -0.15f+tz, -0.15f+tx, -0.15f+ty,  0.15f+tz, -0.15f+tx,  0.15f+ty,  0.15f+tz, -0.15f+tx,  0.15f+ty, -0.15f+tz
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

void render_string(Engine* engine, const char* text, float x, float y) {
    glUseProgram(engine->text_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, engine->font_texture);
    glUniform4f(engine->text_color_uniform, 0.0f, 1.0f, 0.0f, 1.0f);

    float sx = 2.0f / engine->width;
    float sy = 2.0f / engine->height;

    while (*text) {
        if (*text >= 32 && *text < 128) {
            stbtt_bakedchar *b = engine->baked_chars + (*text - 32);
            int round_x = (int)floor(x + b->xoff);
            int round_y = (int)floor(y - b->yoff);
            
            float x0 = round_x * sx;
            float y0 = round_y * sy;
            float x1 = (round_x + b->x1 - b->x0) * sx;
            float y1 = (round_y - (b->y1 - b->y0)) * sy;

            float u0 = b->x0 / (float)ATLAS_W;
            float v0 = b->y0 / (float)ATLAS_H;
            float u1 = b->x1 / (float)ATLAS_W;
            float v1 = b->y1 / (float)ATLAS_H;

            float vertices[16] = {
                x0, y0, u0, v0,
                x1, y0, u1, v0,
                x0, y1, u0, v1,
                x1, y1, u1, v1
            };

            glVertexAttribPointer(engine->text_pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
            glEnableVertexAttribArray(engine->text_pos_loc);
            glVertexAttribPointer(engine->text_tex_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
            glEnableVertexAttribArray(engine->text_tex_loc);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            x += b->xadvance;
        }
        text++;
    }
}

void draw_frame(Engine* engine) {
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
    
    float angleX = time * 0.8f;
    float angleY = time * 0.5f;
    
    float cx = cosf(angleX), sx = sinf(angleX);
    float cy = cosf(angleY), sy = sinf(angleY);
    
    r[0] = cy; r[1] = sy * sx; r[2] = sy * cx;
    r[5] = cx; r[6] = -sx;
    r[8] = -sy; r[9] = cy * sx; r[10] = cy * cx;

    float offsets[8][3] = {
        {-0.16f,-0.16f,-0.16f}, {0.16f,-0.16f,-0.16f}, {-0.16f,0.16f,-0.16f}, {0.16f,0.16f,-0.16f},
        {-0.16f,-0.16f,0.16f},  {0.16f,-0.16f,0.16f},  {-0.16f,0.16f,0.16f},  {0.16f,0.16f,0.16f}
    };

    for (int i = 0; i < 8; i++) {
        draw_cube_let(engine, offsets[i][0], offsets[i][1], offsets[i][2], r);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    char out_str[32];
    snprintf(out_str, sizeof(out_str), "FPS: %.1f", engine->current_fps);
    render_string(engine, out_str, 20.0f, engine->height - 50.0f);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    eglSwapBuffers(engine->display, engine->surface);
}

static void* thread_routing_loop(void* context) {
    Engine* engine = (Engine*)context;
    
    const EGLint attribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE };
    EGLint numConfigs;
    EGLConfig config;
    engine->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(engine->display, 0, 0);
    eglChooseConfig(engine->display, attribs, &config, 1, &numConfigs);
    engine->surface = eglCreateWindowSurface(engine->display, config, engine->window, NULL);
    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    engine->context = eglCreateContext(engine->display, config, NULL, contextAttribs);
    eglMakeCurrent(engine->display, engine->surface, engine->surface, engine->context);
    eglQuerySurface(engine->display, engine->surface, EGL_WIDTH, &engine->width);
    eglQuerySurface(engine->display, engine->surface, EGL_HEIGHT, &engine->height);
    
    init_gl(engine);

    while (engine->running) {
        calculate_fps(engine);
        draw_frame(engine);
    }

    eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(engine->display, engine->context);
    eglDestroySurface(engine->display, engine->surface);
    eglTerminate(engine->display);
    return NULL;
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    engine->window = window;
    engine->running = 1;
    pthread_create(&engine->render_thread, NULL, thread_routing_loop, engine);
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window) {
    Engine* engine = (Engine*)activity->instance;
    engine->running = 0;
    pthread_join(engine->render_thread, NULL);
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
