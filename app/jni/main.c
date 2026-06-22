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
#define GRAPH_SAMPLES 60

static const unsigned char font_atlas_bits[128] = {
    0x00, 0x00, 0x00, 0x00, 0xbd, 0x00, 0xb6, 0x00, 0x38, 0x00, 0x6c, 0x00, 0x6c, 0x00, 0x38, 0x00,
    0x38, 0x00, 0x6c, 0x00, 0x6c, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x36, 0x00, 0x7c, 0x00, 0x66, 0x00, 0x60, 0x00, 0x3c, 0x00, 0x06, 0x00,
    0x3e, 0x00, 0x66, 0x00, 0x3c, 0x00, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x66, 0x00, 0x6e, 0x00, 0x7e, 0x00, 0x76, 0x00, 0x62, 0x00, 0x3c, 0x00, 0x7c, 0x00,
    0x66, 0x00, 0x7c, 0x00, 0x66, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7c, 0x00, 0x62, 0x00, 0x62, 0x00, 0x62, 0x00, 0x62, 0x00, 0x62, 0x00, 0x7c, 0x00, 0x7c, 0x00,
    0x60, 0x00, 0x78, 0x00, 0x60, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct {
    ANativeWindow* window;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    GLuint shader_program;
    GLuint hud_program;
    GLuint position_loc;
    GLuint color_loc;
    GLuint mvp_loc;
    GLuint hud_pos_loc;
    GLuint hud_tex_loc;
    GLuint hud_color_loc;
    GLuint font_texture;
    struct timespec last_time;
    struct timespec start_time;
    int frame_count;
    float current_fps;
    float frame_times[GRAPH_SAMPLES];
    int graph_index;
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

static const char* hud_vertex_shader =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "  v_texcoord = a_texcoord;\n"
    "}\n";

static const char* hud_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_color;\n"
    "uniform bool u_use_texture;\n"
    "void main() {\n"
    "  if (u_use_texture) {\n"
    "    float alpha = texture2D(u_texture, v_texcoord).r;\n"
    "    gl_FragColor = vec4(u_color.rgb, alpha * u_color.a);\n"
    "  } else {\n"
    "    gl_FragColor = u_color;\n"
    "  }\n"
    "}\n";

GLuint build_program(const char* v_src, const char* f_src) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &v_src, NULL);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &f_src, NULL);
    glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    return prog;
}

