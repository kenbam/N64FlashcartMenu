#include <stdbool.h>

#include <libdragon.h>

#include "screensaver_pipes_state.h"

#define SCREENSAVER_PIPES_GRID_WIDTH  (7)
#define SCREENSAVER_PIPES_GRID_HEIGHT (5)
#define SCREENSAVER_PIPES_GRID_DEPTH  (7)
#define SCREENSAVER_PIPES_GROWTH_RATE (3.80f)
#define SCREENSAVER_PIPES_MAX_CELLS   (80)
#define SCREENSAVER_PIPES_RESET_DELAY (1.20f)

typedef enum {
    SCREENSAVER_PIPE_DIR_POS_X = 0,
    SCREENSAVER_PIPE_DIR_NEG_X,
    SCREENSAVER_PIPE_DIR_POS_Y,
    SCREENSAVER_PIPE_DIR_NEG_Y,
    SCREENSAVER_PIPE_DIR_POS_Z,
    SCREENSAVER_PIPE_DIR_NEG_Z,
    SCREENSAVER_PIPE_DIR_COUNT,
} screensaver_pipe_dir_t;

static const int pipe_dx[SCREENSAVER_PIPE_DIR_COUNT] = { 1, -1, 0, 0, 0, 0 };
static const int pipe_dy[SCREENSAVER_PIPE_DIR_COUNT] = { 0, 0, 1, -1, 0, 0 };
static const int pipe_dz[SCREENSAVER_PIPE_DIR_COUNT] = { 0, 0, 0, 0, 1, -1 };
static const int pipe_opposite[SCREENSAVER_PIPE_DIR_COUNT] = {
    SCREENSAVER_PIPE_DIR_NEG_X,
    SCREENSAVER_PIPE_DIR_POS_X,
    SCREENSAVER_PIPE_DIR_NEG_Y,
    SCREENSAVER_PIPE_DIR_POS_Y,
    SCREENSAVER_PIPE_DIR_NEG_Z,
    SCREENSAVER_PIPE_DIR_POS_Z,
};

static uint32_t pipes_rand_u32(screensaver_pipes_state_t *state) {
    if (state->rng == 0) {
        state->rng = (uint32_t)get_ticks_us() ^ 0x9E3779B9u;
    }
    state->rng = (state->rng * 1664525u) + 1013904223u;
    return state->rng;
}

static int pipes_rand_int(screensaver_pipes_state_t *state, int limit) {
    if (limit <= 0) {
        return 0;
    }
    return (int)(pipes_rand_u32(state) % (uint32_t)limit);
}

static uint8_t pipes_pick_run_length(screensaver_pipes_state_t *state) {
    int roll = pipes_rand_int(state, 100);
    if (roll < 28) {
        return (uint8_t)(1 + pipes_rand_int(state, 2));
    }
    if (roll < 68) {
        return (uint8_t)(3 + pipes_rand_int(state, 3));
    }
    if (roll < 90) {
        return (uint8_t)(6 + pipes_rand_int(state, 3));
    }
    return (uint8_t)(9 + pipes_rand_int(state, 3));
}

static bool pipes_coord_valid(int x, int y, int z) {
    return x >= 0 && x < SCREENSAVER_PIPES_GRID_WIDTH &&
        y >= 0 && y < SCREENSAVER_PIPES_GRID_HEIGHT &&
        z >= 0 && z < SCREENSAVER_PIPES_GRID_DEPTH;
}

static int pipes_sign(int value) {
    if (value > 0) return 1;
    if (value < 0) return -1;
    return 0;
}

