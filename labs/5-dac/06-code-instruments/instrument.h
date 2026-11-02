#include "synth.h"

// ADSR Extension
// ADSR defines four stages of a curve that controls how volume changes over time
// 1) Attack: How long linear ramp to max volume takes
// 2) Decary: How long linear decray to "sustain" volume takes
// 3) Sustain: The volume level held while the "note" is down
// 4) Release: How quickly it fades to silence once note is released

// Abstractions:
// * `Instrument` has a base waveform function + ADSR which describe how loudness changes over time
// * `Voice` is what plays notes. It has an instrument, an `osc_t` to keep track of its waveform state, and tracks the ADSR stage "envelope"
#define NVOICES     16
#define MASTER_GAIN 0.15f   // scale for multiple voices
#define ENV_SILENT  0.0001f // voice is done below this level

typedef struct {
    const char *name;
    wave_fn wave;
    float attack;  // seconds
    float decay;   // seconds
    float sustain; // level in [0, 1]
    float release; // seconds
    float gain;
} instrument_t;

// Claude-suggested values
static const instrument_t INSTRUMENTS[] = {
    // name       wave           attack  decay  sustain release gain
    { "piano",    wave_triangle, 0.002f, 0.60f, 0.00f,  0.15f,  1.0f },
    { "organ",    wave_square,   0.005f, 0.05f, 1.00f,  0.03f,  0.5f },
    { "strings",  wave_saw,      0.250f, 0.30f, 0.80f,  0.40f,  0.6f },
    { "pluck",    wave_saw,      0.001f, 0.15f, 0.00f,  0.05f,  0.7f },
    { "flute",    wave_sine,     0.060f, 0.10f, 0.85f,  0.12f,  1.0f },
    { "bell",     wave_sine,     0.001f, 1.50f, 0.00f,  1.00f,  1.0f },
};
// End AI generation lol

#define NINSTRUMENTS (sizeof INSTRUMENTS / sizeof INSTRUMENTS[0])

typedef enum { ENV_OFF = 0, ENV_ATTACK, ENV_DECAY, ENV_RELEASE } env_stage_t;

typedef struct {
    const instrument_t *inst;
    osc_t osc;
    unsigned channel;
    unsigned note;
    float velocity; // [0, 1]
    env_stage_t stage;
    float level;
    // Per-sample envelope steps, computed at note on/off
    float attack_step;
    float decay_coef;
    float release_coef;
} voice_t;

static voice_t voices[NVOICES];
static float NOTE_FREQ[128];

#define MIX_MAX_FRAMES 1024
static float mix[MIX_MAX_FRAMES];

// Exponential decay formula: we want the coef such that after t seconds
// we cover 1 - 1/e = ~63% of the total distance
static float env_coef(float t) {
    return 1.0f - powf(2.718281828f, -1.0f / (t * SYNTH_SAMPLE_RATE));
}

static void instruments_init(void) {
    synth_init();
    for(unsigned n = 0; n < 128; n++)
        NOTE_FREQ[n] = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f); // MIDI note -> freq
}

// Start `note` and `inst`
static void note_on(unsigned channel, const instrument_t *inst, unsigned note, float velocity) {
    voice_t *v = 0;
    // Find the first unused voice
    for(unsigned i = 0; i < NVOICES && !v; i++)
        if(voices[i].stage == ENV_OFF)
            v = &voices[i];
    if(!v)
        return;

    // Initialize state: setup inst, osc, channel and note;
    // Initialize ASDR state: compute the required coefficients and start the stage
    v->inst = inst;
    v->osc = (osc_t){ .phase = 0.0f, .freq = NOTE_FREQ[note], .wave = inst->wave };
    v->channel = channel;
    v->note = note;
    v->velocity = velocity;
    v->stage = ENV_ATTACK;
    v->level = 0.0f;
    v->attack_step = 1.0f / (inst->attack * SYNTH_SAMPLE_RATE);
    v->decay_coef = env_coef(inst->decay);
    v->release_coef = env_coef(inst->release);
}

// When note turns off, start its release stage
static void note_off(unsigned channel, unsigned note) {
    for(unsigned i = 0; i < NVOICES; i++) {
        voice_t *v = &voices[i];
        if(v->stage != ENV_OFF && v->stage != ENV_RELEASE && v->channel == channel && v->note == note)
            v->stage = ENV_RELEASE;
    }
}

// Advance v envelope one sample and return its level
static float env_next(voice_t *v) {
    switch(v->stage) {
    // For each stage:
    // 1) compute its operation (increase/decre volume or hold, etc.)
    // 2) Maybe increment to the next stage
    case ENV_ATTACK:
        v->level += v->attack_step;
        if(v->level >= 1.0f) {
            v->level = 1.0f;
            v->stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        v->level += (v->inst->sustain - v->level) * v->decay_coef;
        if(v->inst->sustain < ENV_SILENT && v->level < ENV_SILENT)
            v->stage = ENV_OFF;
        break;
    case ENV_RELEASE:
        v->level -= v->level * v->release_coef;
        if(v->level < ENV_SILENT)
            v->stage = ENV_OFF;
        break;
    default:
        break;
    }
    return v->level;
}

static float soft_clip(float x) {
    return x / (1.0f + (x < 0.0f ? -x : x));
}

// Mix the active voices
static void voices_render(uint32_t *buf, unsigned nframes) {
    for(unsigned i = 0; i < nframes; i++)
        mix[i] = 0.0f;

    for(unsigned vi = 0; vi < NVOICES; vi++) {
        voice_t *v = &voices[vi];
        if(v->stage == ENV_OFF)
            continue;
        float amp = v->velocity * v->inst->gain;
        for(unsigned i = 0; i < nframes && v->stage != ENV_OFF; i++)
            mix[i] += amp * env_next(v) * osc_next(&v->osc);
    }

    for(unsigned i = 0; i < nframes; i++) {
        // When combining the voices, clamp back to 1
        uint32_t sample = (uint32_t)(int32_t)(soft_clip(MASTER_GAIN * mix[i]) * 2147483647.0f);
        buf[2 * i] = sample;     // left
        buf[2 * i + 1] = sample; // right
    }
}
