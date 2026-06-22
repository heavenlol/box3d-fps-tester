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

void draw_hud_digit(Engine* engine, int digit, float x, float y, float w, float h, float r, float g, float b) {
    float segments[7][6] = {
        {x,y+h, x+w,y+h}, {x+w,y+h, x+w,y+h/2}, {x+w,y+h/2, x+w,y},
        {x,y, x+w,y}, {x,y, x,y+h/2}, {x,y+h/2, x,y+h}, {x,y+h/2, x+w,y+h/2}
    };
    int patterns[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
        {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
    };
    float color[8] = {r,g,b,1.0f, r,g,b,1.0f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, color);
    glEnableVertexAttribArray(engine->color_loc);

    for (int i = 0; i < 7; i++) {
        if (patterns[digit][i]) {
            glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, segments[i]);
            glEnableVertexAttribArray(engine->position_loc);
            glDrawArrays(GL_LINES, 0, 2);
        }
    }
}

void draw_hud_label_fps(Engine* engine, float x, float y, float w, float h) {
    float space = w * 1.5f;
    float f_seg[][4] = {{x,y, x,y+h}, {x,y+h, x+w,y+h}, {x,y+h/2, x+w,y+h/2}};
    float color[8] = {0.0f,0.9f,0.4f,1.0f, 0.0f,0.9f,0.4f,1.0f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, color);
    glEnableVertexAttribArray(engine->color_loc);
    for(int i=0; i<3; i++) { glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, f_seg[i]); glDrawArrays(GL_LINES, 0, 2); }
    
    x += space;
    float p_seg[][4] = {{x,y, x,y+h}, {x,y+h, x+w,y+h}, {x,y+h/2, x+w,y+h/2}, {x+w,y+h/2, x+w,y+h}};
    for(int i=0; i<4; i++) { glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, p_seg[i]); glDrawArrays(GL_LINES, 0, 2); }
    
    x += space;
    float s_seg[][4] = {{x,y+h, x+w,y+h}, {x,y+h, x,y+h/2}, {x,y+h/2, x+w,y+h/2}, {x+w,y+h/2, x+w,y}, {x+w,y, x,y}};
    for(int i=0; i<5; i++) { glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, s_seg[i]); glDrawArrays(GL_LINES, 0, 2); }
}

void draw_mangohud(Engine* engine, int fps, float dt_ms) {
    glDisable(GL_DEPTH_TEST);
    float identity[16];
    make_identity(identity);
    glUniformMatrix4fv(engine->mvp_loc, 1, GL_FALSE, identity);

    float box_vertices[] = {
        -0.95f, 0.95f,   -0.45f, 0.95f,
        -0.45f, 0.95f,   -0.45f, 0.45f,
        -0.45f, 0.45f,   -0.95f, 0.45f,
        -0.95f, 0.45f,   -0.95f, 0.95f
    };
    float box_color[] = {0.1f,0.1f,0.1f,0.8f, 0.1f,0.1f,0.1f,0.8f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, box_color);
    glEnableVertexAttribArray(engine->color_loc);
    glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, box_vertices);
    glEnableVertexAttribArray(engine->position_loc);
    glDrawArrays(GL_LINES, 0, 8);

    draw_hud_label_fps(engine, -0.92f, 0.85f, 0.02f, 0.04f);

    float text_r = 0.0f, text_g = 0.85f, text_b = 0.95f;
    draw_hud_digit(engine, (fps / 100) % 10, -0.75f, 0.85f, 0.02f, 0.04f, text_r, text_g, text_b);
    draw_hud_digit(engine, (fps / 10) % 10, -0.71f, 0.85f, 0.02f, 0.04f, text_r, text_g, text_b);
    draw_hud_digit(engine, fps % 10, -0.67f, 0.85f, 0.02f, 0.04f, text_r, text_g, text_b);

    int ms_int = (int)dt_ms;
    int ms_frac = (int)((dt_ms - ms_int) * 10);
    draw_hud_digit(engine, (ms_int / 10) % 10, -0.92f, 0.77f, 0.015f, 0.03f, 0.9f, 0.9f, 0.2f);
    draw_hud_digit(engine, ms_int % 10, -0.89f, 0.77f, 0.015f, 0.03f, 0.9f, 0.9f, 0.2f);
    
    float dot[] = {-0.865f, 0.77f, -0.865f, 0.775f};
    float dot_color[] = {0.9f,0.9f,0.2f,1.0f, 0.9f,0.9f,0.2f,1.0f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, dot_color);
    glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, dot);
    glDrawArrays(GL_LINES, 0, 2);
    
    draw_hud_digit(engine, ms_frac % 10, -0.85f, 0.77f, 0.015f, 0.03f, 0.9f, 0.9f, 0.2f);

    float graph_left = -0.92f;
    float graph_width = 0.44f;
    float graph_bottom = 0.50f;
    float graph_height = 0.20f;

    float axis_vertices[] = {
        graph_left, graph_bottom, graph_left + graph_width, graph_bottom,
        graph_left, graph_bottom, graph_left, graph_bottom + graph_height
    };
    float axis_color[] = {0.4f,0.4f,0.4f,1.0f, 0.4f,0.4f,0.4f,1.0f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, axis_color);
    glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, axis_vertices);
    glDrawArrays(GL_LINES, 0, 4);

    float line_color[] = {0.0f,0.9f,0.4f,1.0f, 0.0f,0.9f,0.4f,1.0f};
    glVertexAttribPointer(engine->color_loc, 4, GL_FLOAT, GL_FALSE, 0, line_color);

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
        glVertexAttribPointer(engine->position_loc, 2, GL_FLOAT, GL_FALSE, 0, segment);
        glDrawArrays(GL_LINES, 0, 2);
    }
    glEnable(GL_DEPTH_TEST);
}

void draw_frame(Engine* engine) {
    float dt_ms = 0.0f;
    calculate_fps(engine, &dt_ms);

    glViewport(0, 0, engine->width, engine->height);
    glClearColor(0.35f, 0.35f, 0.35f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(engine->program);

    struct timespec curr;
    clock_gettime(CLOCK_MONOTONIC, &curr);
    float time = (curr.tv_sec - engine->start_time.tv_sec) + (curr.tv_nsec - engine->start_time.tv_nsec) / 1000000000.0f;

    float projection[16], modelview[16], mvp[16];
    float aspect = (float)engine->width / (float)engine->height;
    perspective_matrix(projection, 45.0f, aspect, 0.1f, 10.0f);

    make_identity(modelview);
    float angleX = time * 0.7f;
    float angleY = time * 0.9f;
    
    float cx = cosf(angleX), sx = sinf(angleX);
    float cy = cosf(angleY), sy = sinf(angleY);
    
    modelview[0] = cy; modelview[1] = sy * sx; modelview[2] = sy * cx;
    modelview[5] = cx; modelview[6] = -sx;
    modelview[8] = -sy; modelview[9] = cy * sx; modelview[10] = cy * cx;
    modelview[14] = -2.2f;

    multiply_matrix(mvp, projection, modelview);

    float gap = 0.205f;
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
