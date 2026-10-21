// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Arcade basketball machine: a single-player, first-person score attack. You
// rapidly shoot balls at the hoop before a countdown expires. A swipe up the
// screen shoots: sideways drift aims, and how far the swipe travels sets power.
// Releasing puts the ball in the air. The rack holds five balls and they can
// be shot one after another. One ball returns on a fixed timer; an empty rack
// cannot shoot.
// A shot is a make when its landing
// point falls inside the rim window. Consecutive makes build a streak bonus,
// and the final stretch triggers a "fever" frenzy with a sliding hoop and
// doubled scoring.
#define BBALL_SCREEN_W 480
#define BBALL_SCREEN_H 480

#define BBALL_MATCH_TICKS 1800  // session length in logic ticks (60s @ 30Hz)
#define BBALL_FEVER_TICKS 600   // fever begins in the final 20 seconds
#define BBALL_TICK_HZ 30
#define BBALL_HOT_STREAK 3      // streak length that unlocks the +1 hot bonus
#define BBALL_COMBO_POP_TICKS 24 // center "COMBO" punch after a streak make
#define BBALL_HINT_NONE  0
#define BBALL_HINT_SOFT  1      // landed short of the rim: swipe farther
#define BBALL_HINT_HARD  2      // landed past the rim: swipe shorter
#define BBALL_HINT_LEFT  3      // landed right of the rim: aim left
#define BBALL_HINT_RIGHT 4      // landed left of the rim: aim right

#define BBALL_RACK 5            // balls ready to shoot
#define BBALL_RACK_REGEN 30     // one ball returns every 1.0s
#define BBALL_BALL_CAP 8        // airborne balls; larger than the rack so a
                                // returned ball can be shot before the last one lands

// Game-over restart control. A tap anywhere else leaves the score on screen.
#define BBALL_OVER_BTN_W 220
#define BBALL_OVER_BTN_H 44
#define BBALL_OVER_BTN_X ((BBALL_SCREEN_W - BBALL_OVER_BTN_W) / 2)
#define BBALL_OVER_BTN_Y 288

static inline bool bball_over_button_hit(float x, float y)
{
    return x >= (float)BBALL_OVER_BTN_X &&
           x < (float)(BBALL_OVER_BTN_X + BBALL_OVER_BTN_W) &&
           y >= (float)BBALL_OVER_BTN_Y &&
           y < (float)(BBALL_OVER_BTN_Y + BBALL_OVER_BTN_H);
}

// Distance tiers select hoop geometry, ideal power and score value.
#define BBALL_DIST_NEAR 0
#define BBALL_DIST_MID 1
#define BBALL_DIST_FAR 2

typedef enum {
    ARC_TITLE,   // start screen
    ARC_AIM,     // aiming, power meter oscillating, clock running
    ARC_FLIGHT,  // ball in the air, clock running
    ARC_OVER,    // time up, results screen
    ARC_PAUSED,  // paused; resumes to resume_phase
} bball_phase_t;

typedef enum {
    BBALL_SHOT_NONE,
    BBALL_SHOT_MADE,
    BBALL_SHOT_MISS,
} bball_shot_t;

// One ball that has left the hand. The ball waiting to be shot stays on the
// game struct; these only exist while they are flying or settling.
typedef struct {
    bool alive;
    bool scored;
    bball_shot_t outcome;
    int16_t flight_frame;
    int16_t settle_frame;
    float sx, sy;          // launch point
    float ex, ey;          // landing point at the rim plane
    float arc_h;           // parabola apex height
    float x, y, r;         // current sample
    float px, py, pr;      // previous sample, for smooth drawing
    float vx, vy;          // settle velocity after the rim plane
} bball_ball_t;

