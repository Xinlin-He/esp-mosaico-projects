// SPDX-License-Identifier: Apache-2.0
#include "bball_game.h"
#include <math.h>
#include <string.h>

// --- Tunables -------------------------------------------------------------
#define AIM_RANGE 140.0f       // crosshair travel from center for aim = 1
#define POWER_SPEED 0.028f     // meter travel per logic tick
#define POWER_TO_PX 150.0f     // landing-y shift per unit of power error
#define BALL_START_R 26.0f     // ball radius at launch (screen px)
#define BALL_END_SCALE 0.80f   // ball radius fraction when it reaches the rim
                               // (mild perspective; keeps ball ~half the rim
                               //  width at the hoop so the proportion reads right)
#define LAUNCH_X 240.0f
#define LAUNCH_Y 432.0f
#define FLIGHT_TICKS 22        // ticks for the approach arc (snappy reload)
#define SETTLE_TICKS 16        // ticks of drop / bounce after the rim plane
#define GRAVITY 0.55f
#define POPUP_TICKS 26         // floating score dwell

#define RIM_HOLE 0.90f         // fraction of the outer lip that still counts as in
#define HOFFSET_MAX 30.0f      // max horizontal hoop offset from center
#define WIND_MAX 15.0f         // max lateral wind, screen px
#define SWEEP_AMP 82.0f        // full-difficulty hoop sweep width, screen px
#define SWEEP_SPEED 0.045f     // full-difficulty hoop sweep speed, rad/tick
#define SWEEP_SPEED_MIN 0.022f // sweep speed right when the rim starts moving
#define SWEEP_HOLD_TICKS (3 * BBALL_TICK_HZ) // rim stays centred for the first 3s
#define SWEEP_RAMP_TICKS (20.0f * BBALL_TICK_HZ) // then widens to full over 20s
#define FEVER_AMP 92.0f        // fever hoop horizontal sweep, screen px
#define FEVER_SPEED 0.085f     // fever hoop sweep speed, rad/tick

// Upward swipe. Screen Y grows downward, so travel is (start_y - y).
// A swipe of SWIPE_IDEAL_DY lands on the rim; SWIPE_DY_SPAN is the pixel
// travel that covers the whole power bar around that. The green window is
// about ±40 px of that ideal flick. Shorter than SWIPE_MIN_DY is a tap.
#define SWIPE_MIN_DY 56.0f
#define SWIPE_IDEAL_DY 175.0f
#define SWIPE_DY_SPAN 380.0f
#define SWIPE_AIM_DX 140.0f    // sideways travel that reaches aim = ±1

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t next_random(bball_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x ? x : 0x6d2b79f5U;
}

// Uniform float in [0, 1).
static float rand01(bball_game_t *g)
{
    return (float)(next_random(g) & 0xffffffU) / (float)0x1000000U;
}

// Triangular random in (-1, 1), peaked at 0 (sum of two uniforms).
static float rand_tri(bball_game_t *g)
{
    return rand01(g) + rand01(g) - 1.0f;
}

static void apply_distance(bball_game_t *g, uint8_t distance)
{
    g->distance = distance;
    switch (distance) {
    case BBALL_DIST_NEAR:
        g->hoop_cy = 206.0f; g->rim_rx = 58.0f; g->rim_ry = 19.0f;
        g->ideal_power = 0.50f; g->points = 2;
        break;
    case BBALL_DIST_MID:
        g->hoop_cy = 190.0f; g->rim_rx = 46.0f; g->rim_ry = 17.0f;
        g->ideal_power = 0.63f; g->points = 2;
        break;
    case BBALL_DIST_FAR:
    default:
        g->hoop_cy = 176.0f; g->rim_rx = 40.0f; g->rim_ry = 14.0f;
        g->ideal_power = 0.77f; g->points = 3;
        break;
    }
}

// Roll a fresh hoop for the next ball. The rim always keeps the same size and
// height (like a real arcade machine) and sweeps sideways around the centre;
// only the sweep speed/width and wind change. Fever sweeps wider and faster
// with no wind.
static void configure_shot(bball_game_t *g)
{
    apply_distance(g, BBALL_DIST_MID); // fixed rim size + height
    g->hoop_base_cx = LAUNCH_X;        // sweep is centred on the launch column
    g->wind = g->fever ? 0.0f : rand_tri(g) * WIND_MAX;
    g->hoop_cx = g->hoop_base_cx;
}

