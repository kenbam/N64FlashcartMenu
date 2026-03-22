#include <math.h>
#include <stdbool.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <libdragon.h>

#include "screensaver_pipes_gl_render.h"

#define PIPES_GL_CELL_SPACING   (1.15f)
#define PIPES_GL_RADIUS         (0.15f)
#define PIPES_GL_JOINT_SCALE    (1.55f)
#define PIPES_GL_CAMERA_RADIUS  (6.20f)
#define PIPES_GL_CAMERA_HEIGHT  (2.55f)
#define PIPES_GL_CAMERA_SWAY    (0.35f)
#define PIPES_GL_NEAR_PLANE     (0.80f)
#define PIPES_GL_FAR_PLANE      (40.0f)
#define PIPES_GL_TUBE_SIDES     (6)
#define PIPES_GL_SPHERE_SLICES  (6)
#define PIPES_GL_SPHERE_STACKS  (4)

typedef struct {
    GLfloat r;
    GLfloat g;
    GLfloat b;
} pipes_gl_color_t;

typedef struct {
    GLfloat x;
    GLfloat y;
    GLfloat z;
} pipes_gl_vec3_t;

typedef struct {
    int last_segment_count;
    int last_dir;
    float energy;
    pipes_gl_vec3_t focus;
    bool initialized;
} pipes_gl_runtime_t;

static const pipes_gl_color_t pipes_gl_palettes[][8] = {
    {
        { 1.00f, 0.39f, 0.39f },
        { 0.31f, 0.91f, 1.00f },
        { 1.00f, 0.83f, 0.34f },
        { 0.47f, 1.00f, 0.53f },
        { 1.00f, 0.49f, 0.96f },
        { 0.41f, 0.57f, 1.00f },
        { 1.00f, 0.63f, 0.27f },
        { 0.82f, 0.47f, 1.00f },
    },
    {
        { 1.00f, 0.55f, 0.29f },
        { 0.24f, 0.84f, 0.78f },
        { 0.97f, 0.79f, 0.23f },
        { 0.56f, 0.90f, 0.30f },
        { 0.96f, 0.42f, 0.71f },
        { 0.31f, 0.49f, 1.00f },
        { 1.00f, 0.44f, 0.18f },
        { 0.97f, 0.70f, 0.30f },
    },
    {
        { 1.00f, 0.32f, 0.55f },
        { 0.35f, 0.96f, 1.00f },
        { 0.91f, 1.00f, 0.38f },
        { 0.30f, 1.00f, 0.72f },
        { 0.79f, 0.47f, 1.00f },
        { 0.29f, 0.64f, 1.00f },
        { 1.00f, 0.69f, 0.31f },
        { 1.00f, 0.41f, 0.76f },
    },
};

static GLuint pipes_gl_tube_list = 0;
static GLuint pipes_gl_joint_list = 0;
static GLuint pipes_gl_room_list = 0;
static pipes_gl_runtime_t pipes_gl_runtime = {0};

static inline float pipes_gl_clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static pipes_gl_vec3_t pipes_gl_vec3_lerp(pipes_gl_vec3_t a, pipes_gl_vec3_t b, float t) {
    pipes_gl_vec3_t out = {
        .x = a.x + ((b.x - a.x) * t),
        .y = a.y + ((b.y - a.y) * t),
        .z = a.z + ((b.z - a.z) * t),
    };
    return out;
}

static pipes_gl_color_t pipes_gl_color_lerp(pipes_gl_color_t a, pipes_gl_color_t b, float t) {
    pipes_gl_color_t out = {
        .r = a.r + ((b.r - a.r) * t),
        .g = a.g + ((b.g - a.g) * t),
        .b = a.b + ((b.b - a.b) * t),
    };
    return out;
}

static pipes_gl_vec3_t pipes_gl_grid_to_world(float x, float y, float z) {
    pipes_gl_vec3_t point = {
        .x = (x - 3.0f) * PIPES_GL_CELL_SPACING,
        .y = (2.0f - y) * PIPES_GL_CELL_SPACING,
        .z = (z - 3.0f) * PIPES_GL_CELL_SPACING,
    };
    return point;
}

