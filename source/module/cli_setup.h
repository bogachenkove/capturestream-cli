#ifndef CLI_SETUP_H
#define CLI_SETUP_H

typedef enum
{
    CLI_ACTION_FOREGROUND,
    CLI_ACTION_START,
    CLI_ACTION_STOP,
    CLI_ACTION_STATUS,
    CLI_ACTION_RELOAD,
    CLI_ACTION_SET,
    CLI_ACTION_HELP,
    CLI_ACTION_ERROR
} cli_action;

cli_action cli_parse_arguments(int argc, char **argv, char **set_value);
void cli_print_help(void);
void cli_interactive_config_setup(const char *config_path);

#endif