// Put a fresh ball in the hand. Balls already in the air are left alone.
static void arm_hand(bball_game_t *g)
{
    configure_shot(g);
    g->aim = 0.0f;
    g->power = 0.0f;
    g->power_dir = 1;
    g->touch_aiming = false;
    g->ball_x = LAUNCH_X;
    g->ball_y = LAUNCH_Y;
    g->ball_r = BALL_START_R;
    g->phase = ARC_AIM;
}

// Load a new ball and return to aiming. Clears anything still in the air.
static void ready_ball(bball_game_t *g)
{
    memset(g->balls, 0, sizeof(g->balls));
    g->outcome = BBALL_SHOT_NONE;
    g->rack = BBALL_RACK;
    g->reload_timer = 0;
    g->reload_full = 0;
    g->reload_mark_us = 0;
    arm_hand(g);
}

static bool any_ball_alive(const bball_game_t *g)
{
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        if (g->balls[i].alive) {
            return true;
        }
    }
    return false;
}

// Empty slot, or NULL when every airborne slot is busy.
static bball_ball_t *alloc_ball(bball_game_t *g)
{
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        if (!g->balls[i].alive) {
            return &g->balls[i];
        }
    }
    return NULL;
}

void bball_game_reset(bball_game_t *g, uint32_t seed)
{
    uint32_t best = g->best;
    bool use_imu = g->use_imu;
    memset(g, 0, sizeof(*g));
    g->best = best;
    g->use_imu = use_imu;
    g->rng = seed ? seed : 1U;
    g->phase = ARC_TITLE;
    g->match_ticks = BBALL_MATCH_TICKS;
    g->fever = false;
    configure_shot(g);
    // Prime a neutral ball for the title backdrop.
    g->ball_x = LAUNCH_X;
    g->ball_y = LAUNCH_Y;
    g->ball_r = BALL_START_R;
    g->power_dir = 1;
}

void bball_game_start(bball_game_t *g)
{
    if (g->phase != ARC_TITLE && g->phase != ARC_OVER) {
        return;
    }
    g->score = 0;
    g->streak = 0;
    g->longest_streak = 0;
    g->makes = 0;
    g->attempts = 0;
    g->fever = false;
    g->match_ticks = BBALL_MATCH_TICKS;
    g->clock_now_us = 0;
    g->clock_origin_us = 0;
    g->clock_pause_us = 0;
    g->clock_pause_at = 0;
    g->popup_timer = 0;
    g->combo_timer = 0;
    g->flash = 0;
    ready_ball(g);
}

// Launch the ball and arm the next one. Make/miss is decided when this ball
// reaches the rim, against the hoop where it is then, not where it was at
// release. The hand is free on the same tick.
static void launch_shot(bball_game_t *g)
{
    if (g->match_ticks == 0 || g->rack <= 0) {
        return;
    }
    bball_ball_t *slot = alloc_ball(g);
    if (!slot) {
        return;
    }
    float cross_x = LAUNCH_X + g->aim * AIM_RANGE;
    float land_x = cross_x + g->wind;
    float land_y = g->hoop_cy + (g->ideal_power - g->power) * POWER_TO_PX;
    float arc_h = 74.0f + g->power * 176.0f;

    bball_ball_t *b = slot;
    memset(b, 0, sizeof(*b));
    b->alive = true;
    b->sx = LAUNCH_X;
    b->sy = LAUNCH_Y;
    b->ex = land_x;
    b->ey = land_y;
    b->arc_h = arc_h;
    b->x = b->sx;
    b->y = b->sy;
    b->r = BALL_START_R;
    b->px = b->x;
    b->py = b->y;
    b->pr = b->r;
    g->attempts++;
    g->rack--;
    arm_hand(g);
    // Regen keeps its cadence. Starting it only when the rack first drops
    // below full, so a follow-up shot does not push the next ball back.
    if (g->rack < BBALL_RACK && g->reload_timer <= 0) {
        g->reload_full = BBALL_RACK_REGEN;
        g->reload_timer = g->reload_full;
        g->reload_mark_us = g->clock_now_us;
    }
}

void bball_game_aim(bball_game_t *g, float delta)
{
    if (g->phase != ARC_AIM) {
        return;
    }
    g->aim = clampf(g->aim + delta, -1.0f, 1.0f);
}

