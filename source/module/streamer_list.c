#include "streamer_list.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

int streamer_list_parse(const char *list, char ***names, int *count)
{
    if (list == NULL || names == NULL || count == NULL)
    {
        return -1;
    }

    char *copy = strdup(list);
    if (copy == NULL)
    {
        return -1;
    }

    *count = 0;
    char *token = strtok(copy, ",");

    while (token != NULL)
    {
        while (*token == ' ')
        {
            token++;
        }

        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
        {
            end--;
        }
        *(end + 1) = '\0';

        if (strlen(token) > 0)
        {
            (*count)++;
            char **new_array = realloc(*names, sizeof(char*) * (*count));
            if (new_array == NULL)
            {
                streamer_list_free(*names, *count - 1);
                *names = NULL;
                *count = 0;
                free(copy);
                return -1;
            }
            *names = new_array;
            (*names)[*count - 1] = strdup(token);
            if ((*names)[*count - 1] == NULL)
            {
                streamer_list_free(*names, *count);
                *names = NULL;
                *count = 0;
                free(copy);
                return -1;
            }
        }
        token = strtok(NULL, ",");
    }

    free(copy);
    return 0;
}

int streamer_list_find_index(const streamer_manager *manager, const char *streamer,
                             const char *platform)
{
    if (manager == NULL || streamer == NULL || platform == NULL)
    {
        return -1;
    }

    for (int recorder_index = 0; recorder_index < manager->recorder_count; recorder_index++)
    {
        if (strcmp(manager->recorders[recorder_index]->streamer_name, streamer) == 0 &&
            strcmp(manager->recorders[recorder_index]->platform_name, platform) == 0)
        {
            return recorder_index;
        }
    }
    return -1;
}

void streamer_list_free(char **names, int count)
{
    if (names == NULL)
    {
        return;
    }

    for (int name_index = 0; name_index < count; name_index++)
    {
        free(names[name_index]);
    }
    free(names);
}