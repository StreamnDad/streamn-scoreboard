#ifndef SCOREBOARD_DOCK_H
#define SCOREBOARD_DOCK_H

#include <stdbool.h>

#include "scoreboard-core.h"

#ifdef __cplusplus
extern "C" {
#endif

bool scoreboard_dock_init(scoreboard_log_fn log_fn);
void scoreboard_dock_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
