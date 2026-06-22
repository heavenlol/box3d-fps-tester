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

#define GL_CHECK(x) \
    x; \
    { \
        GLenum glError = glGetError(); \
        if (glError != GL_NO_ERROR) { \
            LOGI("glGetError() = %i (0x%.8x) at %s:%i\n", glError, glError, __FILE__, __LINE__); \
        } \
    }

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

static const unsigned char font_bits[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0xfa,0x00,0x00},{0x00,0xe0,0x00,0xe0,0x00},{0x28,0xfe,0x28,0xfe,0x28},{0x24,0x54,0xfe,0x54,0x48},{0xc4,0xc8,0x10,0x26,0x46},{0x6c,0x92,0xaa,0x44,0x0a},{0x00,0xa0,0xc0,0x00,0x00},{0x00,0x38,0x44,0x82,0x00},{0x00,0x82,0x44,0x38,0x00},{0x28,0x10,0x7c,0x10,0x28},{0x10,0x10,0x7c,0x10,0x10},{0x00,0x0a,0x0c,0x00,0x00},{0x10,0x10,0x10,0x10,0x10},{0x00,0x06,0x06,0x00,0x00},{0x04,0x08,0x10,0x20,0x40},
    {0x7c,0x8a,0x92,0xa2,0x7c},{0x00,0x42,0xfe,0x02,0x00},{0x42,0x86,0x8a,0x92,0x62},{0x84,0x82,0xa2,0xd2,0x8c},{0x18,0x28,0x48,0xfe,0x08},{0xe4,0xa2,0xa2,0xa2,0x9c},{0x3c,0x52,0x92,0x92,0x0c},{0x80,0x86,0x98,0xe0,0x80},{0x6c,0x92,0x92,0x92,0x6c},{0x60,0x92,0x92,0x94,0x78},{0x00,0x6c,0x6c,0x00,0x00},{0x00,0xaa,0x6c,0x00,0x00},{0x10,0x28,0x44,0x82,0x00},{0x24,0x24,0x24,0x24,0x24},{0x00,0x82,0x44,0x28,0x10},{0x40,0x80,0x8a,0x90,0x60},
    {0x4c,0x92,0x9e,0x82,0x7c},{0x7e,0x88,0x88,0x88,0x7e},{0xfe,0x92,0x92,0x92,0x6c},{0x7c,0x82,0x82,0x82,0x44},{0xfe,0x82,0x82,0x44,0x38},{0xfe,0x92,0x92,0x92,0x82},{0xfe,0x90,0x90,0x90,0x80},{0x7c,0x82,0x92,0x92,0x5e},{0xfe,0x10,0x10,0x10,0xfe},{0x00,0x82,0xfe,0x82,0x00},{0x06,0x02,0x82,0xfe,0x80},{0xfe,0x10,0x28,0x44,0x82},{0xfe,0x02,0x02,0x02,0x02},{0xfe,0x40,0x30,0x40,0xfe},{0xfe,0x20,0x10,0x08,0xfe},{0x7c,0x82,0x82,0x82,0x7c},
    {0xfe,0x90,0x90,0x90,0x60},{0x7c,0x82,0x8a,0x84,0x7a},{0xfe,0x90,0x98,0x94,0x62},{0x62,0x92,0x92,0x92,0x8c},{0x80,0x80,0xfe,0x80,0x80},{0xfc,0x02,0x02,0x02,0xfc},{0xf8,0x04,0x02,0x04,0xf8},{0xfc,0x02,0x3c,0x02,0xfc},{0xc6,0x28,0x10,0x28,0xc6},{0xe0,0x10,0x0e,0x10,0xe0},{0x86,0x8a,0x92,0xa2,0xc2},{0x00,0xfe,0x82,0x82,0x00},{0x40,0x20,0x10,0x08,0x04},{0x00,0x82,0x82,0xfe,0x00},{0x20,0x40,0x80,0x40,0x20},{0x02,0x02,0x02,0x02,0x02},
    {0x00,0x01,0x02,0x04,0x00},{0x04,0x2a,0x2a,0x2a,0x1f},{0xfe,0x12,0x22,0x22,0x1c},{0x1c,0x22,0x22,0x22,0x04},{0x1c,0x22,0x22,0x12,0xfe},{0x1c,0x2a,0x2a,0x2a,0x18},{0x10,0x7e,0x90,0x80,0x40},{0x30,0x4a,0x4a,0x4a,0x7c},{0xfe,0x10,0x20,0x20,0xdf},{0x00,0x22,0xbe,0x02,0x00},{0x04,0x02,0x22,0xbc,0x00},{0xfe,0x08,0x14,0x22,0x00},{0x00,0x82,0xfe,0x02,0x00},{0x3e,0x20,0x18,0x20,0x1e},{0x3e,0x10,0x20,0x20,0x1f},{0x1c,0x22,0x22,0x22,0x1c},
    {0x3f,0x24,0x24,0x24,0x18},{0x18,0x24,0x24,0x24,0x3f},{0x3e,0x10,0x20,0x20,0x10},{0x12,0x2a,0x2a,0x2a,0x24},{0x10,0x7c,0x12,0x02,0x04},{0x3e,0x02,0x02,0x02,0x3e},{0x38,0x06,0x01,0x06,0x38},{0x3c,0x02,0x1c,0x02,0x3c},{0x22,0x14,0x08,0x14,0x22},{0x30,0x0a,0x0a,0x0a,0x3c},{0x22,0x26,0x2a,0x32,0x22},{0x10,0x10,0x6c,0x82,0x00},{0x00,0x00,0xfe,0x00,0x00},{0x00,0x82,0x6c,0x10,0x10},{0x10,0x10,0x28,0x44,0x00},{0x00,0x00,0x00,0x00,0x00}
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

    int tex_w = 128;
    int tex_h = 64;
    unsigned char* tex_data = (unsigned char*)malloc(tex_w * tex_h);
    memset(tex_data, 0, tex_w * tex_h);

    for (int i = 0; i < 96; i++) {
        int cx = (i % 16) * 8;
        int cy = (i / 16) * 10;
        for (int x = 0; x < 5; x++) {
            unsigned char bits = font_bits[i][x];
            for (int y = 0; y < 8; y++) {
                if ((bits >> y) & 1) {
                    int px = cx + x;
                    int py = cy + (7 - y);
                    tex_data[py * tex_w + px] = 255;
                }
            }
        }
    }

    GL_CHECK(glGenTextures(1, &engine->font_texture));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, engine->font_texture));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, tex_w, tex_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, tex_data));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    free(tex_data);

    clock_gettime(CLOCK_MONOTONIC, &engine->last_time);
    clock_gettime(CLOCK_MONOTONIC, &engine->start_time);
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

    GL_CHECK(glUniformMatrix4fv(engine->mvp_loc, 1, GL_FALSE, view_projection));
    GL_CHECK(glVertexAttribPointer(engine->position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertices));
    GL_CHECK(glEnableVertexAttribArray(engine->position_loc));
    GL_CHECK(glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, colors);)
    GL_CHECK(glEnableVertexAttribArray(engine->color_loc));
    GL_CHECK(glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, indices));
}