static pipes_gl_vec3_t pipes_gl_get_active_head_world(const screensaver_pipes_state_t *state) {
    float hx = (float)state->head_x;
    float hy = (float)state->head_y;
    float hz = (float)state->head_z;
    if (state->dir >= 0 && state->reset_delay_s <= 0.0f) {
        float progress = pipes_gl_clampf(state->segment_progress, 0.0f, 1.0f);
        progress = progress * progress * (3.0f - (2.0f * progress));
        switch (state->dir) {
            case 0: hx += progress; break;
            case 1: hx -= progress; break;
            case 2: hy += progress; break;
            case 3: hy -= progress; break;
            case 4: hz += progress; break;
            case 5: hz -= progress; break;
            default: break;
        }
    }
    return pipes_gl_grid_to_world(hx, hy, hz);
}

static void pipes_gl_update_runtime(const screensaver_pipes_state_t *state) {
    pipes_gl_vec3_t head = pipes_gl_get_active_head_world(state);
    if (!pipes_gl_runtime.initialized) {
        pipes_gl_runtime.focus = head;
        pipes_gl_runtime.initialized = true;
    }

    if (state->segment_count != pipes_gl_runtime.last_segment_count) {
        pipes_gl_runtime.energy += 0.18f;
    }
    if (state->dir != pipes_gl_runtime.last_dir && state->dir >= 0) {
        pipes_gl_runtime.energy += 0.28f;
    }

    pipes_gl_runtime.energy *= 0.90f;
    pipes_gl_runtime.energy = pipes_gl_clampf(pipes_gl_runtime.energy, 0.0f, 1.35f);
    pipes_gl_runtime.focus = pipes_gl_vec3_lerp(pipes_gl_runtime.focus, head, 0.05f);
    pipes_gl_runtime.last_segment_count = state->segment_count;
    pipes_gl_runtime.last_dir = state->dir;
}

static void pipes_gl_set_projection(void) {
    float aspect_ratio = 4.0f / 3.0f;
    if (display_get_height() > 0) {
        aspect_ratio = (float)display_get_width() / (float)display_get_height();
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(
        -PIPES_GL_NEAR_PLANE * aspect_ratio,
        PIPES_GL_NEAR_PLANE * aspect_ratio,
        -PIPES_GL_NEAR_PLANE,
        PIPES_GL_NEAR_PLANE,
        PIPES_GL_NEAR_PLANE,
        PIPES_GL_FAR_PLANE
    );
}

static void pipes_gl_set_camera(const screensaver_pipes_state_t *state) {
    float t = state ? ((float)state->frame_tick * 0.0075f) : 0.0f;
    pipes_gl_vec3_t focus = pipes_gl_runtime.focus;
    pipes_gl_vec3_t anchor = pipes_gl_vec3_lerp((pipes_gl_vec3_t){0.0f, 0.0f, 0.0f}, focus, 0.12f);
    float orbit_radius = 7.25f;
    float eye_x = anchor.x + (cosf(t) * orbit_radius);
    float eye_z = anchor.z + (sinf(t) * orbit_radius);
    float eye_y = 2.15f + (sinf((t * 0.33f) + 0.35f) * 0.18f);
    float center_x = anchor.x;
    float center_y = anchor.y;
    float center_z = anchor.z;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(
        eye_x, eye_y, eye_z,
        center_x, center_y, center_z,
        0.0f, 1.0f, 0.0f
    );
}

static void pipes_gl_set_color4(const pipes_gl_color_t *color, float scale, float alpha) {
    float clamped = pipes_gl_clampf(scale, 0.0f, 1.4f);
    glColor4f(
        pipes_gl_clampf(color->r * clamped, 0.0f, 1.0f),
        pipes_gl_clampf(color->g * clamped, 0.0f, 1.0f),
        pipes_gl_clampf(color->b * clamped, 0.0f, 1.0f),
        pipes_gl_clampf(alpha, 0.0f, 1.0f)
    );
}

static pipes_gl_color_t pipes_gl_get_palette_color(const screensaver_pipes_state_t *state, uint8_t color_index) {
    float palette_time = state ? ((float)state->frame_tick * 0.0045f) : 0.0f;
    int palette_count = (int)(sizeof(pipes_gl_palettes) / sizeof(pipes_gl_palettes[0]));
    int palette_index = ((int)floorf(palette_time)) % palette_count;
    int next_palette_index = (palette_index + 1) % palette_count;
    float blend = palette_time - floorf(palette_time);
    blend = blend * blend * (3.0f - (2.0f * blend));
    int color_slot = color_index % 8;
    pipes_gl_color_t base = pipes_gl_color_lerp(
        pipes_gl_palettes[palette_index][color_slot],
        pipes_gl_palettes[next_palette_index][color_slot],
        blend
    );
    float pulse = 0.96f + (0.08f * sinf((palette_time * 4.8f) + ((float)color_slot * 0.8f)));
    pipes_gl_color_t out = {
        .r = pipes_gl_clampf(base.r * pulse, 0.0f, 1.0f),
        .g = pipes_gl_clampf(base.g * pulse, 0.0f, 1.0f),
        .b = pipes_gl_clampf(base.b * pulse, 0.0f, 1.0f),
    };
    return out;
}

static void pipes_gl_emit_tube_mesh(void) {
    for (int i = 0; i < PIPES_GL_TUBE_SIDES; i++) {
        float a0 = ((float)i / (float)PIPES_GL_TUBE_SIDES) * 6.28318531f;
        float a1 = ((float)(i + 1) / (float)PIPES_GL_TUBE_SIDES) * 6.28318531f;
        float x0 = cosf(a0);
        float y0 = sinf(a0);
        float x1 = cosf(a1);
        float y1 = sinf(a1);

        glBegin(GL_QUADS);
            glNormal3f(x0, y0, 0.0f);
            glVertex3f(x0, y0, -0.5f);
            glVertex3f(x0, y0, 0.5f);
            glNormal3f(x1, y1, 0.0f);
            glVertex3f(x1, y1, 0.5f);
            glVertex3f(x1, y1, -0.5f);
        glEnd();
    }

    glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.0f, 0.0f, -1.0f);
        glVertex3f(0.0f, 0.0f, -0.5f);
        for (int i = 0; i <= PIPES_GL_TUBE_SIDES; i++) {
            float a = ((float)i / (float)PIPES_GL_TUBE_SIDES) * 6.28318531f;
            glVertex3f(cosf(a), sinf(a), -0.5f);
        }
    glEnd();

    glBegin(GL_TRIANGLE_FAN);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(0.0f, 0.0f, 0.5f);
        for (int i = PIPES_GL_TUBE_SIDES; i >= 0; i--) {
            float a = ((float)i / (float)PIPES_GL_TUBE_SIDES) * 6.28318531f;
            glVertex3f(cosf(a), sinf(a), 0.5f);
        }
    glEnd();
}