void init_gl(Engine* engine) {
    engine->shader_program = build_program(vertex_shader_source, fragment_shader_source);
    engine->position_loc = glGetAttribLocation(engine->shader_program, "position");
    engine->color_loc = glGetAttribLocation(engine->shader_program, "color");
    engine->mvp_loc = glGetUniformLocation(engine->shader_program, "u_mvp");

    engine->hud_program = build_program(hud_vertex_shader, hud_fragment_shader);
    engine->hud_pos_loc = glGetAttribLocation(engine->hud_program, "a_position");
    engine->hud_tex_loc = glGetAttribLocation(engine->hud_program, "a_texcoord");
    engine->hud_color_loc = glGetUniformLocation(engine->hud_program, "u_color");

    glGenTextures(1, &engine->font_texture);
    glBindTexture(GL_TEXTURE_2D, engine->font_texture);
    
    unsigned char pixels[16 * 16];
    memset(pixels, 0, sizeof(pixels));
    for (int i = 0; i < 128; i++) {
        int r = i / 16;
        int c = i % 16;
        pixels[r * 16 + c] = font_atlas_bits[i];
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 16, 16, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    clock_gettime(CLOCK_MONOTONIC, &engine->last_time);
    clock_gettime(CLOCK_MONOTONIC, &engine->start_time);
    engine->frame_count = 0;
    engine->current_fps = 0.0f;
    engine->graph_index = 0;
    memset(engine->frame_times, 0, sizeof(engine->frame_times));
    glEnable(GL_DEPTH_TEST);
}

void calculate_fps(Engine* engine, float* out_dt_ms) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    engine->frame_count++;
    
    double elapsed = (current_time.tv_sec - engine->last_time.tv_sec) + 
                     (current_time.tv_nsec - engine->last_time.tv_nsec) / 1000000000.0;
                     
    *out_dt_ms = (float)(elapsed * 1000.0);
    
    if (elapsed >= 0.25) {
        engine->current_fps = (float)(engine->frame_count / elapsed);
        engine->frame_count = 0;
        engine->last_time = current_time;
    }
    
    engine->frame_times[engine->graph_index] = *out_dt_ms;
    engine->graph_index = (engine->graph_index + 1) % GRAPH_SAMPLES;
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

void perspective_matrix(float* m, float fov, float aspect, float near, float far) {
    float f = 1.0f / tanf(fov * 0.5f * 3.14159265f / 180.0f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = (2.0f * far * near) / (near - far);
    m[15] = 0.0f;
}

void draw_cube_let(Engine* engine, float tx, float ty, float tz, float* view_projection) {
    float s = 0.18f;
    float vertices[] = {
        -s+tx, -s+ty,  s+tz,  s+tx, -s+ty,  s+tz,  s+tx,  s+ty,  s+tz, -s+tx,  s+ty,  s+tz,
        -s+tx, -s+ty, -s+tz, -s+tx,  s+ty, -s+tz,  s+tx,  s+ty, -s+tz,  s+tx, -s+ty, -s+tz,
        -s+tx,  s+ty, -s+tz, -s+tx,  s+ty,  s+tz,  s+tx,  s+ty,  s+tz,  s+tx,  s+ty, -s+tz,
        -s+tx, -s+ty, -s+tz,  s+tx, -s+ty, -s+tz,  s+tx, -s+ty,  s+tz, -s+tx, -s+ty,  s+tz,
         s+tx, -s+ty, -s+tz,  s+tx,  s+ty, -s+tz,  s+tx,  s+ty,  s+tz,  s+tx, -s+ty,  s+tz,
        -s+tx, -s+ty, -s+tz, -s+tx, -s+ty,  s+tz, -s+tx,  s+ty,  s+tz, -s+tx,  s+ty, -s+tz
    };

    float colors[] = {
        0.0f,0.438f,0.75f,1.0f, 0.0f,0.438f,0.75f,1.0f, 0.0f,0.438f,0.75f,1.0f, 0.0f,0.438f,0.75f,1.0f,
        0.0f,0.616f,0.301f,1.0f, 0.0f,0.616f,0.301f,1.0f, 0.0f,0.616f,0.301f,1.0f, 0.0f,0.616f,0.301f,1.0f,
        0.725f,0.0f,0.0f,1.0f, 0.725f,0.0f,0.0f,1.0f, 0.725f,0.0f,0.0f,1.0f, 0.725f,0.0f,0.0f,1.0f,
        0.886f,0.345f,0.133f,1.0f, 0.886f,0.345f,0.133f,1.0f, 0.886f,0.345f,0.133f,1.0f, 0.886f,0.345f,0.133f,1.0f,
        0.956f,0.718f,0.118f,1.0f, 0.956f,0.718f,0.118f,1.0f, 0.956f,0.718f,0.118f,1.0f, 0.956f,0.718f,0.118f,1.0f,
        0.9f,0.9f,0.9f,1.0f, 0.9f,0.9f,0.9f,1.0f, 0.9f,0.9f,0.9f,1.0f, 0.9f,0.9f,0.9f,1.0f
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

void draw_text(Engine* engine, const char* str, float x, float y, float size_w, float size_h) {
    glUniform1i(glGetUniformLocation(engine->hud_program, "u_use_texture"), 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, engine->font_texture);
    glUniform1i(glGetUniformLocation(engine->hud_program, "u_texture"), 0);

    float cx = x;
    while (*str) {
        char c = *str++;
        int idx = 0;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
        else if (c == ':') idx = 36;
        else if (c == '.') idx = 37;
        else if (c == ' ') { cx += size_w; continue; }

        float tx = (idx % 16) / 16.0f;
        float ty = (idx / 16) / 16.0f;
        float tw = 1.0f / 16.0f;

        float vertices[] = {
            cx, y,          tx, ty + tw,
            cx + size_w, y,  tx + tw, ty + tw,
            cx, y + size_h, tx, ty,
            cx + size_w, y + size_h, tx + tw, ty
        };

        glVertexAttribPointer(engine->hud_pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), vertices);
        glEnableVertexAttribArray(engine->hud_pos_loc);
        glVertexAttribPointer(engine->hud_tex_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
        glEnableVertexAttribArray(engine->hud_tex_loc);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        cx += size_w * 1.1f;
    }
}

void draw_mangohud(Engine* engine, int fps, float dt_ms) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(engine->hud_program);

    glUniform1i(glGetUniformLocation(engine->hud_program, "u_use_texture"), 0);
    float bg_vertices[] = {
        -0.95f, 0.45f,
         0.45f, 0.45f,
        -0.95f, 0.95f,
         0.45f, 0.95f
    };
    glUniform4f(engine->hud_color_loc, 0.08f, 0.08f, 0.08f, 0.85f);
    glVertexAttribPointer(engine->hud_pos_loc, 2, GL_FLOAT, GL_FALSE, 0, bg_vertices);
    glEnableVertexAttribArray(engine->hud_pos_loc);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    char buf[64];
    glUniform4f(engine->hud_color_loc, 0.0f, 0.9f, 0.4f, 1.0f);
    draw_text(engine, "ADRENO TM 710", -0.92f, 0.88f, 0.04f, 0.05f);

    snprintf(buf, sizeof(buf), "FPS: %d", fps);
    glUniform4f(engine->hud_color_loc, 0.0f, 0.85f, 0.95f, 1.0f);
    draw_text(engine, buf, -0.92f, 0.80f, 0.04f, 0.05f);

    snprintf(buf, sizeof(buf), "FRAMETIME: %.1f MS", dt_ms);
    glUniform4f(engine->hud_color_loc, 0.9f, 0.9f, 0.2f, 1.0f);
    draw_text(engine, buf, -0.92f, 0.72f, 0.035f, 0.045f);

    float graph_left = -0.92f;
    float graph_width = 1.3f;
    float graph_bottom = 0.48f;
    float graph_height = 0.18f;

    glUniform1i(glGetUniformLocation(engine->hud_program, "u_use_texture"), 0);
    glUniform4f(engine->hud_color_loc, 0.0f, 0.9f, 0.4f, 1.0f);

    float dx = graph_width / (GRAPH_SAMPLES - 1);
    for (int i = 0; i < GRAPH_SAMPLES - 1; i++) {
        int idx1 = (engine->graph_index + i) % GRAPH_SAMPLES;
        int idx2 = (engine->graph_index + i + 1) % GRAPH_SAMPLES;

        float val1 = engine->frame_times[idx1] / 33.33f;
        float val2 = engine->frame_times[idx2] / 33.33f;
        if (val1 > 1.0f) val1 = 1.0f;
        if (val2 > 1.0f) val2 = 1.0f;

        float x1 = graph_left + i * dx;
        float y1 = graph_bottom + val1 * graph_height;
        float x2 = graph_left + (i + 1) * dx;
        float y2 = graph_bottom + val2 * graph_height;

        float segment[] = {x1, y1, x2, y2};
        glVertexAttribPointer(engine->hud_pos_loc, 2, GL_FLOAT, GL_FALSE, 0, segment);
        glDrawArrays(GL_LINES, 0, 2);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void draw_frame(Engine* engine) {
    float dt_ms = 0.0f;
    calculate_fps(engine, &dt_ms);

    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glUseProgram(engine->shader_program);

    struct timespec curr;
    clock_gettime(CLOCK_MONOTONIC, &curr);
    float time = (curr.tv_sec - engine->start_time.tv_sec) + (curr.tv_nsec - engine->start_time.tv_nsec) / 1000000000.0f;

    float projection[16], modelview[16], mvp[16];
    float aspect = (float)engine->width / (float)engine->height;
    perspective_matrix(projection, 45.0f, aspect, 0.1f, 10.0f);

    make_identity(modelview);
    float angleX = time * 0.5f;
    float angleY = time * 0.6f;
    
    float cx = cosf(angleX), sx = sinf(angleX);
    float cy = cosf(angleY), sy = sinf(angleY);
    
    modelview[0] = cy; modelview[1] = sy * sx; modelview[2] = sy * cx;
    modelview[5] = cx; modelview[6] = -sx;
    modelview[8] = -sy; modelview[9] = cy * sx; modelview[10] = cy * cx;
    modelview[14] = -2.5f;

    multiply_matrix(mvp, projection, modelview);

    float gap = 0.22f;
    float offsets[8][3] = {
        {-gap,-gap,-gap}, {gap,-gap,-gap}, {-gap,gap,-gap}, {gap,gap,-gap},
        {-gap,-gap,gap},  {gap,-gap,gap},  {-gap,gap,gap},  {gap,gap,gap}
    };

    for (int i = 0; i < 8; i++) {
        draw_cube_let(engine, offsets[i][0], offsets[i][1], offsets[i][2], mvp);
    }

    draw_mangohud(engine, (int)engine->current_fps, dt_ms);

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
    
    eglSwapInterval(engine->display, 0);
    init_gl(engine);

    while (engine->running) {
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