void bball_game_fire(bball_game_t *g)
{
    if (g->phase == ARC_TITLE || g->phase == ARC_OVER) {
        bball_game_start(g);
        return;
    }
    if (g->phase == ARC_AIM) {
        launch_shot(g);
    }
}

static void apply_swipe(bball_game_t *g)
{
    float dx = g->swipe_x - g->swipe_x0;
    float dy = g->swipe_y0 - g->swipe_y;
    if (dy < 0.0f) {
        dy = 0.0f;
    }
    g->aim = clampf(dx / SWIPE_AIM_DX, -1.0f, 1.0f);
    g->power = clampf(g->ideal_power + (dy - SWIPE_IDEAL_DY) / SWIPE_DY_SPAN,
                      0.0f, 1.0f);
}

void bball_game_point(bball_game_t *g, float x, float y, bool pressed)
{
    if (g->phase == ARC_TITLE) {
        if (pressed) {
            bball_game_start(g);
        }
        return;
    }
    if (g->phase == ARC_OVER) {
        if (pressed && bball_over_button_hit(x, y)) {
            bball_game_start(g);
        }
        return;
    }
    if (g->phase != ARC_AIM || g->match_ticks == 0) {
        return;
    }
    if (pressed) {
        if (!g->touch_aiming) {
            g->touch_aiming = true;
            g->swipe_x0 = x;
            g->swipe_y0 = y;
        }
        g->swipe_x = x;
        g->swipe_y = y;
        apply_swipe(g);
        return;
    }
    if (!g->touch_aiming) {
        return;
    }
    // The device touch task reports the lift as (0, 0). Keep the last
    // on-screen sample so the shot uses where the finger actually was.
    if (!(x == 0.0f && y == 0.0f)) {
        g->swipe_x = x;
        g->swipe_y = y;
    }
    g->touch_aiming = false;
    if ((g->swipe_y0 - g->swipe_y) < SWIPE_MIN_DY) {
        g->aim = 0.0f;
        return;
    }
    apply_swipe(g);
    launch_shot(g);
}

void bball_game_toggle_pause(bball_game_t *g)
{
    if (g->phase == ARC_AIM || g->phase == ARC_FLIGHT) {
        g->resume_phase = g->phase;
        g->phase = ARC_PAUSED;
    } else if (g->phase == ARC_PAUSED) {
        g->phase = g->resume_phase;
    }
}

static void spawn_popup(bball_game_t *g, uint16_t points, bball_shot_t kind,
                        int8_t hint, float mx, float my)
{
    g->popup_points = points;
    g->popup_kind = kind;
    g->popup_hint = hint;
    g->popup_mx = mx;
    g->popup_my = my;
    g->popup_x = g->hoop_cx;
    g->popup_y = g->hoop_cy - 18.0f;
    g->popup_timer = POPUP_TICKS;
}

// Which correction matters more. Screen y grows downward, so a landing below
// the rim is short (too soft) and a landing above it is long (too hard).
static int8_t miss_hint(const bball_game_t *g, const bball_ball_t *b)
{
    float dx = b->ex - g->hoop_cx;
    float dy = b->ey - g->hoop_cy;
    float end_r = BALL_START_R * BALL_END_SCALE;
    float hole = g->rim_rx * RIM_HOLE;
    float depth_over = fabsf(dy) - g->rim_ry;
    float side_over = fabsf(dx) + end_r - hole;
    if (depth_over < 0.0f) {
        depth_over = 0.0f;
    }
    if (side_over < 0.0f) {
        side_over = 0.0f;
    }
    float depth_n = depth_over / (g->rim_ry > 1.0f ? g->rim_ry : 1.0f);
    float side_n = side_over / (hole > 1.0f ? hole : 1.0f);
    if (depth_over > 0.0f && depth_n >= side_n) {
        return dy > 0.0f ? BBALL_HINT_SOFT : BBALL_HINT_HARD;
    }
    if (dx < 0.0f) {
        return BBALL_HINT_RIGHT;
    }
    return BBALL_HINT_LEFT;
}

void bball_game_accel(bball_game_t *g, float ax, float ay, float az)
{
    if (!g->use_imu || g->phase != ARC_AIM) {
        return;
    }
    // Magnitude minus 1g removes gravity, so a still device reads zero and a
    // flick reads how hard it was moved, whatever way the board is held.
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    float linear = fabsf(mag - 1.0f);
    float sample = clampf((linear - 0.12f) / 1.35f, 0.0f, 1.0f);
    if (sample >= g->power) {
        g->power = sample;
    } else {
        g->power += (sample - g->power) * 0.08f;
    }
}

