#include "squeezeback-transition.h"

/* ──────────────────────────────────────────────
 * Settings keys
 * ────────────────────────────────────────────── */
#define S_POSITION "position"
#define S_FINAL_SCALE "final_scale"
#define S_PADDING "padding"
#define S_EASING "easing"
#define S_PUSH_INTENSITY "push_intensity"
#define S_REVERSE "reverse_mode"
#define S_BORDER_ENABLED "border_enabled"
#define S_BORDER_WIDTH "border_width"
#define S_BORDER_COLOR "border_color"
#define S_CORNER_RADIUS "corner_radius"
#define S_SHADOW_ENABLED "shadow_enabled"
#define S_SHADOW_COLOR "shadow_color"
#define S_SHADOW_OFFSET_X "shadow_offset_x"
#define S_SHADOW_OFFSET_Y "shadow_offset_y"
#define S_SHADOW_BLUR "shadow_blur"

/* ──────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────── */

static const char *squeezeback_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Squeezeback");
}

static void cache_effect_params(struct squeezeback_info *s)
{
	s->param_tex_a = gs_effect_get_param_by_name(s->effect, "tex_a");
	s->param_tex_b = gs_effect_get_param_by_name(s->effect, "tex_b");
	s->param_progress =
		gs_effect_get_param_by_name(s->effect, "progress");
	s->param_final_scale =
		gs_effect_get_param_by_name(s->effect, "final_scale");
	s->param_padding =
		gs_effect_get_param_by_name(s->effect, "padding");
	s->param_resolution =
		gs_effect_get_param_by_name(s->effect, "resolution");
	s->param_position =
		gs_effect_get_param_by_name(s->effect, "position");
	s->param_push_intensity =
		gs_effect_get_param_by_name(s->effect, "push_intensity");
	s->param_border_enabled =
		gs_effect_get_param_by_name(s->effect, "border_enabled");
	s->param_border_width =
		gs_effect_get_param_by_name(s->effect, "border_width");
	s->param_border_color =
		gs_effect_get_param_by_name(s->effect, "border_color");
	s->param_corner_radius =
		gs_effect_get_param_by_name(s->effect, "corner_radius");
	s->param_shadow_enabled =
		gs_effect_get_param_by_name(s->effect, "shadow_enabled");
	s->param_shadow_color =
		gs_effect_get_param_by_name(s->effect, "shadow_color");
	s->param_shadow_offset =
		gs_effect_get_param_by_name(s->effect, "shadow_offset");
	s->param_shadow_blur =
		gs_effect_get_param_by_name(s->effect, "shadow_blur");
}

/* ──────────────────────────────────────────────
 * Lifecycle
 * ────────────────────────────────────────────── */

static void *squeezeback_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);

	struct squeezeback_info *s =
		bzalloc(sizeof(struct squeezeback_info));
	s->source = source;

	blog(LOG_INFO, "[squeezeback] Creating transition instance");

	/* Hardcoded defaults (no get_properties so transition auto-appears) */
	s->target_position = POS_TOP_RIGHT;
	s->final_scale = 0.30f;
	s->padding = 20.0f;
	s->easing_type = EASE_IN_OUT_CUBIC;
	s->push_intensity = 0.3f;
	s->reverse_mode = false;
	s->border_enabled = true;
	s->border_width = 3.0f;
	vec4_set(&s->border_color, 1.0f, 1.0f, 1.0f, 1.0f);
	s->corner_radius = 8.0f;
	s->shadow_enabled = false;
	vec2_set(&s->shadow_offset, 4.0f, 4.0f);
	s->shadow_blur = 8.0f;
	vec4_set(&s->shadow_color, 0.0f, 0.0f, 0.0f, 0.6f);

	/* Load shader effect file */
	char *effect_path = obs_module_file("squeezeback.effect");
	if (effect_path) {
		blog(LOG_INFO, "[squeezeback] Loading effect from: %s",
		     effect_path);

		obs_enter_graphics();
		s->effect = gs_effect_create_from_file(effect_path, NULL);
		obs_leave_graphics();

		if (s->effect) {
			cache_effect_params(s);
			blog(LOG_INFO,
			     "[squeezeback] Effect loaded successfully");
		} else {
			blog(LOG_ERROR,
			     "[squeezeback] Failed to load effect file!");
		}

		bfree(effect_path);
	} else {
		blog(LOG_ERROR,
		     "[squeezeback] Could not find squeezeback.effect");
	}

	return s;
}

static void squeezeback_destroy(void *data)
{
	struct squeezeback_info *s = data;

	if (s->effect) {
		obs_enter_graphics();
		gs_effect_destroy(s->effect);
		obs_leave_graphics();
	}

	bfree(s);
}

