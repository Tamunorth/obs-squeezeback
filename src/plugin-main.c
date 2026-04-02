#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-squeezeback", "en-US")

extern struct obs_source_info squeezeback_transition_info;

bool obs_module_load(void)
{
	blog(LOG_INFO, "[squeezeback] Plugin loading, registering transition (id=%s, type=%d)",
	     squeezeback_transition_info.id,
	     (int)squeezeback_transition_info.type);
	obs_register_source(&squeezeback_transition_info);
	blog(LOG_INFO, "[squeezeback] Transition registered successfully");
	return true;
}

const char *obs_module_description(void)
{
	return "Squeezeback DVE transition - scales and slides the current scene "
	       "into a corner while the next scene pushes in from behind.";
}