static void pipes_gl_emit_joint_mesh(void) {
    for (int stack = 0; stack < PIPES_GL_SPHERE_STACKS; stack++) {
        float v0 = ((float)stack / (float)PIPES_GL_SPHERE_STACKS);
        float v1 = ((float)(stack + 1) / (float)PIPES_GL_SPHERE_STACKS);
        float phi0 = (v0 * 3.14159265f) - (3.14159265f * 0.5f);
        float phi1 = (v1 * 3.14159265f) - (3.14159265f * 0.5f);
        float z0 = sinf(phi0);
        float zr0 = cosf(phi0);
        float z1 = sinf(phi1);
        float zr1 = cosf(phi1);

        glBegin(GL_QUAD_STRIP);
        for (int slice = 0; slice <= PIPES_GL_SPHERE_SLICES; slice++) {
            float u = ((float)slice / (float)PIPES_GL_SPHERE_SLICES) * 6.28318531f;
            float x = cosf(u);
            float y = sinf(u);
            glNormal3f(x * zr0, y * zr0, z0);
            glVertex3f(x * zr0, y * zr0, z0);
            glNormal3f(x * zr1, y * zr1, z1);
            glVertex3f(x * zr1, y * zr1, z1);
        }
        glEnd();
    }
}

static void pipes_gl_emit_room_mesh(void) {
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(-6.0f, -3.2f, -6.0f);
        glVertex3f(6.0f, -3.2f, -6.0f);
        glVertex3f(6.0f, -3.2f, 6.0f);
        glVertex3f(-6.0f, -3.2f, 6.0f);
    glEnd();
}

static void pipes_gl_ensure_lists(void) {
    if (pipes_gl_tube_list == 0) {
        pipes_gl_tube_list = glGenLists(1);
        glNewList(pipes_gl_tube_list, GL_COMPILE);
            pipes_gl_emit_tube_mesh();
        glEndList();
    }
    if (pipes_gl_joint_list == 0) {
        pipes_gl_joint_list = glGenLists(1);
        glNewList(pipes_gl_joint_list, GL_COMPILE);
            pipes_gl_emit_joint_mesh();
        glEndList();
    }
    if (pipes_gl_room_list == 0) {
        pipes_gl_room_list = glGenLists(1);
        glNewList(pipes_gl_room_list, GL_COMPILE);
            pipes_gl_emit_room_mesh();
        glEndList();
    }
}