typedef struct {
    bball_phase_t phase;
    bball_phase_t resume_phase; // phase to restore when leaving ARC_PAUSED

    // Session score and stats.
    uint32_t score, best;
    uint16_t match_ticks;       // ticks remaining on the countdown
    // Wall-clock countdown. Zero origin means the host sim, which still
    // burns one tick per logic update. The device fills these from the
    // hardware timer so a slow frame does not stretch the 60 seconds.
    int64_t clock_now_us;
    int64_t clock_origin_us;
    int64_t clock_pause_us;
    int64_t clock_pause_at;
    uint16_t streak, longest_streak;
    uint16_t makes, attempts;
    bool fever;                 // frenzy mode active (final stretch)

    // Current shot configuration (re-rolled for every ball).
    uint8_t distance;   // BBALL_DIST_*
    float hoop_cx;      // rim center, screen x (oscillates during fever)
    float hoop_base_cx; // fever oscillation center
    float hoop_cy;      // rim center, screen y
    float rim_rx;       // rim half width, screen px
    float rim_ry;       // rim vertical tolerance, screen px
    float ideal_power;  // power value that lands the ball at the rim
    uint8_t points;     // base value of the current hoop (2 or 3)
    float wind;         // lateral drift pushed onto the ball, screen px

    // Aim and oscillating power meter.
    float aim;         // crosshair position in [-1, 1]
    float power;       // current meter value in [0, 1]
    int8_t power_dir;  // meter travel direction, +1 or -1
    bool use_imu;      // power comes from a flick, not the oscillating bar
    bool touch_aiming; // a swipe is in progress
    float swipe_x0, swipe_y0; // finger-down point
    float swipe_x, swipe_y;   // latest on-screen sample of that swipe

    // Ball waiting in the hand. rack is how many shots are ready. reload_*
    // counts down to the next ball coming back, and only blocks a shot at 0.
    float ball_x, ball_y, ball_r;
    int16_t rack;
    int16_t reload_timer;
    int16_t reload_full;
    int64_t reload_mark_us; // hardware-timer sample when the regen last moved
    // Balls already released. A free slot is !alive.
    bball_ball_t balls[BBALL_BALL_CAP];
    bball_shot_t outcome; // latest ball to reach the rim
    uint16_t resolve_seq; // increments once per ball that reaches the rim

    // Floating result after a shot resolves. On a miss, popup_hint says what
    // to change and popup_m* is the landing offset from the rim.
    int16_t popup_timer;
    float popup_x, popup_y;
    float popup_mx, popup_my;
    uint16_t popup_points;
    int8_t popup_hint;
    bball_shot_t popup_kind;

    // Center combo punch. combo_pop is the streak being shown.
    int16_t combo_timer;
    uint16_t combo_pop;

    uint8_t flash;     // brief made pulse
    uint32_t rng, tick;
} bball_game_t;

// Reset to the title screen and seed the RNG (best score is preserved).
void bball_game_reset(bball_game_t *g, uint32_t seed);
// Begin (or restart) a session from the title or game-over screen.
void bball_game_start(bball_game_t *g);
// Nudge the crosshair (keyboard left/right); delta is in aim units.
void bball_game_aim(bball_game_t *g, float delta);
// Shoot: lock the current power meter and launch the ball.
void bball_game_fire(bball_game_t *g);
// Swipe up to shoot. Sideways drift aims; upward travel sets power. A short
// tap does not shoot. A title tap starts a game. On the results screen only
// the yellow PLAY AGAIN button starts the next one.
void bball_game_point(bball_game_t *g, float x, float y, bool pressed);
// Feed accelerometer samples in g. While aiming, the flick strength drives
// power instead of the oscillating meter. Ignored unless use_imu is set.
void bball_game_accel(bball_game_t *g, float ax, float ay, float az);
void bball_game_toggle_pause(bball_game_t *g);
void bball_game_update(bball_game_t *g);
// Sample the hardware timer. now_us is microseconds from esp_timer_get_time().
// Call once per device frame, including paused frames, before bball_game_update.
void bball_game_clock_us(bball_game_t *g, int64_t now_us);
// Smooth draw samples between logic ticks. frac is 0..1 within the current
// tick. Outputs may be NULL. draw_balls, when set, receives BBALL_BALL_CAP
// interpolated copies. Does not change game state.
void bball_game_present(const bball_game_t *g, float frac, float *hoop_cx,
                        float *ball_x, float *ball_y, float *ball_r,
                        bball_ball_t *draw_balls);
uint32_t bball_game_state_hash(const bball_game_t *g);

// Seconds remaining on the countdown, for the HUD.
uint16_t bball_game_seconds_left(const bball_game_t *g);
