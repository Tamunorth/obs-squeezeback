#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct squeezeback_dock_state {
	float progress;       /* 0.0 to 1.0 */
	float delay_progress; /* delay_elapsed / delay, 0.0 to 1.0 */
	float duration;       /* current duration in seconds */
	float delay;          /* current delay in seconds */
	int animating;        /* 1 = animation in flight */
	int in_delay;         /* 1 = in delay countdown */
	int is_zoomed_out;    /* 1 = full scene visible */
	int has_filter;       /* 1 = an active filter exists */
};

void squeezeback_get_dock_state(struct squeezeback_dock_state *out);
void squeezeback_trigger_toggle(void);
void squeezeback_set_duration(float duration);
void squeezeback_set_delay(float delay);

void squeezeback_dock_init(void);
void squeezeback_dock_destroy(void);

#ifdef __cplusplus
}
#endif
