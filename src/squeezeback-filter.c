#include "squeezeback-filter.h"

/* Global pointer to the most recently activated filter instance.
 * Used by the global hotkey to find which filter to toggle. */
static struct squeezeback_filter_data *g_active_filter = NULL;

/* ──────────────────────────────────────────────
 * Settings keys
 * ────────────────────────────────────────────── */
#define S_VIDEO_SOURCE "video_source"
#define S_DURATION "duration"
#define S_DELAY "delay"
#define S_EASING "easing"
#define S_AUTO_ANIMATE "auto_animate"
/* Cached video rect (persisted in settings, transferred to copies) */
#define S_CACHED_X "cached_target_x"
#define S_CACHED_Y "cached_target_y"
#define S_CACHED_W "cached_target_w"
#define S_CACHED_H "cached_target_h"

/* ──────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────── */

static const char *sqf_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SqueezebackZoom");
}

/* Detect the video source's screen rect within the parent scene */
struct detect_ctx {
	const char *target_name;
	float out_x, out_y, out_w, out_h;
	uint32_t canvas_w, canvas_h;
	bool found;
};

static bool detect_item_cb(obs_scene_t *scene, obs_sceneitem_t *item,
			   void *param)
{
	UNUSED_PARAMETER(scene);
	struct detect_ctx *ctx = param;

	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return true;

	const char *name = obs_source_get_name(src);
	if (!name || strcmp(name, ctx->target_name) != 0)
		return true;

	/* Get exact on-screen rectangle via box_transform */
	struct matrix4 box;
	obs_sceneitem_get_box_transform(item, &box);

	float screen_x = box.t.x;
	float screen_y = box.t.y;
	float screen_w = sqrtf(box.x.x * box.x.x + box.x.y * box.x.y);
	float screen_h = sqrtf(box.y.x * box.y.x + box.y.y * box.y.y);

	if (screen_w < 1.0f || screen_h < 1.0f)
		return true;

	/* Normalize to 0-1 range */
	ctx->out_x = screen_x / (float)ctx->canvas_w;
	ctx->out_y = screen_y / (float)ctx->canvas_h;
	ctx->out_w = screen_w / (float)ctx->canvas_w;
	ctx->out_h = screen_h / (float)ctx->canvas_h;
	ctx->found = true;

	return false; /* stop enumeration */
}

static void detect_video_rect(struct squeezeback_filter_data *f)
{
	if (!f->video_source_name || !f->video_source_name[0])
		return;

	obs_source_t *parent = obs_filter_get_parent(f->context);
	if (!parent)
		return;

	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene)
		return;

	struct obs_video_info ovi;
	uint32_t cw = 1920, ch = 1080;
	if (obs_get_video_info(&ovi)) {
		cw = ovi.base_width;
		ch = ovi.base_height;
	}

	struct detect_ctx ctx = {
		.target_name = f->video_source_name,
		.canvas_w = cw,
		.canvas_h = ch,
		.found = false,
	};

	obs_scene_enum_items(scene, detect_item_cb, &ctx);

	if (ctx.found && ctx.out_w > 0.01f && ctx.out_h > 0.01f) {
		f->target_x = ctx.out_x;
		f->target_y = ctx.out_y;
		f->target_w = ctx.out_w;
		f->target_h = ctx.out_h;
		f->target_valid = true;

		/* Cache in settings so copies inherit the rect */
		obs_data_t *s = obs_source_get_settings(f->context);
		if (s) {
			obs_data_set_double(s, S_CACHED_X, f->target_x);
			obs_data_set_double(s, S_CACHED_Y, f->target_y);
			obs_data_set_double(s, S_CACHED_W, f->target_w);
			obs_data_set_double(s, S_CACHED_H, f->target_h);
			obs_data_release(s);
		}

		blog(LOG_INFO,
		     "[Squeezeback Filter] Video rect detected and cached: (%.3f,%.3f %.3fx%.3f)",
		     f->target_x, f->target_y, f->target_w, f->target_h);
	}
}

/* ── Forward declaration ── */
static void sqf_do_toggle(struct squeezeback_filter_data *f);

/* ──────────────────────────────────────────────
 * Hotkey callback
 * ────────────────────────────────────────────── */

static void sqf_hotkey_toggle(void *data, obs_hotkey_id id,
			      obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	sqf_do_toggle(data);
}

/* ──────────────────────────────────────────────
 * Lifecycle
 * ────────────────────────────────────────────── */