static bool pipes_edge_occupied(const screensaver_pipes_state_t *state, int ax, int ay, int az, int bx, int by, int bz) {
    for (int i = 0; i < state->segment_count; i++) {
        const screensaver_pipe_segment_t *segment = &state->segments[i];
        if (ay == by && az == bz && segment->ay == segment->by && segment->az == segment->bz &&
            segment->ay == ay && segment->az == az) {
            int edge_min = ax < bx ? ax : bx;
            int edge_max = ax > bx ? ax : bx;
            int seg_min = segment->ax < segment->bx ? segment->ax : segment->bx;
            int seg_max = segment->ax > segment->bx ? segment->ax : segment->bx;
            if (edge_min >= seg_min && edge_max <= seg_max) {
                return true;
            }
        }

        if (ax == bx && az == bz && segment->ax == segment->bx && segment->az == segment->bz &&
            segment->ax == ax && segment->az == az) {
            int edge_min = ay < by ? ay : by;
            int edge_max = ay > by ? ay : by;
            int seg_min = segment->ay < segment->by ? segment->ay : segment->by;
            int seg_max = segment->ay > segment->by ? segment->ay : segment->by;
            if (edge_min >= seg_min && edge_max <= seg_max) {
                return true;
            }
        }

        if (ax == bx && ay == by && segment->ax == segment->bx && segment->ay == segment->by &&
            segment->ax == ax && segment->ay == ay) {
            int edge_min = az < bz ? az : bz;
            int edge_max = az > bz ? az : bz;
            int seg_min = segment->az < segment->bz ? segment->az : segment->bz;
            int seg_max = segment->az > segment->bz ? segment->az : segment->bz;
            if (edge_min >= seg_min && edge_max <= seg_max) {
                return true;
            }
        }
    }
    return false;
}

static bool pipes_node_occupied(const screensaver_pipes_state_t *state, int x, int y, int z) {
    for (int i = 0; i < state->segment_count; i++) {
        const screensaver_pipe_segment_t *segment = &state->segments[i];
        if (segment->ay == segment->by && segment->az == segment->bz &&
            y == segment->ay && z == segment->az) {
            int seg_min = segment->ax < segment->bx ? segment->ax : segment->bx;
            int seg_max = segment->ax > segment->bx ? segment->ax : segment->bx;
            if (x >= seg_min && x <= seg_max) {
                return true;
            }
        }

        if (segment->ax == segment->bx && segment->az == segment->bz &&
            x == segment->ax && z == segment->az) {
            int seg_min = segment->ay < segment->by ? segment->ay : segment->by;
            int seg_max = segment->ay > segment->by ? segment->ay : segment->by;
            if (y >= seg_min && y <= seg_max) {
                return true;
            }
        }

        if (segment->ax == segment->bx && segment->ay == segment->by &&
            x == segment->ax && y == segment->ay) {
            int seg_min = segment->az < segment->bz ? segment->az : segment->bz;
            int seg_max = segment->az > segment->bz ? segment->az : segment->bz;
            if (z >= seg_min && z <= seg_max) {
                return true;
            }
        }
    }
    return false;
}

static bool pipes_can_extend_segment(const screensaver_pipe_segment_t *segment, int ax, int ay, int az, int bx, int by, int bz, uint8_t color_index) {
    if (!segment || segment->color_index != color_index) {
        return false;
    }

    if (segment->bx != ax || segment->by != ay || segment->bz != az) {
        return false;
    }

    int seg_dx = pipes_sign(segment->bx - segment->ax);
    int seg_dy = pipes_sign(segment->by - segment->ay);
    int seg_dz = pipes_sign(segment->bz - segment->az);
    int edge_dx = pipes_sign(bx - ax);
    int edge_dy = pipes_sign(by - ay);
    int edge_dz = pipes_sign(bz - az);
    return seg_dx == edge_dx && seg_dy == edge_dy && seg_dz == edge_dz;
}

static int pipes_free_edge_count(const screensaver_pipes_state_t *state, int x, int y, int z) {
    int count = 0;
    for (int dir = 0; dir < SCREENSAVER_PIPE_DIR_COUNT; dir++) {
        int nx = x + pipe_dx[dir];
        int ny = y + pipe_dy[dir];
        int nz = z + pipe_dz[dir];
        if (!pipes_coord_valid(nx, ny, nz)) {
            continue;
        }
        if (!pipes_edge_occupied(state, x, y, z, nx, ny, nz)) {
            count++;
        }
    }
    return count;
}

