// Host mock of <arm_neon.h> - just the f32x4 ops NX Diag's CPU benchmark uses.
// Scalar fallback: it compiles and runs on x86, the numbers are meaningless.
#pragma once

typedef struct { float v[4]; } float32x4_t;

static inline float32x4_t vdupq_n_f32(float x) {
    float32x4_t r = { { x, x, x, x } };
    return r;
}
static inline float32x4_t vmlaq_f32(float32x4_t a, float32x4_t b, float32x4_t c) {
    float32x4_t r;
    for (int i = 0; i < 4; i++) r.v[i] = a.v[i] + b.v[i] * c.v[i];
    return r;
}
static inline float vgetq_lane_f32(float32x4_t v, int lane) { return v.v[lane & 3]; }
