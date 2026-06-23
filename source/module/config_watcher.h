#ifndef CONFIG_WATCHER_H
#define CONFIG_WATCHER_H

#include "config.h"

int config_watcher_start(configuration_manager *manager);
void config_watcher_stop(configuration_manager *manager);

#endif