static void pipes_gl_draw_segment(
    const screensaver_pipes_state_t *state,
    pipes_gl_vec3_t a_world,
    pipes_gl_vec3_t b_world,
    uint8_t color_index,
    float radius_scale,
    float color_scale,
    float alpha
) {
    pipes_gl_color_t color = pipes_gl_get_palette_color(state, color_index);
    float dx = b_world.x - a_world.x;
    float dy = b_world.y - a_world.y;
    float dz = b_world.z - a_world.z;
    float length = sqrtf((dx * dx) + (dy * dy) + (dz * dz));
    pipes_gl_vec3_t center = {
        .x = (a_world.x + b_world.x) * 0.5f,
        .y = (a_world.y + b_world.y) * 0.5f,
        .z = (a_world.z + b_world.z) * 0.5f,
    };

    pipes_gl_set_color4(&color, color_scale, alpha);
    glPushMatrix();
        glTranslatef(center.x, center.y, center.z);
        if (fabsf(dx) > 0.001f) {
            glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        } else if (fabsf(dy) > 0.001f) {
            glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        }
        glScalef(PIPES_GL_RADIUS * radius_scale, PIPES_GL_RADIUS * radius_scale, length + (PIPES_GL_RADIUS * 0.75f));
        glCallList(pipes_gl_tube_list);
    glPopMatrix();
}

