#include <obs-module.h>
#include "squeezeback-dock-bridge.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-squeezeback", "en-US")

extern struct obs_source_info squeezeback_transition_info;
extern struct obs_source_info squeezeback_filter_info;

/* Defined in squeezeback-filter.c */
extern void squeezeback_filter_global_toggle(void *data, obs_hotkey_id id,
					     obs_hotkey_t *hotkey,
					     bool pressed);

static obs_hotkey_id squeezeback_hotkey_id = OBS_INVALID_HOTKEY_ID;

bool obs_module_load(void)
{
	obs_register_source(&squeezeback_transition_info);
	obs_register_source(&squeezeback_filter_info);

	/* Register global hotkey */
	squeezeback_hotkey_id = obs_hotkey_register_frontend(
		"squeezeback_toggle", "Squeezeback Toggle",
		squeezeback_filter_global_toggle, NULL);

	/* Set default hotkey (F9) if no binding exists yet */
	if (squeezeback_hotkey_id != OBS_INVALID_HOTKEY_ID) {
		obs_data_array_t *saved =
			obs_hotkey_save(squeezeback_hotkey_id);
		bool has_binding =
			saved && obs_data_array_count(saved) > 0;
		if (saved)
			obs_data_array_release(saved);

		if (!has_binding) {
			obs_data_array_t *arr = obs_data_array_create();
			obs_data_t *b = obs_data_create();
			obs_data_set_string(b, "key", "OBS_KEY_F9");
			obs_data_array_push_back(arr, b);
			obs_data_release(b);
			obs_hotkey_load(squeezeback_hotkey_id, arr);
			obs_data_array_release(arr);
			blog(LOG_INFO,
			     "[squeezeback] Default hotkey set: F9");
		}
	}

	/* Register dock panel */
	squeezeback_dock_init();

	blog(LOG_INFO,
	     "[squeezeback] Plugin loaded: transition + filter + hotkey + dock");
	return true;
}

void obs_module_unload(void)
{
	squeezeback_dock_destroy();
}

const char *obs_module_description(void)
{
	return "Squeezeback DVE transition - scales and slides the current scene "
	       "into a corner while the next scene pushes in from behind.";
}
