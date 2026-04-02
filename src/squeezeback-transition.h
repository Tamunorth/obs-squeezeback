#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <math.h>

/* ── Position presets ── */
typedef enum {
	POS_TOP_RIGHT = 0,
	POS_TOP_LEFT = 1,
	POS_BOTTOM_RIGHT = 2,
	POS_BOTTOM_LEFT = 3,
	POS_TOP_CENTER = 4,
	POS_BOTTOM_CENTER = 5,
	POS_CENTER = 6,
} squeeze_position_t;

/* ── Easing types ── */
typedef enum {
	EASE_LINEAR = 0,
	EASE_IN_QUAD,
	EASE_OUT_QUAD,
	EASE_IN_OUT_QUAD,
	EASE_IN_CUBIC,
	EASE_OUT_CUBIC,
	EASE_IN_OUT_CUBIC, /* default */
	EASE_IN_EXPO,
	EASE_OUT_EXPO,
	EASE_IN_OUT_EXPO,
	EASE_IN_BACK,
	EASE_OUT_BACK,
	EASE_IN_OUT_BACK,
} easing_type_t;

/* ── Plugin data ── */
struct squeezeback_info {
	obs_source_t *source;
	gs_effect_t *effect;

	/* Shader parameter handles (cached on create) */
	gs_eparam_t *param_tex_a;
	gs_eparam_t *param_tex_b;
	gs_eparam_t *param_progress;
	gs_eparam_t *param_final_scale;
	gs_eparam_t *param_padding;
	gs_eparam_t *param_resolution;
	gs_eparam_t *param_position;
	gs_eparam_t *param_push_intensity;
	gs_eparam_t *param_border_enabled;
	gs_eparam_t *param_border_width;
	gs_eparam_t *param_border_color;
	gs_eparam_t *param_corner_radius;
	gs_eparam_t *param_shadow_enabled;
	gs_eparam_t *param_shadow_color;
	gs_eparam_t *param_shadow_offset;
	gs_eparam_t *param_shadow_blur;

	/* ── User-configurable properties ── */

	/* Position & scale */
	int target_position;   /* squeeze_position_t, default: POS_TOP_RIGHT */
	float final_scale;     /* 0.10 - 0.75, default: 0.30 (30% of screen) */
	float padding;         /* pixels from screen edge, default: 20.0 */

	/* Animation */
	int easing_type;       /* easing_type_t, default: EASE_IN_OUT_CUBIC */

	/* Push physics */
	float push_intensity;  /* 0.0 = static, 1.0 = aggressive, default: 0.3 */
	bool reverse_mode;     /* false = squeeze-in, true = squeeze-out */

	/* Border */
	bool border_enabled;   /* default: true */
	float border_width;    /* pixels, default: 3.0 */
	struct vec4 border_color; /* RGBA, default: white (1,1,1,1) */

	/* Rounded corners */
	float corner_radius;   /* pixels, default: 8.0 */

	/* Shadow */
	bool shadow_enabled;   /* default: false (Phase 2) */
	struct vec2 shadow_offset;
	float shadow_blur;
	struct vec4 shadow_color;
};

/* ── Easing function ── */
static inline float apply_easing(float t, int type)
{
	if (t <= 0.0f)
		return 0.0f;
	if (t >= 1.0f)
		return 1.0f;

	switch (type) {
	case EASE_LINEAR:
		return t;

	case EASE_IN_QUAD:
		return t * t;
	case EASE_OUT_QUAD:
		return t * (2.0f - t);
	case EASE_IN_OUT_QUAD:
		return t < 0.5f ? 2.0f * t * t
			       : -1.0f + (4.0f - 2.0f * t) * t;

	case EASE_IN_CUBIC:
		return t * t * t;
	case EASE_OUT_CUBIC: {
		float u = t - 1.0f;
		return u * u * u + 1.0f;
	}
	case EASE_IN_OUT_CUBIC:
		return t < 0.5f ? 4.0f * t * t * t
			       : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;

	case EASE_IN_EXPO:
		return powf(2.0f, 10.0f * (t - 1.0f));
	case EASE_OUT_EXPO:
		return 1.0f - powf(2.0f, -10.0f * t);
	case EASE_IN_OUT_EXPO:
		return t < 0.5f ? powf(2.0f, 20.0f * t - 10.0f) / 2.0f
			       : (2.0f - powf(2.0f, -20.0f * t + 10.0f)) /
					 2.0f;

	case EASE_IN_BACK: {
		const float c = 1.70158f;
		return (c + 1.0f) * t * t * t - c * t * t;
	}
	case EASE_OUT_BACK: {
		const float c = 1.70158f;
		float u = t - 1.0f;
		return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
	}
	case EASE_IN_OUT_BACK: {
		const float c = 1.70158f * 1.525f;
		return t < 0.5f
			       ? (powf(2.0f * t, 2.0f) * ((c + 1.0f) * 2.0f * t - c)) / 2.0f
			       : (powf(2.0f * t - 2.0f, 2.0f) *
					  ((c + 1.0f) * (t * 2.0f - 2.0f) + c) +
				  2.0f) /
					 2.0f;
	}

	default:
		return t;
	}
}