void render_string(Engine* engine, const char* text, float screen_x, float screen_y, float scale) {
    GL_CHECK(glUseProgram(engine->text_program));
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, engine->font_texture));
    GL_CHECK(glUniform4f(engine->text_color_uniform, 0.0f, 1.0f, 1.0f, 1.0f));

    float char_w_pixels = 6.0f * scale;
    float char_h_pixels = 9.0f * scale;
    float step_x = char_w_pixels / (engine->width / 2.0f);
    
    float start_x = (screen_x / (engine->width / 2.0f)) - 1.0f;
    float start_y = (screen_y / (engine->height / 2.0f)) - 1.0f;
    
    float w = char_w_pixels / (engine->width / 2.0f);
    float h = char_h_pixels / (engine->height / 2.0f);

    int tex_w = 128;
    int tex_h = 64;

    while (*text) {
        int ascii = (int)(*text);
        if (ascii >= 32 && ascii < 128) {
            int idx = ascii - 32;
            int cx = (idx % 16) * 8;
            int cy = (idx / 16) * 10;

            float u0 = (float)cx / tex_w;
            float v0 = (float)cy / tex_h;
            float u1 = (float)(cx + 6) / tex_w;
            float v1 = (float)(cy + 9) / tex_h;

            float vertices[16] = {
                start_x,     start_y + h, u0, v0,
                start_x + w, start_y + h, u1, v0,
                start_x,     start_y,     u0, v1,
                start_x + w, start_y,     u1, v1
            };

            GL_CHECK(glVertexAttribPointer(engine->text_pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices));
            GL_CHECK(glEnableVertexAttribArray(engine->text_pos_loc));
            GL_CHECK(glVertexAttribPointer(engine->text_tex_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]));
            GL_CHECK(glEnableVertexAttribArray(engine->text_tex_loc));
            GL_CHECK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

            start_x += step_x;
        }
        text++;
    }
}

void draw_frame(Engine* engine) {
    GL_CHECK(glViewport(0, 0, engine->width, engine->height));
    GL_CHECK(glClearColor(0.2f, 0.2f, 0.2f, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glUseProgram(engine->program));

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

    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    char out_str[64];
    snprintf(out_str, sizeof(out_str), "Adreno (TM) 710");
    render_string(engine, out_str, 40.0f, engine->height - 80.0f, 6.0f);

    snprintf(out_str, sizeof(out_str), "FPS: %.1f", engine->current_fps);
    render_string(engine, out_str, 40.0f, engine->height - 160.0f, 6.0f);

    GL_CHECK(glDisable(GL_BLEND));

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
