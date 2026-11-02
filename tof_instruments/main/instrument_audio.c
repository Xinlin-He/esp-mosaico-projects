// SPDX-License-Identifier: Apache-2.0

#include "instrument_audio.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "guitar_chords.h"

#define AUDIO_RATE              BSP_AUDIO_DEFAULT_SAMPLE_RATE
#define AUDIO_CHANNELS          BSP_AUDIO_DEFAULT_CHANNELS
#define AUDIO_CHUNK_FRAMES      576
#define AUDIO_RENDER_DIVISOR    4
#define SYNTH_RATE              (AUDIO_RATE / AUDIO_RENDER_DIVISOR)
#define AUDIO_VOICES            8
#define AUDIO_QUEUE_LENGTH      24
#define SINE_TABLE_SIZE         256
#define DEFAULT_VOLUME_PCT      80U
#define BACKING_MASTER_GAIN     2.70f
#define DRUM_MASTER_GAIN        1.24f
#define GUITAR_MASTER_GAIN      1.35f
#define PI_F                    3.14159265358979323846f

#define GUITAR_SAMPLE_RATE      16000U
#define STRUM_STEP_US           12000
#define NOTE_GAIN_Q8            216
#define UP_STROKE_NUM           72
#define UP_STROKE_DEN           100
#define PENDING_PLUCK_MAX       12

#define PCM_SYM(sym) extern const uint8_t sym[] asm("_binary_" #sym);
PCM_SYM(n040_pcm_start) PCM_SYM(n040_pcm_end)
PCM_SYM(n041_pcm_start) PCM_SYM(n041_pcm_end)
PCM_SYM(n043_pcm_start) PCM_SYM(n043_pcm_end)
PCM_SYM(n045_pcm_start) PCM_SYM(n045_pcm_end)
PCM_SYM(n047_pcm_start) PCM_SYM(n047_pcm_end)
PCM_SYM(n048_pcm_start) PCM_SYM(n048_pcm_end)
PCM_SYM(n050_pcm_start) PCM_SYM(n050_pcm_end)
PCM_SYM(n052_pcm_start) PCM_SYM(n052_pcm_end)
PCM_SYM(n053_pcm_start) PCM_SYM(n053_pcm_end)
PCM_SYM(n055_pcm_start) PCM_SYM(n055_pcm_end)
PCM_SYM(n057_pcm_start) PCM_SYM(n057_pcm_end)
PCM_SYM(n059_pcm_start) PCM_SYM(n059_pcm_end)
PCM_SYM(n060_pcm_start) PCM_SYM(n060_pcm_end)
PCM_SYM(n062_pcm_start) PCM_SYM(n062_pcm_end)
PCM_SYM(n064_pcm_start) PCM_SYM(n064_pcm_end)
PCM_SYM(n065_pcm_start) PCM_SYM(n065_pcm_end)
PCM_SYM(n066_pcm_start) PCM_SYM(n066_pcm_end)
PCM_SYM(n067_pcm_start) PCM_SYM(n067_pcm_end)

typedef struct {
    uint8_t midi;
    const uint8_t *start;
    const uint8_t *end;
} note_span_t;

static const note_span_t GUITAR_NOTES[] = {
    {40, n040_pcm_start, n040_pcm_end}, {41, n041_pcm_start, n041_pcm_end},
    {43, n043_pcm_start, n043_pcm_end}, {45, n045_pcm_start, n045_pcm_end},
    {47, n047_pcm_start, n047_pcm_end}, {48, n048_pcm_start, n048_pcm_end},
    {50, n050_pcm_start, n050_pcm_end}, {52, n052_pcm_start, n052_pcm_end},
    {53, n053_pcm_start, n053_pcm_end}, {55, n055_pcm_start, n055_pcm_end},
    {57, n057_pcm_start, n057_pcm_end}, {59, n059_pcm_start, n059_pcm_end},
    {60, n060_pcm_start, n060_pcm_end}, {62, n062_pcm_start, n062_pcm_end},
    {64, n064_pcm_start, n064_pcm_end}, {65, n065_pcm_start, n065_pcm_end},
    {66, n066_pcm_start, n066_pcm_end}, {67, n067_pcm_start, n067_pcm_end},
};