static void tick_power_meter(bball_game_t *g)
{
    g->power += (float)g->power_dir * POWER_SPEED;
    if (g->power >= 1.0f) {
        g->power = 1.0f;
        g->power_dir = -1;
    } else if (g->power <= 0.0f) {
        g->power = 0.0f;
        g->power_dir = 1;
    }
}

// Award points once when the ball reaches the rim, then begin the drop/bounce.
static void begin_settle(bball_game_t *g, bball_ball_t *b)
{
    if (b->outcome == BBALL_SHOT_NONE) {
        float dx = b->ex - g->hoop_cx;
        float dy = b->ey - g->hoop_cy;
        // rim_rx is the outer lip of the orange ring. The counted hole is a
        // little inside that lip. A make still needs the whole ball inside it,
        // so a ball sitting on the orange rim is a miss.
        float end_r = BALL_START_R * BALL_END_SCALE;
        float hole = g->rim_rx * RIM_HOLE;
        bool made = (fabsf(dx) + end_r <= hole) && (fabsf(dy) <= g->rim_ry);
        b->outcome = made ? BBALL_SHOT_MADE : BBALL_SHOT_MISS;
    }
    if (!b->scored) {
        b->scored = true;
        g->outcome = b->outcome;
        g->resolve_seq++;
        if (b->outcome == BBALL_SHOT_MADE) {
            g->makes++;
            g->streak++;
            if (g->streak > g->longest_streak) {
                g->longest_streak = g->streak;
            }
            uint16_t base = (uint16_t)(g->points +
                                       (g->streak >= BBALL_HOT_STREAK ? 1U : 0U));
            uint16_t awarded = (uint16_t)(base * (g->fever ? 2U : 1U));
            g->score += awarded;
            if (g->score > g->best) {
                g->best = g->score;
            }
            g->flash = 10;
            spawn_popup(g, awarded, BBALL_SHOT_MADE, BBALL_HINT_NONE, 0.0f, 0.0f);
            if (g->streak >= 2) {
                g->combo_pop = g->streak;
                g->combo_timer = BBALL_COMBO_POP_TICKS;
            }
        } else {
            g->streak = 0;
            g->combo_timer = 0;
            spawn_popup(g, 0, BBALL_SHOT_MISS, miss_hint(g, b),
                        b->ex - g->hoop_cx, b->ey - g->hoop_cy);
        }
    }
    if (b->outcome == BBALL_SHOT_MADE) {
        // Drop through from where the ball met the rim, not from the centre.
        b->vx = 0.0f;
        b->vy = 7.0f;
    } else {
        float side = (b->ex < g->hoop_cx) ? -1.0f : 1.0f;
        if (b->ey > g->hoop_cy) {
            side = (rand01(g) < 0.5f) ? -1.0f : 1.0f; // short: kick off front rim
        }
        b->vx = side * 3.0f;
        b->vy = -4.2f; // rebound up before falling away
    }
}

static void finalize_over(bball_game_t *g)
{
    g->phase = ARC_OVER;
}

// Screen position of one ball at a fractional point along its approach arc.
static void ball_on_arc(const bball_ball_t *b, float frame, float *x, float *y,
                        float *r)
{
    float t = clampf(frame / (float)FLIGHT_TICKS, 0.0f, 1.0f);
    *x = b->sx + (b->ex - b->sx) * t;
    *y = b->sy + (b->ey - b->sy) * t - b->arc_h * 4.0f * t * (1.0f - t);
    *r = BALL_START_R * (1.0f - (1.0f - BALL_END_SCALE) * t);
}

static void tick_ball(bball_game_t *g, bball_ball_t *b)
{
    if (!b->alive) {
        return;
    }
    b->px = b->x;
    b->py = b->y;
    b->pr = b->r;
    if (b->flight_frame < FLIGHT_TICKS) {
        b->flight_frame++;
        ball_on_arc(b, (float)b->flight_frame, &b->x, &b->y, &b->r);
        if (b->flight_frame == FLIGHT_TICKS) {
            begin_settle(g, b);
        }
        return;
    }
    b->settle_frame++;
    b->x += b->vx;
    b->y += b->vy;
    b->vy += GRAVITY;
    if (b->settle_frame >= SETTLE_TICKS) {
        b->alive = false;
    }
}

