#ifndef STREAMER_LIST_H
#define STREAMER_LIST_H

#include "streamer_manager.h"

int streamer_list_parse(const char *list, char ***names, int *count);
int streamer_list_find_index(const streamer_manager *manager, const char *streamer,
                             const char *platform);
void streamer_list_free(char **names, int count);

#endif