typedef enum {
    AUDIO_EVENT_DRUM_HIT = 0,
    AUDIO_EVENT_START_SONG,
    AUDIO_EVENT_STOP_SONG,
    AUDIO_EVENT_SET_VOLUME_PCT,
    AUDIO_EVENT_GUITAR_PLUCK,
} audio_event_kind_t;

typedef struct {
    audio_event_kind_t kind;
    kodou_hit_t hit;
    kodou_judgement_t judgement;
    uint8_t strength;
    kodou_song_t song;
    uint8_t volume_pct;
    uint8_t string_index;
    uint8_t midi;
    uint8_t gain_q8;
    int64_t due_us;
} audio_event_t;

typedef struct {
    bool active;
    kodou_hit_t hit;
    float envelope;
    float decay;
    float body_envelope;
    float body_decay;
    float previous_noise;
    float high_pass;
    uint32_t noise_state;
    uint32_t phase[3];
    uint32_t step[3];
    uint32_t frames_left;
} drum_voice_t;

typedef struct {
    const int16_t *pcm;
    size_t frames;
    uint32_t pos_q16;
    uint32_t step_q16;
    int gain_q8;
    bool active;
} guitar_voice_t;

static const char *TAG = "inst_audio";
static QueueHandle_t s_queue;
static esp_codec_dev_handle_t s_speaker;
static drum_voice_t s_drum_voices[AUDIO_VOICES];
static guitar_voice_t s_guitar_voices[GUITAR_STRING_COUNT];
static audio_event_t s_pending_plucks[PENDING_PLUCK_MAX];
static int s_pending_pluck_count;

static float s_sine[SINE_TABLE_SIZE];
static uint32_t s_song_frame;
static uint32_t s_next_step;
static uint16_t s_song_step;
static uint32_t s_melody_phase;
static uint32_t s_melody_step;
static uint32_t s_melody_phase2;
static uint32_t s_melody_step2;
static uint32_t s_melody_phase3;
static uint32_t s_melody_step3;
static float s_melody_envelope;
static uint32_t s_bass_phase;
static uint32_t s_bass_step;
static float s_bass_envelope;
static uint32_t s_pluck_phase;
static uint32_t s_pluck_phase2;
static uint32_t s_pluck_step;
static float s_pluck_envelope;
static float s_hat_envelope;
static float s_crash_envelope;
static uint32_t s_crash_phase;
static uint32_t s_crash_step;
static uint32_t s_backing_noise_state = 0x6d2b79f5U;
static float s_backing_previous_noise;
static float s_backing_high_pass;
static uint32_t s_vibrato_phase;
static uint32_t s_vibrato_step;
static kodou_song_t s_song;
static bool s_song_playing;
static float s_backing_duck = 1.0f;
static float s_volume_scale = 0.80f;

static const note_span_t *find_guitar_note(uint8_t midi)
{
    for (size_t i = 0; i < sizeof(GUITAR_NOTES) / sizeof(GUITAR_NOTES[0]); ++i) {
        if (GUITAR_NOTES[i].midi == midi) {
            return &GUITAR_NOTES[i];
        }
    }
    return NULL;
}

static void start_guitar_voice(const audio_event_t *msg)
{
    const note_span_t *note = find_guitar_note(msg->midi);
    if (!note || msg->string_index >= GUITAR_STRING_COUNT) {
        return;
    }
    const size_t bytes = (size_t)(note->end - note->start);
    guitar_voice_t *v = &s_guitar_voices[msg->string_index];
    v->pcm = (const int16_t *)note->start;
    v->frames = bytes / sizeof(int16_t);
    v->pos_q16 = 0;
    v->step_q16 = (uint32_t)((GUITAR_SAMPLE_RATE << 16) / SYNTH_RATE);
    v->gain_q8 = msg->gain_q8;
    v->active = v->frames > 0;
}