/* ──────────────────────────────────────────────
 * Color space (required for OBS 32+)
 * ────────────────────────────────────────────── */

static enum gs_color_space
squeezeback_video_get_color_space(void *data, size_t count,
				  const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);

	struct squeezeback_info *s = data;
	return obs_transition_video_get_color_space(s->source);
}

/* ──────────────────────────────────────────────
 * Render callback (called by OBS transition system)
 * ────────────────────────────────────────────── */

static void squeezeback_render_callback(void *data, gs_texture_t *a,
					gs_texture_t *b, float t,
					uint32_t cx, uint32_t cy)
{
	struct squeezeback_info *s = data;

	if (!s->effect)
		return;

	/* Apply easing */
	float eased = apply_easing(t, s->easing_type);

	/* Reverse mode: swap textures and invert progress.
	 * Squeeze-out = PiP in corner grows to fullscreen,
	 * pushing the background away. */
	gs_texture_t *src_a = s->reverse_mode ? b : a;
	gs_texture_t *src_b = s->reverse_mode ? a : b;
	float progress = s->reverse_mode ? (1.0f - eased) : eased;

	/* Set shader uniforms */
	if (s->param_tex_a)
		gs_effect_set_texture(s->param_tex_a, src_a);
	if (s->param_tex_b)
		gs_effect_set_texture(s->param_tex_b, src_b);
	if (s->param_progress)
		gs_effect_set_float(s->param_progress, progress);
	if (s->param_final_scale)
		gs_effect_set_float(s->param_final_scale, s->final_scale);
	if (s->param_padding)
		gs_effect_set_float(s->param_padding, s->padding);
	if (s->param_push_intensity)
		gs_effect_set_float(s->param_push_intensity,
				    s->push_intensity);

	struct vec2 res;
	vec2_set(&res, (float)cx, (float)cy);
	if (s->param_resolution)
		gs_effect_set_vec2(s->param_resolution, &res);

	if (s->param_position)
		gs_effect_set_int(s->param_position, s->target_position);

	/* Border */
	if (s->param_border_enabled)
		gs_effect_set_bool(s->param_border_enabled,
				   s->border_enabled);
	if (s->param_border_width)
		gs_effect_set_float(s->param_border_width, s->border_width);
	if (s->param_border_color)
		gs_effect_set_vec4(s->param_border_color, &s->border_color);

	/* Rounded corners */
	if (s->param_corner_radius)
		gs_effect_set_float(s->param_corner_radius, s->corner_radius);

	/* Shadow */
	if (s->param_shadow_enabled)
		gs_effect_set_bool(s->param_shadow_enabled,
				   s->shadow_enabled);
	if (s->param_shadow_color)
		gs_effect_set_vec4(s->param_shadow_color, &s->shadow_color);
	if (s->param_shadow_offset)
		gs_effect_set_vec2(s->param_shadow_offset, &s->shadow_offset);
	if (s->param_shadow_blur)
		gs_effect_set_float(s->param_shadow_blur, s->shadow_blur);

	/* Draw fullscreen quad through the shader */
	while (gs_effect_loop(s->effect, "Squeezeback"))
		gs_draw_sprite(NULL, 0, cx, cy);
}

/* ──────────────────────────────────────────────
 * Video render entry point
 * ────────────────────────────────────────────── */

static void squeezeback_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct squeezeback_info *s = data;

	obs_transition_video_render(s->source, squeezeback_render_callback);
}

/* ──────────────────────────────────────────────
 * Audio render (standard crossfade)
 * ────────────────────────────────────────────── */

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - t; /* linear crossfade out */
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t; /* linear crossfade in */
}

static bool squeezeback_audio_render(void *data, uint64_t *ts_out,
				     struct obs_source_audio_mix *audio,
				     uint32_t mixers, size_t channels,
				     size_t sample_rate)
{
	struct squeezeback_info *s = data;
	return obs_transition_audio_render(s->source, ts_out, audio, mixers,
					   channels, sample_rate, mix_a,
					   mix_b);
}

/* ──────────────────────────────────────────────
 * Source info registration
 * ────────────────────────────────────────────── */

struct obs_source_info squeezeback_transition_info = {
	.id = "squeezeback_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = squeezeback_get_name,
	.create = squeezeback_create,
	.destroy = squeezeback_destroy,
	.video_render = squeezeback_video_render,
	.audio_render = squeezeback_audio_render,
	.video_get_color_space = squeezeback_video_get_color_space,
};