static void tick_balls(bball_game_t *g)
{
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        tick_ball(g, &g->balls[i]);
    }
}

static void sweep_at(const bball_game_t *g, float at, float *cx_out)
{
    // Width and speed depend only on elapsed match time, never on how many
    // shots have been taken. A shot counter jumps the rim the instant a ball
    // is released; time does not.
    float elapsed = (float)(BBALL_MATCH_TICKS - g->match_ticks);
    float amp = 0.0f;
    if (elapsed >= (float)SWEEP_HOLD_TICKS) {
        float prog = clampf((elapsed - (float)SWEEP_HOLD_TICKS) / SWEEP_RAMP_TICKS,
                            0.0f, 1.0f);
        amp = prog * SWEEP_AMP;
        if (g->fever) {
            // Widen into the fever sweep over 2s. An instant amp step moves
            // the rim sideways wherever the sine is not zero.
            float into = (float)(BBALL_FEVER_TICKS - g->match_ticks);
            float blend = clampf(into / (2.0f * (float)BBALL_TICK_HZ), 0.0f, 1.0f);
            amp += blend * (FEVER_AMP - SWEEP_AMP);
        }
    }
    // One fixed angular rate for the whole match. Amplitude eases with time,
    // including into fever, so the rim never steps to a new position.
    if (cx_out) *cx_out = g->hoop_base_cx + sinf(at * SWEEP_SPEED_MIN) * amp;
}

// Draw position between the previous and current logic sample. The device
// presents slower than the 30 Hz logic clock, so sampling only on whole ticks
// makes the ball and the moving rim step. frac is 0 at the previous sample
// and 1 at the current one. This only fills the draw outputs; logic state is
// left untouched.
void bball_game_present(const bball_game_t *g, float frac, float *hoop_cx,
                        float *ball_x, float *ball_y, float *ball_r,
                        bball_ball_t *draw_balls)
{
    frac = clampf(frac, 0.0f, 1.0f);
    float cx = g->hoop_cx;
    if (g->phase == ARC_AIM || g->phase == ARC_FLIGHT || any_ball_alive(g)) {
        sweep_at(g, (float)g->tick + frac, &cx);
    }
    if (hoop_cx) *hoop_cx = cx;
    if (ball_x) *ball_x = g->ball_x;
    if (ball_y) *ball_y = g->ball_y;
    if (ball_r) *ball_r = g->ball_r;
    if (!draw_balls) {
        return;
    }
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        const bball_ball_t *b = &g->balls[i];
        bball_ball_t *out = &draw_balls[i];
        *out = *b;
        if (!b->alive) {
            continue;
        }
        if (b->settle_frame == 0 && b->flight_frame < FLIGHT_TICKS) {
            ball_on_arc(b, (float)b->flight_frame - 1.0f + frac,
                        &out->x, &out->y, &out->r);
        } else {
            out->x = b->px + (b->x - b->px) * frac;
            out->y = b->py + (b->y - b->py) * frac;
            out->r = b->pr + (b->r - b->pr) * frac;
        }
    }
}

// Enter fever once the clock crosses into the final stretch. Reconfigure the
// live ball so the mode change is immediate while aiming.
static void maybe_enter_fever(bball_game_t *g)
{
    if (!g->fever && g->match_ticks <= BBALL_FEVER_TICKS) {
        g->fever = true;
        if (g->phase == ARC_AIM) {
            configure_shot(g);
        }
    }
}

void bball_game_clock_us(bball_game_t *g, int64_t now_us)
{
    g->clock_now_us = now_us;
    if (g->phase == ARC_TITLE || g->phase == ARC_OVER) {
        g->clock_origin_us = 0;
        g->clock_pause_us = 0;
        g->clock_pause_at = 0;
        return;
    }
    if (g->phase == ARC_PAUSED) {
        if (g->clock_pause_at == 0) {
            g->clock_pause_at = now_us;
        }
        return;
    }
    if (g->clock_pause_at != 0) {
        int64_t held = now_us - g->clock_pause_at;
        if (held > 0) {
            g->clock_pause_us += held;
        }
        g->clock_pause_at = 0;
    }
    if (g->clock_origin_us == 0) {
        g->clock_origin_us = now_us;
    }
}