static void dispatch_due_plucks(int64_t now_us)
{
    int i = 0;
    while (i < s_pending_pluck_count) {
        if (s_pending_plucks[i].due_us > now_us) {
            ++i;
            continue;
        }
        start_guitar_voice(&s_pending_plucks[i]);
        s_pending_plucks[i] = s_pending_plucks[s_pending_pluck_count - 1];
        --s_pending_pluck_count;
    }
}

static float render_guitar_voices(void)
{
    int32_t acc = 0;
    for (int i = 0; i < GUITAR_STRING_COUNT; ++i) {
        guitar_voice_t *v = &s_guitar_voices[i];
        if (!v->active) {
            continue;
        }
        const size_t idx = (size_t)(v->pos_q16 >> 16);
        if (idx >= v->frames) {
            v->active = false;
            continue;
        }
        acc += (int32_t)v->pcm[idx] * v->gain_q8;
        v->pos_q16 += v->step_q16;
        if ((size_t)(v->pos_q16 >> 16) >= v->frames) {
            v->active = false;
        }
    }
    return ((float)(acc / 256)) / 32768.0f;
}

static uint32_t frequency_step(float hz)
{
    return (uint32_t)(hz * (4294967296.0 / (double)SYNTH_RATE));
}

static float sine_sample(uint32_t *phase, uint32_t step)
{
    *phase += step;
    return s_sine[*phase >> 24];
}

static float noise_sample(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return (float)(int32_t)value / 2147483648.0f;
}

static float midi_frequency(uint8_t note)
{
    return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

static drum_voice_t *allocate_drum_voice(void)
{
    drum_voice_t *quietest = &s_drum_voices[0];
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        if (!s_drum_voices[i].active) return &s_drum_voices[i];
        if (s_drum_voices[i].envelope + s_drum_voices[i].body_envelope <
            quietest->envelope + quietest->body_envelope) {
            quietest = &s_drum_voices[i];
        }
    }
    return quietest;
}

static void start_drum(const audio_event_t *event)
{
    drum_voice_t *voice = allocate_drum_voice();
    memset(voice, 0, sizeof(*voice));
    voice->active = true;
    voice->hit = event->hit;
    voice->noise_state = esp_random() | 1U;
    const float velocity = 0.55f + (float)event->strength * 0.0045f;
    s_backing_duck = event->hit == KODOU_HIT_CENTER ? 0.58f : 0.68f;

    if (event->hit == KODOU_HIT_CENTER) {
        voice->frames_left = (uint32_t)(0.34f * SYNTH_RATE);
        voice->envelope = 0.28f * velocity;
        voice->body_envelope = 0.88f * velocity;
        voice->step[0] = frequency_step(116.0f);
        voice->step[1] = frequency_step(178.0f);
        voice->step[2] = frequency_step(242.0f);
        voice->body_decay = 0.99912f;
        voice->high_pass = 0.0f;
    } else {
        voice->frames_left = (uint32_t)(0.16f * SYNTH_RATE);
        voice->envelope = 0.74f * velocity;
        voice->body_envelope = 0.20f * velocity;
        voice->step[0] = frequency_step(720.0f);
        voice->step[1] = frequency_step(1030.0f);
        voice->step[2] = frequency_step(1480.0f);
        voice->body_decay = 0.99740f;
    }
    voice->decay = expf(logf(0.0008f) / (float)voice->frames_left);
}

static float render_drum(drum_voice_t *voice)
{
    if (!voice->active) return 0.0f;
    const float noise = noise_sample(&voice->noise_state);
    voice->high_pass = noise - voice->previous_noise +
                       0.84f * voice->high_pass;
    voice->previous_noise = noise;
    const float tone = 0.52f * sine_sample(&voice->phase[0], voice->step[0]) +
                       0.30f * sine_sample(&voice->phase[1], voice->step[1]) +
                       0.18f * sine_sample(&voice->phase[2], voice->step[2]);
    float sample;
    if (voice->hit == KODOU_HIT_CENTER) {
        sample = voice->body_envelope * tone +
                 voice->envelope * (0.55f * voice->high_pass + 0.20f * tone);
    } else {
        sample = voice->envelope * (0.76f * tone +
                 0.24f * voice->high_pass) +
                 voice->body_envelope * sine_sample(&voice->phase[0],
                                                     voice->step[0]);
    }
    voice->envelope *= voice->decay;
    voice->body_envelope *= voice->body_decay;
    if (--voice->frames_left == 0 ||
        (voice->envelope < 0.0005f && voice->body_envelope < 0.0005f)) {
        voice->active = false;
    }
    return sample;
}