static void *sqf_create(obs_data_t *settings, obs_source_t *source)
{
	struct squeezeback_filter_data *f =
		bzalloc(sizeof(struct squeezeback_filter_data));
	f->context = source;

	/* Load shader */
	char *path = obs_module_file("squeezeback_filter.effect");
	if (path) {
		obs_enter_graphics();
		f->effect = gs_effect_create_from_file(path, NULL);
		obs_leave_graphics();

		if (f->effect) {
			f->param_mul = gs_effect_get_param_by_name(f->effect,
								   "mul_val");
			f->param_add = gs_effect_get_param_by_name(f->effect,
								   "add_val");
			blog(LOG_INFO,
			     "[Squeezeback Filter] Effect loaded from %s",
			     path);
		} else {
			blog(LOG_ERROR,
			     "[Squeezeback Filter] Failed to load effect");
		}
		bfree(path);
	}

	/* Ensure auto_animate defaults to true in settings
	 * (sqf_defaults only applies to properties UI, not to copied settings) */
	obs_data_set_default_bool(settings, S_AUTO_ANIMATE, true);

	/* Apply settings (loads cached rect into target_x/y/w/h) */
	obs_source_update(source, settings);

	/* Start ZOOMED IN so the first frame shows the video filling the screen.
	 * If we have a valid target rect, use it; otherwise pass-through. */
	if (f->target_valid) {
		vec2_set(&f->mul_val, f->target_w, f->target_h);
		vec2_set(&f->add_val, f->target_x, f->target_y);
		f->is_zoomed_out = false;
		f->progress = 0.0f;
		blog(LOG_INFO,
		     "[Squeezeback Filter] Created: starting ZOOMED IN at (%.3f,%.3f %.3fx%.3f)",
		     f->target_x, f->target_y, f->target_w, f->target_h);
	} else {
		vec2_set(&f->mul_val, 1.0f, 1.0f);
		vec2_set(&f->add_val, 0.0f, 0.0f);
		f->is_zoomed_out = true;
		f->progress = 1.0f;
		blog(LOG_INFO,
		     "[Squeezeback Filter] Created: no target rect, starting pass-through");
	}

	return f;
}

static void sqf_destroy(void *data)
{
	struct squeezeback_filter_data *f = data;

	if (f->effect) {
		obs_enter_graphics();
		gs_effect_destroy(f->effect);
		obs_leave_graphics();
	}

	bfree(f->video_source_name);
	bfree(f);
}

/* ──────────────────────────────────────────────
 * Settings
 * ────────────────────────────────────────────── */

static void sqf_update(void *data, obs_data_t *settings)
{
	struct squeezeback_filter_data *f = data;

	const char *name = obs_data_get_string(settings, S_VIDEO_SOURCE);
	bfree(f->video_source_name);
	f->video_source_name = bstrdup(name);

	f->duration = (float)obs_data_get_double(settings, S_DURATION);
	f->delay = (float)obs_data_get_double(settings, S_DELAY);
	f->easing = (int)obs_data_get_int(settings, S_EASING);

	/* Ensure auto_animate defaults to true on EVERY settings object we touch.
	 * OBS may call sqf_update with different obs_data_t instances (e.g. after
	 * scene duplication), and only the one in sqf_create had our default set. */
	obs_data_set_default_bool(settings, S_AUTO_ANIMATE, true);
	f->auto_animate = obs_data_get_bool(settings, S_AUTO_ANIMATE);

	/* Try cached rect first (survives scene duplication) */
	bool had_target = f->target_valid;
	float cw = (float)obs_data_get_double(settings, S_CACHED_W);
	float ch = (float)obs_data_get_double(settings, S_CACHED_H);
	if (cw > 0.01f && ch > 0.01f) {
		f->target_x = (float)obs_data_get_double(settings, S_CACHED_X);
		f->target_y = (float)obs_data_get_double(settings, S_CACHED_Y);
		f->target_w = cw;
		f->target_h = ch;
		f->target_valid = true;
		blog(LOG_INFO,
		     "[Squeezeback Filter] Loaded cached rect: (%.3f,%.3f %.3fx%.3f)",
		     f->target_x, f->target_y, f->target_w, f->target_h);
	} else {
		/* No cache, detect from scene */
		f->target_valid = false;
		detect_video_rect(f);
	}

	/* If we just got a valid target for the first time, snap to zoomed-in
	 * so the scene starts with the video source filling the screen.
	 * If needs_auto_trigger is already set (activate fired before rect was
	 * available), start the animation right here instead of waiting. */
	if (f->target_valid && !had_target && !f->animating) {
		vec2_set(&f->mul_val, f->target_w, f->target_h);
		vec2_set(&f->add_val, f->target_x, f->target_y);
		f->is_zoomed_out = false;
		f->progress = 0.0f;
		blog(LOG_INFO,
		     "[Squeezeback Filter] Snapped to zoomed-in on first valid rect");

		if (f->needs_auto_trigger) {
			f->needs_auto_trigger = false;
			f->is_zoomed_out = true;
			f->progress = 0.0f;
			f->in_delay = (f->delay > 0.001f);
			f->delay_elapsed = 0.0f;
			f->animating = true;
			blog(LOG_INFO,
			     "[Squeezeback Filter] Auto-trigger fired from update (delay=%.2fs)",
			     f->delay);
		}
	}

	/* Always write rect to settings so copies inherit it */
	if (f->target_valid) {
		obs_data_set_double(settings, S_CACHED_X, (double)f->target_x);
		obs_data_set_double(settings, S_CACHED_Y, (double)f->target_y);
		obs_data_set_double(settings, S_CACHED_W, (double)f->target_w);
		obs_data_set_double(settings, S_CACHED_H, (double)f->target_h);
		obs_data_set_bool(settings, S_AUTO_ANIMATE, f->auto_animate);
	}
}