static void tick_clock(bball_game_t *g)
{
    if (g->clock_origin_us != 0) {
        int64_t elapsed = g->clock_now_us - g->clock_origin_us - g->clock_pause_us;
        if (elapsed < 0) {
            elapsed = 0;
        }
        int64_t used = (elapsed * (int64_t)BBALL_TICK_HZ) / 1000000;
        if (used > BBALL_MATCH_TICKS) {
            used = BBALL_MATCH_TICKS;
        }
        int32_t left = (int32_t)BBALL_MATCH_TICKS - (int32_t)used;
        if (left < 0) {
            left = 0;
        }
        g->match_ticks = (uint16_t)left;
    } else if (g->match_ticks > 0) {
        --g->match_ticks;
    }
    maybe_enter_fever(g);
}

static void regen_steps(bball_game_t *g, int steps)
{
    if (steps < 1 || g->rack >= BBALL_RACK) {
        if (g->rack >= BBALL_RACK) {
            g->reload_timer = 0;
        }
        return;
    }
    if (g->reload_timer <= 0) {
        g->reload_full = BBALL_RACK_REGEN;
        g->reload_timer = BBALL_RACK_REGEN;
    }
    while (steps > 0 && g->rack < BBALL_RACK) {
        if (steps < g->reload_timer) {
            g->reload_timer = (int16_t)(g->reload_timer - steps);
            return;
        }
        steps -= g->reload_timer;
        g->rack++;
        if (g->rack >= BBALL_RACK) {
            g->reload_timer = 0;
            return;
        }
        g->reload_full = BBALL_RACK_REGEN;
        g->reload_timer = BBALL_RACK_REGEN;
    }
}

// On device the return timer follows the hardware clock. The host sim still
// burns one tick per update. A full rack does not tick.
static void tick_reload(bball_game_t *g)
{
    if (g->rack >= BBALL_RACK) {
        g->reload_timer = 0;
        return;
    }
    if (g->clock_origin_us == 0) {
        regen_steps(g, 1);
        return;
    }
    if (g->reload_mark_us == 0) {
        g->reload_mark_us = g->clock_now_us;
        if (g->reload_timer <= 0) {
            g->reload_full = BBALL_RACK_REGEN;
            g->reload_timer = BBALL_RACK_REGEN;
        }
        return;
    }
    int64_t delta = g->clock_now_us - g->reload_mark_us;
    if (delta < 0) {
        g->reload_mark_us = g->clock_now_us;
        return;
    }
    int64_t steps = (delta * (int64_t)BBALL_TICK_HZ) / 1000000;
    if (steps <= 0) {
        return;
    }
    g->reload_mark_us += steps * (1000000 / BBALL_TICK_HZ);
    regen_steps(g, (int)steps);
}

void bball_game_update(bball_game_t *g)
{
    if (g->flash) {
        --g->flash;
    }
    if (g->popup_timer > 0) {
        --g->popup_timer;
    }
    if (g->combo_timer > 0) {
        --g->combo_timer;
    }
    g->tick++;

    switch (g->phase) {
    case ARC_AIM:
        tick_clock(g);
        // A swipe owns aim and power until the finger lifts. The meter keeps
        // running for keyboard shots when nobody is touching the screen.
        if (!g->use_imu && !g->touch_aiming && g->match_ticks > 0) {
            tick_power_meter(g);
        }
        sweep_at(g, (float)g->tick, &g->hoop_cx);
        tick_reload(g);
        tick_balls(g);
        // Shots already in the air still finish after the buzzer. A ball still
        // in the hand does not count, and no new shot can start.
        if (g->match_ticks == 0 && !any_ball_alive(g)) {
            finalize_over(g);
        }
        break;
    case ARC_TITLE:
    case ARC_OVER:
    case ARC_PAUSED:
    default:
        break;
    }
}

uint16_t bball_game_seconds_left(const bball_game_t *g)
{
    return (uint16_t)((g->match_ticks + BBALL_TICK_HZ - 1) / BBALL_TICK_HZ);
}

uint32_t bball_game_state_hash(const bball_game_t *g)
{
    const uint8_t *bytes = (const uint8_t *)g;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < sizeof(*g); ++i) {
        hash = (hash ^ bytes[i]) * 16777619U;
    }
    return hash;
}