static void backing_step(void)
{
    static const float shanhai_melody[] = {
        293.66f, 392.00f, 440.00f, 392.00f,
        329.63f, 440.00f, 493.88f, 440.00f,
        293.66f, 392.00f, 523.25f, 493.88f,
        440.00f, 392.00f, 329.63f, 261.63f,
    };
    static const float zhuque_melody[] = {
        329.63f, 440.00f, 493.88f, 587.33f,
        493.88f, 659.25f, 587.33f, 493.88f,
        392.00f, 493.88f, 587.33f, 659.25f,
        587.33f, 493.88f, 440.00f, 329.63f,
    };
    static const uint8_t pozhen_lead[64] = {
        62, 0, 67, 0, 69, 0, 72, 0, 74, 0, 72, 69, 67, 0, 62, 0,
        62, 0, 65, 67, 69, 0, 67, 65, 62, 0, 69, 0, 72, 69, 67, 0,
        74, 0, 77, 0, 79, 77, 74, 72, 69, 0, 72, 74, 77, 0, 74, 0,
        79, 77, 74, 72, 69, 72, 74, 77, 79, 0, 81, 79, 77, 74, 72, 0,
    };
    static const uint8_t pozhen_pluck[16] = {
        50, 0, 57, 0, 53, 0, 57, 0, 48, 0, 55, 0, 50, 0, 57, 0,
    };
    static const uint8_t pozhen_bass[16] = {
        38, 0, 38, 0, 41, 0, 41, 0, 36, 0, 36, 0, 38, 0, 45, 0,
    };

    if (s_song == KODOU_SONG_POZHEN) {
        const uint16_t total_step = s_song_step;
        const uint8_t phrase_step = (uint8_t)(total_step & 63U);
        const uint8_t bar_step = (uint8_t)(total_step & 15U);
        const uint8_t section = (uint8_t)(total_step >> 6);
        const uint8_t lead_note = pozhen_lead[phrase_step];
        if (lead_note != 0U) {
            const float frequency = midi_frequency(lead_note);
            s_melody_step = frequency_step(frequency);
            s_melody_step2 = frequency_step(frequency * 2.003f);
            s_melody_step3 = frequency_step(frequency * 3.006f);
            s_melody_envelope = section == 0U ? 0.115f : 0.145f;
        }
        if ((bar_step & 1U) == 0U) {
            const uint8_t pluck_note = pozhen_pluck[bar_step];
            if (pluck_note != 0U) {
                s_pluck_step = frequency_step(midi_frequency(pluck_note));
                s_pluck_envelope = section == 0U ? 0.085f : 0.115f;
            }
        }
        if ((bar_step & 3U) == 0U) {
            const uint8_t bass_note = pozhen_bass[bar_step];
            s_bass_step = frequency_step(midi_frequency(bass_note));
            s_bass_envelope = section == 0U ? 0.17f : 0.23f;
        }
        if ((bar_step & (section >= 1U ? 1U : 3U)) == 0U) {
            s_hat_envelope = section >= 2U ? 0.024f : 0.016f;
        }
        if (bar_step == 0U && (total_step == 16U ||
            (total_step != 0U && phrase_step == 0U))) {
            s_crash_envelope = 0.042f;
        }
        ++s_song_step;
        return;
    }

    const float *melody = shanhai_melody;
    if (s_song == KODOU_SONG_ZHUQUE) melody = zhuque_melody;
    const uint8_t phrase_step = (uint8_t)(s_song_step & 15U);
    const uint8_t bar_step = (uint8_t)(s_song_step & 7U);
    const float note = melody[phrase_step];
    s_melody_step = frequency_step(note);
    s_melody_step2 = frequency_step(note * 2.01f);
    s_melody_step3 = frequency_step(note * 3.00f);
    s_melody_envelope = (phrase_step & 1U) ? 0.082f : 0.135f;
    if ((phrase_step & 1U) == 0U) {
        float bass = (phrase_step & 3U) ? 73.42f : 65.41f;
        if (s_song == KODOU_SONG_ZHUQUE) {
            bass = (phrase_step & 3U) ? 98.00f : 82.41f;
        }
        s_bass_step = frequency_step(bass);
        s_bass_envelope = 0.235f;
        s_pluck_step = frequency_step(note * 0.5f);
        s_pluck_envelope = s_song == KODOU_SONG_ZHUQUE ? 0.105f : 0.088f;
    }
    s_hat_envelope = (bar_step & 1U) == 0U ? 0.020f : 0.012f;
    if (phrase_step == 0U && s_song_step != 0U) {
        s_crash_envelope = 0.030f;
    }
    ++s_song_step;
}