static bool pipes_pick_start(screensaver_pipes_state_t *state, int *out_x, int *out_y, int *out_z) {
    int total_nodes = SCREENSAVER_PIPES_GRID_WIDTH * SCREENSAVER_PIPES_GRID_HEIGHT * SCREENSAVER_PIPES_GRID_DEPTH;
    int start_index = pipes_rand_int(state, total_nodes);
    for (int i = 0; i < total_nodes; i++) {
        int index = (start_index + i) % total_nodes;
        int x = index % SCREENSAVER_PIPES_GRID_WIDTH;
        int y = (index / SCREENSAVER_PIPES_GRID_WIDTH) % SCREENSAVER_PIPES_GRID_HEIGHT;
        int z = index / (SCREENSAVER_PIPES_GRID_WIDTH * SCREENSAVER_PIPES_GRID_HEIGHT);
        if (pipes_node_occupied(state, x, y, z)) {
            continue;
        }
        if (pipes_free_edge_count(state, x, y, z) <= 0) {
            continue;
        }
        if (out_x) *out_x = x;
        if (out_y) *out_y = y;
        if (out_z) *out_z = z;
        return true;
    }
    return false;
}

static int pipes_collect_dirs(const screensaver_pipes_state_t *state, int x, int y, int z, int prev_dir, int *out_dirs) {
    int count = 0;
    for (int dir = 0; dir < SCREENSAVER_PIPE_DIR_COUNT; dir++) {
        if (prev_dir >= 0 && dir == pipe_opposite[prev_dir]) {
            continue;
        }
        int nx = x + pipe_dx[dir];
        int ny = y + pipe_dy[dir];
        int nz = z + pipe_dz[dir];
        if (!pipes_coord_valid(nx, ny, nz)) {
            continue;
        }
        if (pipes_node_occupied(state, nx, ny, nz)) {
            continue;
        }
        if (pipes_edge_occupied(state, x, y, z, nx, ny, nz)) {
            continue;
        }
        out_dirs[count++] = dir;
    }
    return count;
}

static int pipes_choose_dir(screensaver_pipes_state_t *state, int x, int y, int z, int prev_dir) {
    int options[SCREENSAVER_PIPE_DIR_COUNT];
    int option_count = pipes_collect_dirs(state, x, y, z, prev_dir, options);
    if (option_count <= 0) {
        return -1;
    }

    if (prev_dir >= 0) {
        bool straight_available = false;
        for (int i = 0; i < option_count; i++) {
            if (options[i] == prev_dir) {
                straight_available = true;
                if (state->straight_run_remaining > 0) {
                    return prev_dir;
                }
                break;
            }
        }

        if (straight_available && option_count == 1) {
            return prev_dir;
        }

        if (straight_available && option_count > 1 && pipes_rand_int(state, 100) < 18) {
            return prev_dir;
        }
    }

    return options[pipes_rand_int(state, option_count)];
}

static void pipes_schedule_reset(screensaver_pipes_state_t *state) {
    state->dir = -1;
    state->segment_progress = 0.0f;
    state->reset_delay_s = SCREENSAVER_PIPES_RESET_DELAY;
}

static bool pipes_start_pipe(screensaver_pipes_state_t *state) {
    int start_x, start_y, start_z;
    if (!pipes_pick_start(state, &start_x, &start_y, &start_z)) {
        pipes_schedule_reset(state);
        return false;
    }

    state->head_x = start_x;
    state->head_y = start_y;
    state->head_z = start_z;
    state->dir = pipes_choose_dir(state, start_x, start_y, start_z, -1);
    state->segment_progress = 0.0f;
    state->reset_delay_s = 0.0f;
    state->active_color_index = (uint8_t)pipes_rand_int(state, 8);
    state->straight_run_remaining = pipes_pick_run_length(state);
    return state->dir >= 0;
}