static void pipes_gl_draw_joint(
    const screensaver_pipes_state_t *state,
    pipes_gl_vec3_t center,
    uint8_t color_index,
    float scale,
    float color_scale,
    float alpha
) {
    pipes_gl_color_t color = pipes_gl_get_palette_color(state, color_index);
    float spin = (float)(state ? state->frame_tick : 0U);

    pipes_gl_set_color4(&color, color_scale, alpha);
    glPushMatrix();
        glTranslatef(center.x, center.y, center.z);
        glRotatef(spin * 3.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(30.0f + (sinf(spin * 0.04f) * 10.0f), 1.0f, 0.0f, 0.0f);
        glScalef(PIPES_GL_RADIUS * scale, PIPES_GL_RADIUS * scale, PIPES_GL_RADIUS * scale);
        glCallList(pipes_gl_joint_list);
    glPopMatrix();
}

static void pipes_gl_draw_room(const screensaver_pipes_state_t *state) {
    float pulse = 0.55f + (0.08f * sinf((float)state->frame_tick * 0.03f));

    glColor4f(0.003f, 0.004f, 0.010f, 1.0f);
    glCallList(pipes_gl_room_list);

    glBegin(GL_QUADS);
        for (int i = -8; i <= 8; i++) {
            float x = (float)i;
            float z = (float)i;
            float major = (i % 4 == 0) ? 1.0f : 0.48f;
            glColor4f(0.01f * major * pulse, 0.04f * major * pulse, 0.10f * major * pulse, 1.0f);
            glVertex3f(x - 0.015f, -3.18f, -8.6f);
            glVertex3f(x + 0.015f, -3.18f, -8.6f);
            glVertex3f(x + 0.015f, -3.18f, 8.6f);
            glVertex3f(x - 0.015f, -3.18f, 8.6f);

            glVertex3f(-8.6f, -3.18f, z - 0.015f);
            glVertex3f(8.6f, -3.18f, z - 0.015f);
            glVertex3f(8.6f, -3.18f, z + 0.015f);
            glVertex3f(-8.6f, -3.18f, z + 0.015f);
        }
    glEnd();
}

static bool pipes_gl_segment_matches_active_dir(const screensaver_pipes_state_t *state, const screensaver_pipe_segment_t *segment) {
    if (!state || !segment || state->dir < 0 || state->reset_delay_s > 0.0f) {
        return false;
    }
    if (segment->bx != state->head_x || segment->by != state->head_y || segment->bz != state->head_z) {
        return false;
    }

    int seg_dx = segment->bx - segment->ax;
    int seg_dy = segment->by - segment->ay;
    int seg_dz = segment->bz - segment->az;
    switch (state->dir) {
        case 0: return seg_dx > 0 && seg_dy == 0 && seg_dz == 0;
        case 1: return seg_dx < 0 && seg_dy == 0 && seg_dz == 0;
        case 2: return seg_dy > 0 && seg_dx == 0 && seg_dz == 0;
        case 3: return seg_dy < 0 && seg_dx == 0 && seg_dz == 0;
        case 4: return seg_dz > 0 && seg_dx == 0 && seg_dy == 0;
        case 5: return seg_dz < 0 && seg_dx == 0 && seg_dy == 0;
        default: return false;
    }
}

static bool pipes_gl_segment_dirs_match(const screensaver_pipe_segment_t *a, const screensaver_pipe_segment_t *b) {
    return (a->bx - a->ax) == (b->bx - b->ax) &&
        (a->by - a->ay) == (b->by - b->ay) &&
        (a->bz - a->az) == (b->bz - b->az);
}

static bool pipes_gl_should_draw_joint(const screensaver_pipes_state_t *state, int segment_index) {
    if (!state || segment_index < 0 || segment_index >= state->segment_count) {
        return false;
    }

    if (segment_index + 1 >= state->segment_count) {
        return !pipes_gl_segment_matches_active_dir(state, &state->segments[segment_index]);
    }

    const screensaver_pipe_segment_t *segment = &state->segments[segment_index];
    const screensaver_pipe_segment_t *next = &state->segments[segment_index + 1];
    bool chained = segment->bx == next->ax && segment->by == next->ay && segment->bz == next->az;
    if (!chained) {
        return true;
    }

    return !pipes_gl_segment_dirs_match(segment, next);
}

void screensaver_pipes_gl_draw(surface_t *display, const screensaver_pipes_state_t *state) {
    if (!display || !state) {
        return;
    }

    static const GLfloat clear_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const GLfloat light0_pos[] = { 0.55f, 0.85f, 0.35f, 0.0f };
    static const GLfloat light0_diffuse[] = { 0.88f, 0.88f, 0.88f, 1.0f };
    static const GLfloat light1_pos[] = { -0.40f, 0.25f, -0.90f, 0.0f };
    static const GLfloat light1_diffuse[] = { 0.24f, 0.24f, 0.28f, 1.0f };
    static const GLfloat mat_specular[] = { 0.10f, 0.10f, 0.10f, 1.0f };
    static const GLfloat mat_emission[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    gl_context_begin();
    pipes_gl_ensure_lists();
    pipes_gl_update_runtime(state);

    pipes_gl_set_projection();
    pipes_gl_set_camera(state);

    glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glShadeModel(GL_SMOOTH);
    glDitherModeN64(DITHER_NONE_NONE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glDisable(GL_LIGHTING);
    pipes_gl_draw_room(state);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightfv(GL_LIGHT0, GL_POSITION, light0_pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
    glLightfv(GL_LIGHT1, GL_POSITION, light1_pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 12.0f);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_emission);

    for (int i = 0; i < state->segment_count; i++) {
        const screensaver_pipe_segment_t *segment = &state->segments[i];
        pipes_gl_draw_segment(
            state,
            pipes_gl_grid_to_world((float)segment->ax, (float)segment->ay, (float)segment->az),
            pipes_gl_grid_to_world((float)segment->bx, (float)segment->by, (float)segment->bz),
            segment->color_index,
            1.0f,
            1.0f,
            1.0f
        );

        if (pipes_gl_should_draw_joint(state, i)) {
            pipes_gl_draw_joint(
                state,
                pipes_gl_grid_to_world((float)segment->bx, (float)segment->by, (float)segment->bz),
                segment->color_index,
                PIPES_GL_JOINT_SCALE,
                1.0f,
                1.0f
            );
        }
    }

    if (state->dir >= 0 && state->reset_delay_s <= 0.0f) {
        float progress = pipes_gl_clampf(state->segment_progress, 0.0f, 1.0f);
        progress = progress * progress * (3.0f - (2.0f * progress));
        float hx = (float)state->head_x;
        float hy = (float)state->head_y;
        float hz = (float)state->head_z;
        float tx = hx;
        float ty = hy;
        float tz = hz;
        switch (state->dir) {
            case 0: tx += progress; break;
            case 1: tx -= progress; break;
            case 2: ty += progress; break;
            case 3: ty -= progress; break;
            case 4: tz += progress; break;
            case 5: tz -= progress; break;
            default: break;
        }

        pipes_gl_draw_segment(
            state,
            pipes_gl_grid_to_world(hx, hy, hz),
            pipes_gl_grid_to_world(tx, ty, tz),
            state->active_color_index,
            1.0f,
            1.0f,
            1.0f
        );
        pipes_gl_draw_joint(
            state,
            pipes_gl_grid_to_world(tx, ty, tz),
            state->active_color_index,
            1.15f + (progress * 0.35f),
            1.0f,
            1.0f
        );
    }
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_LIGHT1);
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    gl_context_end();
}