static float render_backing(void)
{
    if (!s_song_playing) return 0.0f;
    uint32_t bpm = 120U;
    uint32_t duration_seconds = 24U;
    if (s_song == KODOU_SONG_ZHUQUE) {
        bpm = 136U;
        duration_seconds = 22U;
    } else if (s_song == KODOU_SONG_POZHEN) {
        bpm = 146U;
        duration_seconds = 26U;
    }
    const uint32_t step_frames = s_song == KODOU_SONG_POZHEN
        ? (SYNTH_RATE * 60U) / (bpm * 4U)
        : (SYNTH_RATE * 60U) / (bpm * 2U);
    const uint32_t song_frames = duration_seconds * SYNTH_RATE;
    if (s_song_frame >= s_next_step) {
        backing_step();
        s_next_step += step_frames;
    }
    float lead = sine_sample(&s_melody_phase, s_melody_step);
    if (s_song == KODOU_SONG_POZHEN) {
        const float vibrato = sine_sample(&s_vibrato_phase, s_vibrato_step);
        const int32_t bend = (int32_t)((float)s_melody_step * vibrato * 0.0045f);
        s_melody_phase += (uint32_t)bend;
        s_melody_phase2 += (uint32_t)(bend * 2);
        s_melody_phase3 += (uint32_t)(bend * 3);
        lead = 0.54f * lead +
               0.29f * sine_sample(&s_melody_phase2, s_melody_step2) +
               0.17f * sine_sample(&s_melody_phase3, s_melody_step3);
    } else {
        lead = 0.72f * lead +
               0.20f * sine_sample(&s_melody_phase2, s_melody_step2) +
               0.08f * sine_sample(&s_melody_phase3, s_melody_step3);
    }
    float sample = lead * s_melody_envelope;
    float bass = sine_sample(&s_bass_phase, s_bass_step);
    const float bass_second = s_sine[(s_bass_phase >> 23) & 0xFFU];
    const float bass_third = s_sine[((uint32_t)(s_bass_phase * 3U)) >> 24];
    bass = 0.61f * bass + 0.27f * bass_second + 0.12f * bass_third;
    if (s_song == KODOU_SONG_POZHEN) {
        s_melody_envelope *= 0.99944f;
        s_bass_envelope *= 0.99896f;
    } else {
        s_melody_envelope *= 0.99928f;
        s_bass_envelope *= 0.99892f;
    }
    const float pluck = 0.68f * sine_sample(&s_pluck_phase, s_pluck_step) +
                        0.32f * sine_sample(&s_pluck_phase2,
                                            s_pluck_step * 2U);
    const float noise = noise_sample(&s_backing_noise_state);
    s_backing_high_pass = noise - s_backing_previous_noise +
                          0.86f * s_backing_high_pass;
    s_backing_previous_noise = noise;
    const float hat = s_backing_high_pass * s_hat_envelope;
    const float crash = (0.74f * s_backing_high_pass +
        0.26f * sine_sample(&s_crash_phase, s_crash_step)) *
        s_crash_envelope;
    sample += bass * s_bass_envelope + pluck * s_pluck_envelope +
              hat + crash;
    s_pluck_envelope *= 0.99836f;
    s_hat_envelope *= 0.98963f;
    s_crash_envelope *= 0.99910f;
    if (++s_song_frame >= song_frames) {
        s_song_frame = 0;
        s_next_step = 0;
        s_song_step = 0;
    }
    return sample;
}

