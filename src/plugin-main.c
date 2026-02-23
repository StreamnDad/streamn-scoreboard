#include <obs-module.h>

#include "plugin-version.h"
#include "scoreboard-core.h"
#include "scoreboard-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("streamn-obs-scoreboard", "en-US")
OBS_MODULE_AUTHOR("Streamn")

static void obs_log_adapter(enum scoreboard_log_level level, const char *message)
{
	int obs_level = LOG_INFO;

	if (level == SCOREBOARD_LOG_WARNING) {
		obs_level = LOG_WARNING;
	} else if (level == SCOREBOARD_LOG_ERROR) {
		obs_level = LOG_ERROR;
	}

	blog(obs_level, "%s", message);
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Streamn Scoreboard";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Streamn Scoreboard v" PLUGIN_VERSION
	       " — state tracker for youth hockey broadcasts";
}

bool obs_module_load(void)
{
	const bool loaded = scoreboard_on_load(obs_log_adapter);
	if (!loaded) {
		return false;
	}

	if (!scoreboard_dock_init(obs_log_adapter)) {
		blog(LOG_WARNING,
		     "[streamn-obs-scoreboard] failed to initialize dock");
	}

	return true;
}

void obs_module_unload(void)
{
	scoreboard_dock_shutdown();
	scoreboard_on_unload(obs_log_adapter);
}
