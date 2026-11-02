#include "rpi.h"
#include "openlibm_math.h"

#define SYNTH_SAMPLE_RATE 48000.0f
#define SYNTH_PI          3.14159265358979323846f

#define SYNTH_VOLUME      0.125f

#define SINE_TAB_SIZE     4096

static float SINE_TAB[SINE_TAB_SIZE];

// p in [0, 1) -> sample in [-1, 1]
typedef float (*wave_fn)(float p);

typedef struct {
    float phase; // in [0, 1)
    float freq;  // Hz
    wave_fn wave;
} osc_t;

static void synth_init(void) {
    for(unsigned i = 0; i < SINE_TAB_SIZE; i++)
        SINE_TAB[i] = sinf(2.0f * SYNTH_PI * i / SINE_TAB_SIZE);
}

static float wave_square(float p) {
    return p < 0.5f ? 1.0f : -1.0f;
}

static float wave_saw(float p) {
    return 2.0f * p - 1.0f;
}

static float wave_triangle(float p) {
    float d = p - 0.5f;
    if(d < 0.0f)
        d = -d;
    return 4.0f * d - 1.0f;
}

static float wave_sine(float p) {
    return SINE_TAB[(unsigned)(p * SINE_TAB_SIZE) & (SINE_TAB_SIZE - 1)];
}

// Clamp to [-1, 1] and scale to signed 32-bit PCM.
static uint32_t f32_to_pcm32(float s) {
    if(s > 1.0f)
        s = 1.0f;
    if(s < -1.0f)
        s = -1.0f;
    return (uint32_t)(int32_t)(s * 2147483647.0f);
}

// Next sample from o, advancing its phase accumulator
static float osc_next(osc_t *o) {
    float s = o->wave(o->phase);
    o->phase += o->freq / SYNTH_SAMPLE_RATE;
    if(o->phase >= 1.0f)
        o->phase -= 1.0f;
    return s;
}

// Render n interleaved L, R frames into buf
static void synth_render(osc_t *o, uint32_t *buf, unsigned nframes) {
    for(unsigned i = 0; i < nframes; i++) {
        uint32_t sample = f32_to_pcm32(SYNTH_VOLUME * osc_next(o));
        buf[2 * i] = sample;     // left
        buf[2 * i + 1] = sample; // right
    }
}