static int16_t render_sample(void)
{
    float mixed = render_backing() * BACKING_MASTER_GAIN * s_backing_duck;
    s_backing_duck += (1.0f - s_backing_duck) * 0.0014f;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        mixed += render_drum(&s_drum_voices[i]) * DRUM_MASTER_GAIN;
    }
    mixed += render_guitar_voices() * GUITAR_MASTER_GAIN;
    mixed *= s_volume_scale;
    mixed = mixed / (1.0f + fabsf(mixed));
    return (int16_t)(mixed * 32767.0f);
}

static void start_backing(kodou_song_t song)
{
    s_song = song < KODOU_SONG_COUNT ? song : KODOU_SONG_SHANHAI;
    s_song_playing = true;
    s_song_frame = 0;
    s_next_step = 0;
    s_song_step = 0;
    s_melody_phase = 0;
    s_melody_phase2 = 0;
    s_melody_phase3 = 0;
    s_bass_phase = 0;
    s_pluck_phase = 0;
    s_pluck_phase2 = 0;
    s_vibrato_phase = 0;
    s_vibrato_step = frequency_step(5.25f);
    s_crash_phase = 0;
    s_crash_step = frequency_step(1840.0f);
    s_melody_envelope = 0.0f;
    s_bass_envelope = 0.0f;
    s_pluck_envelope = 0.0f;
    s_hat_envelope = 0.0f;
    s_crash_envelope = 0.0f;
    s_backing_duck = 1.0f;
}

static void stop_backing(void)
{
    s_song_playing = false;
    s_melody_envelope = 0.0f;
    s_bass_envelope = 0.0f;
    s_pluck_envelope = 0.0f;
    s_hat_envelope = 0.0f;
    s_crash_envelope = 0.0f;
    s_backing_duck = 1.0f;
}