static void sqf_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, S_DURATION, 0.8);
	obs_data_set_default_double(settings, S_DELAY, 0.3);
	obs_data_set_default_int(settings, S_EASING, SQ_EASE_OUT_CUBIC);
	obs_data_set_default_bool(settings, S_AUTO_ANIMATE, true);
}

/* ──────────────────────────────────────────────
 * Properties UI
 * ────────────────────────────────────────────── */

/* Toggle: zoom out (reveal L-shape) or zoom in (show video fullscreen) */
static void sqf_do_toggle(struct squeezeback_filter_data *f)
{
	detect_video_rect(f);
	if (!f->target_valid)
		return;
	if (f->animating)
		return;

	f->is_zoomed_out = !f->is_zoomed_out;

	/* Duration=0 means instant snap */
	if (f->duration < 0.01f) {
		if (f->is_zoomed_out) {
			vec2_set(&f->mul_val, 1.0f, 1.0f);
			vec2_set(&f->add_val, 0.0f, 0.0f);
		} else {
			vec2_set(&f->mul_val, f->target_w, f->target_h);
			vec2_set(&f->add_val, f->target_x, f->target_y);
		}
		f->animating = false;
		blog(LOG_INFO, "[Squeezeback Filter] Toggle: instant snap to %s",
		     f->is_zoomed_out ? "zoomed-out" : "zoomed-in");
		return;
	}

	f->progress = 0.0f;
	f->in_delay = (f->delay > 0.001f);
	f->delay_elapsed = 0.0f;
	f->animating = true;

	blog(LOG_INFO,
	     "[Squeezeback Filter] Toggle: animating to %s (dur=%.2fs delay=%.2fs)",
	     f->is_zoomed_out ? "zoomed-out (reveal)" : "zoomed-in (fullscreen)",
	     f->duration, f->delay);
}

static bool sqf_recapture_clicked(obs_properties_t *props, obs_property_t *p,
				  void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct squeezeback_filter_data *f = data;
	detect_video_rect(f);
	if (f->target_valid && !f->is_zoomed_out && !f->animating) {
		vec2_set(&f->mul_val, f->target_w, f->target_h);
		vec2_set(&f->add_val, f->target_x, f->target_y);
	}
	return false;
}

static bool sqf_trigger_clicked(obs_properties_t *props, obs_property_t *p,
				void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	sqf_do_toggle(data);
	return false;
}

static bool enum_video_sources_cb(void *param, obs_source_t *src)
{
	obs_property_t *list = param;
	uint32_t flags = obs_source_get_output_flags(src);
	if (flags & OBS_SOURCE_VIDEO) {
		const char *n = obs_source_get_name(src);
		obs_property_list_add_string(list, n, n);
	}
	return true;
}

