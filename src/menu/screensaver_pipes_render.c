#include <math.h>
#include <stdbool.h>

#include <libdragon.h>

#include "screensaver_pipes_render.h"

#define SCREENSAVER_PIPES_CELL_SPACING   (1.0f)
#define SCREENSAVER_PIPES_CAMERA_DISTANCE (7.9f)
#define SCREENSAVER_PIPES_FOCAL_LENGTH   (212.0f)
#define SCREENSAVER_PIPES_RADIUS         (0.17f)
#define SCREENSAVER_PIPES_TUBE_SIDES     (6)
#define SCREENSAVER_PIPES_JOINT_SIDES    (6)

typedef struct {
    float x;
    float y;
    float z;
} screensaver_vec3_t;

typedef struct {
    bool visible;
    float x;
    float y;
    float depth;
    float scale;
    screensaver_vec3_t camera;
} screensaver_projected_t;

typedef struct {
    int index;
    float depth;
} screensaver_pipe_draw_entry_t;

typedef struct {
    bool visible;
    float depth;
    screensaver_projected_t p[4];
    color_t color[4];
} screensaver_pipe_face_t;

static const color_t screensaver_palette[] = {
    RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    RGBA32(0xFF, 0x5E, 0x5E, 0xFF),
    RGBA32(0x55, 0xE8, 0xFF, 0xFF),
    RGBA32(0xFF, 0xD3, 0x55, 0xFF),
    RGBA32(0x7B, 0xFF, 0x83, 0xFF),
    RGBA32(0xFF, 0x7B, 0xF1, 0xFF),
    RGBA32(0x65, 0x8D, 0xFF, 0xFF),
    RGBA32(0xFF, 0x9C, 0x47, 0xFF),
};