static void audio_task(void *arg)
{
    (void)arg;
    int16_t pcm[AUDIO_CHUNK_FRAMES * AUDIO_CHANNELS];
    while (true) {
        const int64_t now_us = esp_timer_get_time();
        audio_event_t event;
        while (xQueueReceive(s_queue, &event, 0) == pdTRUE) {
            if (event.kind == AUDIO_EVENT_START_SONG) {
                start_backing(event.song);
            } else if (event.kind == AUDIO_EVENT_STOP_SONG) {
                stop_backing();
            } else if (event.kind == AUDIO_EVENT_SET_VOLUME_PCT) {
                const uint8_t pct = event.volume_pct > 100U ? 100U : event.volume_pct;
                s_volume_scale = (float)pct / 100.0f;
                const int codec_vol = pct == 0U ? 0 : (50 + (pct * 50) / 100);
                (void)esp_codec_dev_set_out_vol(s_speaker, codec_vol);
            } else if (event.kind == AUDIO_EVENT_GUITAR_PLUCK) {
                if (s_pending_pluck_count < PENDING_PLUCK_MAX) {
                    s_pending_plucks[s_pending_pluck_count++] = event;
                }
            } else if (event.kind == AUDIO_EVENT_DRUM_HIT) {
                start_drum(&event);
            }
        }
        dispatch_due_plucks(now_us);
        for (size_t frame = 0; frame < AUDIO_CHUNK_FRAMES;
             frame += AUDIO_RENDER_DIVISOR) {
            const int16_t sample = render_sample();
            for (size_t repeat = 0; repeat < AUDIO_RENDER_DIVISOR; ++repeat) {
                for (size_t channel = 0; channel < AUDIO_CHANNELS; ++channel) {
                    pcm[(frame + repeat) * AUDIO_CHANNELS + channel] = sample;
                }
            }
        }
        if (esp_codec_dev_write(s_speaker, pcm, sizeof(pcm)) !=
            ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

esp_err_t instrument_audio_start(void)
{
    if (s_queue) {
        return ESP_OK;
    }
    for (size_t i = 0; i < SINE_TABLE_SIZE; ++i) {
        s_sine[i] = sinf(2.0f * PI_F * (float)i / (float)SINE_TABLE_SIZE);
    }
    s_queue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(audio_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    s_speaker = bsp_audio_codec_speaker_init();
    if (!s_speaker) return ESP_FAIL;
    esp_codec_dev_sample_info_t info = {
        .sample_rate = AUDIO_RATE,
        .bits_per_sample = BSP_AUDIO_DEFAULT_BITS_PER_SAMPLE,
        .channel = AUDIO_CHANNELS,
        .channel_mask = 0,
    };
    if (esp_codec_dev_open(s_speaker, &info) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_speaker, 90) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    stop_backing();
    if (xTaskCreate(audio_task, "inst_audio", 6400, NULL, 8, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Unified Taiko + 6-string Guitar audio engine ready");
    return ESP_OK;
}

esp_err_t instrument_audio_start_song(kodou_song_t song)
{
    if (!s_queue || song >= KODOU_SONG_COUNT) return ESP_ERR_INVALID_ARG;
    const audio_event_t event = {
        .kind = AUDIO_EVENT_START_SONG,
        .song = song,
    };
    return xQueueSend(s_queue, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t instrument_audio_stop_song(void)
{
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    const audio_event_t event = {.kind = AUDIO_EVENT_STOP_SONG};
    return xQueueSend(s_queue, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t instrument_audio_set_volume_pct(uint8_t volume_pct)
{
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    const audio_event_t event = {
        .kind = AUDIO_EVENT_SET_VOLUME_PCT,
        .volume_pct = volume_pct > 100U ? 100U : volume_pct,
    };
    return xQueueSend(s_queue, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t instrument_audio_set_volume_level(uint8_t level)
{
    static const uint8_t level_to_pct[4] = {0, 60, 80, 100};
    return instrument_audio_set_volume_pct(level_to_pct[level & 3U]);
}

esp_err_t instrument_audio_drum_hit(kodou_hit_t hit, kodou_judgement_t judgement,
                                    uint8_t strength)
{
    if (!s_queue || (hit != KODOU_HIT_CENTER && hit != KODOU_HIT_RIM)) {
        return ESP_ERR_INVALID_ARG;
    }
    const audio_event_t event = {
        .kind = AUDIO_EVENT_DRUM_HIT,
        .hit = hit,
        .judgement = judgement,
        .strength = strength,
    };
    return xQueueSend(s_queue, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void instrument_audio_guitar_strum(int chord, int direction)
{
    if (!s_queue || chord < 0 || chord >= GUITAR_CHORD_COUNT) {
        return;
    }
    const int step = direction < 0 ? -1 : 1;
    const int first = direction < 0 ? GUITAR_STRING_COUNT - 1 : 0;
    const int64_t now = esp_timer_get_time();
    int played = 0;
    const int gain = direction < 0 ? (NOTE_GAIN_Q8 * UP_STROKE_NUM) / UP_STROKE_DEN : NOTE_GAIN_Q8;
    for (int n = 0; n < GUITAR_STRING_COUNT; ++n) {
        const int string = first + step * n;
        const int midi = guitar_chord_midi(chord, string);
        if (midi < 0) {
            continue;
        }
        const audio_event_t event = {
            .kind = AUDIO_EVENT_GUITAR_PLUCK,
            .string_index = (uint8_t)string,
            .midi = (uint8_t)midi,
            .gain_q8 = (uint8_t)gain,
            .due_us = now + (int64_t)played * STRUM_STEP_US,
        };
        if (xQueueSend(s_queue, &event, 0) != pdTRUE) {
            return;
        }
        ++played;
    }
}
