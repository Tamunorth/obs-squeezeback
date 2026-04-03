#pragma once

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/matrix4.h>
#include <math.h>

/* ── Easing types ── */
enum sq_easing {
	SQ_EASE_LINEAR = 0,
	SQ_EASE_OUT_CUBIC,
	SQ_EASE_IN_OUT_CUBIC,
	SQ_EASE_OUT_EXPO,
	SQ_EASE_OUT_BACK,
};

/* ── Filter data ── */
struct squeezeback_filter_data {
	obs_source_t *context;

	/* Graphics */
	gs_effect_t *effect;
	gs_eparam_t *param_mul;
	gs_eparam_t *param_add;

	/* Zoom target (normalized 0-1, relative to scene canvas) */
	float target_x; /* top-left x */
	float target_y; /* top-left y */
	float target_w; /* width */
	float target_h; /* height */
	bool target_valid;

	/* Settings */
	char *video_source_name;
	float duration;
	float delay;        /* seconds to wait before animation starts */
	int easing;
	bool auto_animate;  /* auto-trigger animation on scene activation */

	/* Animation state */
	bool animating;
	bool is_zoomed_out; /* true = full scene visible */
	float progress;     /* 0 = zoomed in, 1 = zoomed out */
	bool in_delay;      /* true during delay countdown */
	float delay_elapsed;/* accumulated delay time */

	/* Computed shader uniforms */
	struct vec2 mul_val;
	struct vec2 add_val;

	/* Hotkey */
	obs_hotkey_id hotkey_toggle;

	/* Deferred trigger: set by show/activate, acted on by video_tick */
	bool needs_auto_trigger;

	/* Debug */
	bool render_logged;
};

/* Global toggle: finds the active squeezeback filter and toggles it */
void squeezeback_filter_global_toggle(void *data, obs_hotkey_id id,
				      obs_hotkey_t *hotkey, bool pressed);

/* ── Easing ── */
static inline float sq_apply_easing(float t, int type)
{
	if (t <= 0.0f)
		return 0.0f;
	if (t >= 1.0f)
		return 1.0f;

	switch (type) {
	case SQ_EASE_LINEAR:
		return t;
	case SQ_EASE_OUT_CUBIC: {
		float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
	case SQ_EASE_IN_OUT_CUBIC:
		return t < 0.5f ? 4.0f * t * t * t
			       : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
	case SQ_EASE_OUT_EXPO:
		return 1.0f - powf(2.0f, -10.0f * t);
	case SQ_EASE_OUT_BACK: {
		float c = 1.70158f;
		float u = t - 1.0f;
		return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
	}
	default:
		return t;
	}
}

static inline float sq_lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}