static obs_properties_t *sqf_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	/* Video source dropdown */
	obs_property_t *src_list = obs_properties_add_list(
		props, S_VIDEO_SOURCE,
		obs_module_text("VideoSource"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_video_sources_cb, src_list);

	/* Animation settings */
	obs_properties_add_float_slider(props, S_DURATION,
					obs_module_text("Duration"), 0.0,
					3.0, 0.1);

	obs_properties_add_float_slider(props, S_DELAY,
					obs_module_text("Delay"), 0.0,
					2.0, 0.1);

	obs_property_t *e = obs_properties_add_list(
		props, S_EASING, obs_module_text("Easing"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(e, "Linear", SQ_EASE_LINEAR);
	obs_property_list_add_int(e, "Ease Out Cubic (Smooth)",
				  SQ_EASE_OUT_CUBIC);
	obs_property_list_add_int(e, "Ease In-Out Cubic",
				  SQ_EASE_IN_OUT_CUBIC);
	obs_property_list_add_int(e, "Ease Out Expo (Fast)", SQ_EASE_OUT_EXPO);
	obs_property_list_add_int(e, "Ease Out Back (Overshoot)",
				  SQ_EASE_OUT_BACK);

	/* Auto-animate */
	obs_properties_add_bool(props, S_AUTO_ANIMATE,
				obs_module_text("AutoAnimate"));

	/* Recapture layout button */
	obs_properties_add_button2(props, "recapture",
				   obs_module_text("RecaptureLayout"),
				   sqf_recapture_clicked, data);

	/* Toggle button */
	obs_properties_add_button2(props, "trigger",
				   obs_module_text("TriggerSqueeze"),
				   sqf_trigger_clicked, data);

	return props;
}

/* ──────────────────────────────────────────────
 * Animation tick (per frame)
 * ────────────────────────────────────────────── */

static void sqf_tick(void *data, float seconds)
{
	struct squeezeback_filter_data *f = data;

	/* Deferred auto-trigger: show/activate set the flag,
	 * we act on it here once target is valid */
	if (f->needs_auto_trigger) {
		if (!f->target_valid)
			detect_video_rect(f);

		if (f->target_valid) {
			f->needs_auto_trigger = false;
			/* Ensure we're visually zoomed in before starting animation */
			vec2_set(&f->mul_val, f->target_w, f->target_h);
			vec2_set(&f->add_val, f->target_x, f->target_y);
			/* Animate TO zoomed-out (reveal full scene) */
			f->is_zoomed_out = true;
			f->progress = 0.0f;
			f->in_delay = (f->delay > 0.001f);
			f->delay_elapsed = 0.0f;
			f->animating = true;
			blog(LOG_INFO,
			     "[Squeezeback Filter] Auto-squeeze: zoomed-in -> animating out (delay=%.2fs)",
			     f->delay);
		}
	}

	if (!f->target_valid || !f->animating)
		return;

	/* Delay phase */
	if (f->in_delay) {
		f->delay_elapsed += seconds;
		if (f->delay_elapsed >= f->delay) {
			f->in_delay = false;
			blog(LOG_INFO,
			     "[Squeezeback Filter] Delay complete, animating");
		}
		return;
	}

	/* Animation phase */
	f->progress += seconds / f->duration;

	if (f->progress >= 1.0f) {
		f->progress = 1.0f;
		f->animating = false;
		blog(LOG_INFO,
		     "[Squeezeback Filter] Animation complete (zoomed_out=%d)",
		     f->is_zoomed_out);
	}

	float eased = sq_apply_easing(f->progress, f->easing);

	if (f->is_zoomed_out) {
		/* Zooming OUT: target_rect -> full (1,1,0,0) */
		float w = sq_lerp(f->target_w, 1.0f, eased);
		float h = sq_lerp(f->target_h, 1.0f, eased);
		float x = sq_lerp(f->target_x, 0.0f, eased);
		float y = sq_lerp(f->target_y, 0.0f, eased);
		vec2_set(&f->mul_val, w, h);
		vec2_set(&f->add_val, x, y);
	} else {
		/* Zooming IN: full -> target_rect */
		float w = sq_lerp(1.0f, f->target_w, eased);
		float h = sq_lerp(1.0f, f->target_h, eased);
		float x = sq_lerp(0.0f, f->target_x, eased);
		float y = sq_lerp(0.0f, f->target_y, eased);
		vec2_set(&f->mul_val, w, h);
		vec2_set(&f->add_val, x, y);
	}
}

/* ──────────────────────────────────────────────
 * Render
 * ────────────────────────────────────────────── */

static void sqf_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct squeezeback_filter_data *f = data;

	if (!f->effect) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	const enum gs_color_space preferred[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	const enum gs_color_space space = obs_source_get_color_space(
		target, sizeof(preferred) / sizeof(preferred[0]), preferred);
	const enum gs_color_format format =
		gs_get_format_from_space(space);

	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);

	if (cx == 0 || cy == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (obs_source_process_filter_begin_with_color_space(
		    f->context, format, space, OBS_NO_DIRECT_RENDERING)) {

		if (!f->render_logged) {
			blog(LOG_INFO,
			     "[Squeezeback Filter] Rendering: mul(%.3f,%.3f) add(%.3f,%.3f) cx=%u cy=%u",
			     f->mul_val.x, f->mul_val.y, f->add_val.x,
			     f->add_val.y, cx, cy);
			f->render_logged = true;
		}

		gs_effect_set_vec2(f->param_mul, &f->mul_val);
		gs_effect_set_vec2(f->param_add, &f->add_val);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

		obs_source_process_filter_tech_end(f->context, f->effect,
						   cx, cy, "Draw");

		gs_blend_state_pop();
	}
}

/* ──────────────────────────────────────────────
 * Show/Hide: auto-trigger on scene activation
 * ────────────────────────────────────────────── */

static void sqf_activate(void *data)
{
	struct squeezeback_filter_data *f = data;
	f->render_logged = false;
	g_active_filter = f; /* track the active filter for global hotkey */

	/* Re-detect source position on every scene cut so the rect
	 * stays current if the user moved/resized the source. */
	detect_video_rect(f);

	/* Read auto_animate directly from settings, not the struct field.
	 * On duplicated filters, sqf_activate can fire before sqf_update
	 * has loaded the correct settings into the struct. */
	bool should_auto = f->auto_animate;
	obs_data_t *settings = obs_source_get_settings(f->context);
	if (settings) {
		obs_data_set_default_bool(settings, S_AUTO_ANIMATE, true);
		should_auto = obs_data_get_bool(settings, S_AUTO_ANIMATE);
		obs_data_release(settings);
	}

	if (should_auto) {
		f->needs_auto_trigger = true;
		blog(LOG_INFO,
		     "[Squeezeback Filter] ACTIVATE: queued auto-trigger");
	} else {
		f->needs_auto_trigger = false;
		blog(LOG_INFO,
		     "[Squeezeback Filter] ACTIVATE: auto-animate OFF, waiting for toggle");
	}
}

static void sqf_deactivate(void *data)
{
	struct squeezeback_filter_data *f = data;
	if (g_active_filter == f)
		g_active_filter = NULL;
	f->needs_auto_trigger = false;
	f->animating = false;

	/* Reset to zoomed-IN so next activation starts with video filling screen */
	if (f->target_valid) {
		vec2_set(&f->mul_val, f->target_w, f->target_h);
		vec2_set(&f->add_val, f->target_x, f->target_y);
		f->is_zoomed_out = false;
		f->progress = 0.0f;
	} else {
		vec2_set(&f->mul_val, 1.0f, 1.0f);
		vec2_set(&f->add_val, 0.0f, 0.0f);
		f->is_zoomed_out = true;
		f->progress = 1.0f;
	}
	blog(LOG_INFO, "[Squeezeback Filter] DEACTIVATE: reset to zoomed-in");
}

static void sqf_show(void *data)
{
	sqf_activate(data);
}

static void sqf_hide(void *data)
{
	sqf_deactivate(data);
}

/* ──────────────────────────────────────────────
 * Color space
 * ────────────────────────────────────────────── */

static enum gs_color_space sqf_color_space(void *data, size_t count,
					   const enum gs_color_space *spaces)
{
	struct squeezeback_filter_data *f = data;
	obs_source_t *target = obs_filter_get_target(f->context);
	return obs_source_get_color_space(target, count, spaces);
}

/* ──────────────────────────────────────────────
 * Registration
 * ────────────────────────────────────────────── */

struct obs_source_info squeezeback_filter_info = {
	.id = "squeezeback_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = sqf_get_name,
	.create = sqf_create,
	.destroy = sqf_destroy,
	.update = sqf_update,
	.get_defaults = sqf_defaults,
	.get_properties = sqf_properties,
	.video_tick = sqf_tick,
	.video_render = sqf_render,
	.activate = sqf_activate,
	.deactivate = sqf_deactivate,
	.show = sqf_show,
	.hide = sqf_hide,
	.video_get_color_space = sqf_color_space,
};

/* ──────────────────────────────────────────────
 * Global hotkey: finds the squeezeback filter on the
 * current program scene and toggles it.
 * Registered in obs_module_load, not per-filter.
 * ────────────────────────────────────────────── */

void squeezeback_filter_global_toggle(void *data, obs_hotkey_id id,
				      obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);

	if (!pressed)
		return;

	if (g_active_filter) {
		sqf_do_toggle(g_active_filter);
		blog(LOG_INFO, "[Squeezeback Filter] Global hotkey toggle");
	} else {
		blog(LOG_WARNING,
		     "[Squeezeback Filter] Hotkey: no active filter");
	}
}