static void pipes_advance(screensaver_pipes_state_t *state) {
    if (state->dir < 0) {
        pipes_start_pipe(state);
        return;
    }

    int ax = state->head_x;
    int ay = state->head_y;
    int az = state->head_z;
    int bx = ax + pipe_dx[state->dir];
    int by = ay + pipe_dy[state->dir];
    int bz = az + pipe_dz[state->dir];

    if (!pipes_coord_valid(bx, by, bz) || pipes_node_occupied(state, bx, by, bz) || pipes_edge_occupied(state, ax, ay, az, bx, by, bz)) {
        if (!pipes_start_pipe(state)) {
            pipes_schedule_reset(state);
        }
        return;
    }

    screensaver_pipe_segment_t *segment = NULL;
    if (state->segment_count > 0) {
        screensaver_pipe_segment_t *last_segment = &state->segments[state->segment_count - 1];
        if (pipes_can_extend_segment(last_segment, ax, ay, az, bx, by, bz, state->active_color_index)) {
            segment = last_segment;
        }
    }

    if (!segment) {
        if (state->segment_count >= SCREENSAVER_PIPES_MAX_SEGMENTS) {
            pipes_schedule_reset(state);
            return;
        }

        segment = &state->segments[state->segment_count++];
        segment->color_index = state->active_color_index;
        segment->ax = ax;
        segment->ay = ay;
        segment->az = az;
    }

    segment->bx = bx;
    segment->by = by;
    segment->bz = bz;
    state->traversed_cells++;

    state->head_x = bx;
    state->head_y = by;
    state->head_z = bz;

    if (state->traversed_cells >= SCREENSAVER_PIPES_MAX_CELLS) {
        pipes_schedule_reset(state);
        return;
    }

    int prev_dir = state->dir;
    if (state->straight_run_remaining > 0) {
        state->straight_run_remaining--;
    }

    int next_dir = pipes_choose_dir(state, bx, by, bz, prev_dir);
    if (next_dir < 0) {
        if (!pipes_start_pipe(state)) {
            pipes_schedule_reset(state);
        }
        return;
    }
    if (next_dir != prev_dir) {
        state->straight_run_remaining = pipes_pick_run_length(state);
    }
    state->dir = next_dir;
}

void screensaver_pipes_init_state(screensaver_pipes_state_t *state) {
    if (!state) {
        return;
    }
    state->rng = 0;
    state->head_x = 0;
    state->head_y = 0;
    state->head_z = 0;
    state->dir = -1;
    state->segment_progress = 0.0f;
    state->reset_delay_s = 0.0f;
    state->active_color_index = 0;
    state->straight_run_remaining = 0;
    state->frame_tick = 0;
    state->traversed_cells = 0;
    state->segment_count = 0;
}

void screensaver_pipes_reset(screensaver_pipes_state_t *state) {
    if (!state) {
        return;
    }
    state->segment_progress = 0.0f;
    state->reset_delay_s = 0.0f;
    state->dir = -1;
    state->straight_run_remaining = 0;
    state->traversed_cells = 0;
}

void screensaver_pipes_activate(screensaver_pipes_state_t *state) {
    if (!state) {
        return;
    }
    state->segment_progress = 0.0f;
    state->reset_delay_s = 0.0f;
    state->frame_tick = 0;
    state->traversed_cells = 0;
    state->segment_count = 0;
    state->straight_run_remaining = 0;
    pipes_start_pipe(state);
}

void screensaver_pipes_step(screensaver_pipes_state_t *state, float dt) {
    if (!state) {
        return;
    }

    if (state->reset_delay_s > 0.0f) {
        state->reset_delay_s -= dt;
        if (state->reset_delay_s <= 0.0f) {
            screensaver_pipes_activate(state);
        }
        return;
    }

    if (state->dir < 0 && !pipes_start_pipe(state)) {
        return;
    }

    state->segment_progress += dt * SCREENSAVER_PIPES_GROWTH_RATE;
    int steps = 0;
    while (state->segment_progress >= 1.0f && steps < 8) {
        state->segment_progress -= 1.0f;
        pipes_advance(state);
        steps++;
        if (state->reset_delay_s > 0.0f) {
            break;
        }
    }
    state->frame_tick++;
}