static inline float screensaver_clampf(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static inline uint8_t screensaver_u8_clamp(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static color_t screensaver_get_palette_color(uint8_t color_index) {
    return screensaver_palette[color_index % (sizeof(screensaver_palette) / sizeof(screensaver_palette[0]))];
}

static color_t screensaver_color_scale(color_t color, float scale) {
    if (scale < 0.0f) scale = 0.0f;
    return RGBA32(
        screensaver_u8_clamp((int)((float)color.r * scale)),
        screensaver_u8_clamp((int)((float)color.g * scale)),
        screensaver_u8_clamp((int)((float)color.b * scale)),
        0xFF
    );
}

static screensaver_vec3_t vec3_add(screensaver_vec3_t a, screensaver_vec3_t b) {
    screensaver_vec3_t out = { a.x + b.x, a.y + b.y, a.z + b.z };
    return out;
}

static screensaver_vec3_t vec3_sub(screensaver_vec3_t a, screensaver_vec3_t b) {
    screensaver_vec3_t out = { a.x - b.x, a.y - b.y, a.z - b.z };
    return out;
}

static screensaver_vec3_t vec3_scale(screensaver_vec3_t v, float scale) {
    screensaver_vec3_t out = { v.x * scale, v.y * scale, v.z * scale };
    return out;
}

static float vec3_dot(screensaver_vec3_t a, screensaver_vec3_t b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static screensaver_vec3_t vec3_cross(screensaver_vec3_t a, screensaver_vec3_t b) {
    screensaver_vec3_t out = {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x),
    };
    return out;
}

static screensaver_vec3_t vec3_normalize(screensaver_vec3_t v) {
    float len = sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    if (len <= 0.0001f) {
        screensaver_vec3_t zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }
    return vec3_scale(v, 1.0f / len);
}

static screensaver_vec3_t grid_to_world(float x, float y, float z) {
    screensaver_vec3_t point = {
        .x = (x - 3.0f) * SCREENSAVER_PIPES_CELL_SPACING,
        .y = (2.0f - y) * SCREENSAVER_PIPES_CELL_SPACING,
        .z = (z - 3.0f) * SCREENSAVER_PIPES_CELL_SPACING,
    };
    return point;
}

static void get_camera_angles(const screensaver_pipes_state_t *state, float *yaw, float *pitch) {
    float t = state ? ((float)state->frame_tick * 0.011f) : 0.0f;
    if (yaw) {
        *yaw = 0.92f + (sinf(t * 0.47f) * 0.36f);
    }
    if (pitch) {
        *pitch = 0.58f + (sinf((t * 0.29f) + 1.1f) * 0.16f);
    }
}

static screensaver_vec3_t rotate_to_camera(const screensaver_pipes_state_t *state, screensaver_vec3_t point) {
    float yaw, pitch;
    get_camera_angles(state, &yaw, &pitch);
    float yaw_cos = cosf(yaw);
    float yaw_sin = sinf(yaw);
    float pitch_cos = cosf(pitch);
    float pitch_sin = sinf(pitch);

    float rot_x = (point.x * yaw_cos) - (point.z * yaw_sin);
    float rot_z = (point.x * yaw_sin) + (point.z * yaw_cos);
    float rot_y = (point.y * pitch_cos) - (rot_z * pitch_sin);
    float rot_z2 = (point.y * pitch_sin) + (rot_z * pitch_cos);

    screensaver_vec3_t out = { rot_x, rot_y, rot_z2 };
    return out;
}

static screensaver_projected_t project_world_point(const screensaver_pipes_state_t *state, screensaver_vec3_t point, int screen_w, int screen_h) {
    screensaver_vec3_t camera = rotate_to_camera(state, point);
    float depth = camera.z + SCREENSAVER_PIPES_CAMERA_DISTANCE;
    screensaver_projected_t projected = {
        .visible = false,
        .x = 0.0f,
        .y = 0.0f,
        .depth = depth,
        .scale = 0.0f,
        .camera = camera,
    };

    if (depth <= 0.35f) {
        return projected;
    }

    float scale = SCREENSAVER_PIPES_FOCAL_LENGTH / depth;
    projected.visible = true;
    projected.scale = scale;
    projected.x = ((float)screen_w * 0.5f) + (camera.x * scale);
    projected.y = ((float)screen_h * 0.5f) - (camera.y * scale);
    return projected;
}

static void make_shade_vertex(float *vertex, float x, float y, color_t color) {
    vertex[0] = x;
    vertex[1] = y;
    vertex[2] = (float)color.r / 255.0f;
    vertex[3] = (float)color.g / 255.0f;
    vertex[4] = (float)color.b / 255.0f;
    vertex[5] = (float)color.a / 255.0f;
}

static void draw_shaded_triangle(
    float x0, float y0, color_t c0,
    float x1, float y1, color_t c1,
    float x2, float y2, color_t c2
) {
    float v0[6];
    float v1[6];
    float v2[6];
    make_shade_vertex(v0, x0, y0, c0);
    make_shade_vertex(v1, x1, y1, c1);
    make_shade_vertex(v2, x2, y2, c2);
    rdpq_triangle(&TRIFMT_SHADE, v0, v1, v2);
}

static void draw_shaded_quad(
    const screensaver_projected_t *p0, color_t c0,
    const screensaver_projected_t *p1, color_t c1,
    const screensaver_projected_t *p2, color_t c2,
    const screensaver_projected_t *p3, color_t c3
) {
    draw_shaded_triangle(p0->x, p0->y, c0, p1->x, p1->y, c1, p2->x, p2->y, c2);
    draw_shaded_triangle(p0->x, p0->y, c0, p2->x, p2->y, c2, p3->x, p3->y, c3);
}

static color_t pipe_vertex_color(color_t base_color, float fog, float light) {
    float diffuse = screensaver_clampf(light, 0.0f, 1.0f);
    float gloss = diffuse * diffuse;
    gloss *= gloss;
    return screensaver_color_scale(base_color, fog * (0.18f + (diffuse * 0.56f) + (gloss * 0.46f)));
}

static bool pipes_segment_dirs_match(const screensaver_pipe_segment_t *a, const screensaver_pipe_segment_t *b) {
    int adx = a->bx - a->ax;
    int ady = a->by - a->ay;
    int adz = a->bz - a->az;
    int bdx = b->bx - b->ax;
    int bdy = b->by - b->ay;
    int bdz = b->bz - b->az;
    return adx == bdx && ady == bdy && adz == bdz;
}

static bool pipes_should_draw_joint(const screensaver_pipes_state_t *state, int segment_index) {
    if (!state || segment_index < 0 || segment_index >= state->segment_count) {
        return false;
    }

    if (segment_index + 1 >= state->segment_count) {
        return true;
    }

    const screensaver_pipe_segment_t *segment = &state->segments[segment_index];
    const screensaver_pipe_segment_t *next = &state->segments[segment_index + 1];
    bool chained = segment->bx == next->ax && segment->by == next->ay && segment->bz == next->az;
    if (!chained) {
        return true;
    }

    return !pipes_segment_dirs_match(segment, next);
}

static void draw_joint_fitting(const screensaver_pipes_state_t *state, screensaver_vec3_t center_world, color_t base_color, int screen_w, int screen_h) {
    screensaver_projected_t center = project_world_point(state, center_world, screen_w, screen_h);
    if (!center.visible) {
        return;
    }

    float fog = 1.0f - screensaver_clampf((center.depth - 4.0f) / 6.0f, 0.0f, 0.72f);
    float radius = (center.scale * SCREENSAVER_PIPES_RADIUS * 0.92f) + 0.55f;
    float bevel = radius * 0.62f;
    color_t center_color = screensaver_color_scale(base_color, fog * 0.82f);
    color_t light_color = screensaver_color_scale(base_color, fog * 0.96f);
    color_t dark_color = screensaver_color_scale(base_color, fog * 0.38f);
    color_t mid_color = screensaver_color_scale(base_color, fog * 0.60f);

    float top_x = center.x;
    float top_y = center.y - radius;
    float right_x = center.x + radius;
    float right_y = center.y;
    float bottom_x = center.x;
    float bottom_y = center.y + radius;
    float left_x = center.x - radius;
    float left_y = center.y;
    float top_right_x = center.x + bevel;
    float top_right_y = center.y - bevel;
    float bottom_right_x = center.x + bevel;
    float bottom_right_y = center.y + bevel;
    float bottom_left_x = center.x - bevel;
    float bottom_left_y = center.y + bevel;
    float top_left_x = center.x - bevel;
    float top_left_y = center.y - bevel;

    draw_shaded_triangle(center.x, center.y, center_color, top_x, top_y, light_color, top_right_x, top_right_y, mid_color);
    draw_shaded_triangle(center.x, center.y, center_color, top_right_x, top_right_y, mid_color, right_x, right_y, mid_color);
    draw_shaded_triangle(center.x, center.y, center_color, right_x, right_y, mid_color, bottom_right_x, bottom_right_y, dark_color);
    draw_shaded_triangle(center.x, center.y, center_color, bottom_right_x, bottom_right_y, dark_color, bottom_x, bottom_y, dark_color);
    draw_shaded_triangle(center.x, center.y, center_color, bottom_x, bottom_y, dark_color, bottom_left_x, bottom_left_y, dark_color);
    draw_shaded_triangle(center.x, center.y, center_color, bottom_left_x, bottom_left_y, dark_color, left_x, left_y, mid_color);
    draw_shaded_triangle(center.x, center.y, center_color, left_x, left_y, mid_color, top_left_x, top_left_y, mid_color);
    draw_shaded_triangle(center.x, center.y, center_color, top_left_x, top_left_y, mid_color, top_x, top_y, light_color);
}

static void draw_pipe_segment(
    const screensaver_pipes_state_t *state,
    screensaver_vec3_t a_world,
    screensaver_vec3_t b_world,
    uint8_t color_index,
    int screen_w,
    int screen_h,
    bool draw_joint
) {
    screensaver_vec3_t dir = vec3_normalize(vec3_sub(b_world, a_world));
    screensaver_vec3_t ref = fabsf(dir.y) < 0.9f ? (screensaver_vec3_t){0.0f, 1.0f, 0.0f} : (screensaver_vec3_t){1.0f, 0.0f, 0.0f};
    screensaver_vec3_t basis_u = vec3_normalize(vec3_cross(dir, ref));
    screensaver_vec3_t basis_v = vec3_normalize(vec3_cross(dir, basis_u));
    screensaver_vec3_t light_dir = vec3_normalize((screensaver_vec3_t){-0.55f, 0.28f, -0.78f});
    color_t base_color = screensaver_get_palette_color(color_index);

    screensaver_pipe_face_t faces[SCREENSAVER_PIPES_TUBE_SIDES] = {0};
    int face_count = 0;

    for (int i = 0; i < SCREENSAVER_PIPES_TUBE_SIDES; i++) {
        float t0 = ((float)i / (float)SCREENSAVER_PIPES_TUBE_SIDES) * 6.28318531f;
        float t1 = ((float)(i + 1) / (float)SCREENSAVER_PIPES_TUBE_SIDES) * 6.28318531f;
        screensaver_vec3_t off0 = vec3_add(
            vec3_scale(basis_u, cosf(t0) * SCREENSAVER_PIPES_RADIUS),
            vec3_scale(basis_v, sinf(t0) * SCREENSAVER_PIPES_RADIUS)
        );
        screensaver_vec3_t off1 = vec3_add(
            vec3_scale(basis_u, cosf(t1) * SCREENSAVER_PIPES_RADIUS),
            vec3_scale(basis_v, sinf(t1) * SCREENSAVER_PIPES_RADIUS)
        );
        screensaver_vec3_t n0 = vec3_normalize(off0);
        screensaver_vec3_t n1 = vec3_normalize(off1);
        screensaver_vec3_t face_normal = vec3_normalize(vec3_add(n0, n1));
        screensaver_vec3_t face_center = vec3_add(
            vec3_scale(vec3_add(a_world, b_world), 0.5f),
            vec3_scale(vec3_add(off0, off1), 0.5f)
        );
        screensaver_vec3_t face_normal_cam = rotate_to_camera(state, face_normal);
        screensaver_vec3_t face_center_cam = rotate_to_camera(state, face_center);
        if (vec3_dot(face_normal_cam, face_center_cam) >= -0.02f) {
            continue;
        }

        screensaver_projected_t p0 = project_world_point(state, vec3_add(a_world, off0), screen_w, screen_h);
        screensaver_projected_t p1 = project_world_point(state, vec3_add(a_world, off1), screen_w, screen_h);
        screensaver_projected_t p2 = project_world_point(state, vec3_add(b_world, off1), screen_w, screen_h);
        screensaver_projected_t p3 = project_world_point(state, vec3_add(b_world, off0), screen_w, screen_h);
        if (!p0.visible || !p1.visible || !p2.visible || !p3.visible) {
            continue;
        }

        float avg_depth = (p0.depth + p1.depth + p2.depth + p3.depth) * 0.25f;
        float fog = 1.0f - screensaver_clampf((avg_depth - 4.0f) / 6.0f, 0.0f, 0.72f);
        screensaver_vec3_t n0_cam = rotate_to_camera(state, n0);
        screensaver_vec3_t n1_cam = rotate_to_camera(state, n1);

        faces[face_count].visible = true;
        faces[face_count].depth = avg_depth;
        faces[face_count].p[0] = p0;
        faces[face_count].p[1] = p1;
        faces[face_count].p[2] = p2;
        faces[face_count].p[3] = p3;
        faces[face_count].color[0] = pipe_vertex_color(base_color, fog, vec3_dot(n0_cam, light_dir));
        faces[face_count].color[1] = pipe_vertex_color(base_color, fog, vec3_dot(n1_cam, light_dir));
        faces[face_count].color[2] = faces[face_count].color[1];
        faces[face_count].color[3] = faces[face_count].color[0];
        face_count++;
    }

    for (int i = 0; i < face_count - 1; i++) {
        for (int j = i + 1; j < face_count; j++) {
            if (faces[j].depth > faces[i].depth) {
                screensaver_pipe_face_t swap = faces[i];
                faces[i] = faces[j];
                faces[j] = swap;
            }
        }
    }

    for (int i = 0; i < face_count; i++) {
        draw_shaded_quad(
            &faces[i].p[0], faces[i].color[0],
            &faces[i].p[1], faces[i].color[1],
            &faces[i].p[2], faces[i].color[2],
            &faces[i].p[3], faces[i].color[3]
        );
    }

    if (draw_joint) {
        draw_joint_fitting(state, b_world, base_color, screen_w, screen_h);
    }
}

void screensaver_pipes_draw(surface_t *display, const screensaver_pipes_state_t *state) {
    if (!display || !state) {
        return;
    }

    int screen_w = display_get_width();
    int screen_h = display_get_height();
    screensaver_pipe_draw_entry_t draw_list[SCREENSAVER_PIPES_MAX_SEGMENTS];
    int draw_count = 0;

    for (int i = 0; i < state->segment_count; i++) {
        screensaver_vec3_t a_world = grid_to_world((float)state->segments[i].ax, (float)state->segments[i].ay, (float)state->segments[i].az);
        screensaver_vec3_t b_world = grid_to_world((float)state->segments[i].bx, (float)state->segments[i].by, (float)state->segments[i].bz);
        screensaver_projected_t pa = project_world_point(state, a_world, screen_w, screen_h);
        screensaver_projected_t pb = project_world_point(state, b_world, screen_w, screen_h);
        if (!pa.visible && !pb.visible) {
            continue;
        }
        draw_list[draw_count].index = i;
        draw_list[draw_count].depth = (pa.depth + pb.depth) * 0.5f;
        draw_count++;
    }

    for (int i = 0; i < draw_count - 1; i++) {
        for (int j = i + 1; j < draw_count; j++) {
            if (draw_list[j].depth > draw_list[i].depth) {
                screensaver_pipe_draw_entry_t swap = draw_list[i];
                draw_list[i] = draw_list[j];
                draw_list[j] = swap;
            }
        }
    }

    rdpq_set_scissor(0, 0, screen_w, screen_h);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);

        for (int i = 0; i < draw_count; i++) {
            const screensaver_pipe_segment_t *segment = &state->segments[draw_list[i].index];
            draw_pipe_segment(
                state,
                grid_to_world((float)segment->ax, (float)segment->ay, (float)segment->az),
                grid_to_world((float)segment->bx, (float)segment->by, (float)segment->bz),
                segment->color_index,
                screen_w,
                screen_h,
                pipes_should_draw_joint(state, draw_list[i].index)
            );
        }

        if (state->dir >= 0 && state->reset_delay_s <= 0.0f) {
            float progress = screensaver_clampf(state->segment_progress, 0.0f, 1.0f);
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

            draw_pipe_segment(
                state,
                grid_to_world(hx, hy, hz),
                grid_to_world(tx, ty, tz),
                state->active_color_index,
                screen_w,
                screen_h,
                false
            );
        }
    rdpq_mode_pop();
}
