#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "config.h"
#include "streamer_manager.h"

typedef struct assembler_context assembler_context;

assembler_context *assembler_create (streamer_manager *manager, configuration_manager *config_mgr);
void assembler_destroy (assembler_context *ctx);
void assembler_finalize_all (assembler_context *ctx);
void assembler_update_config (assembler_context *ctx, const configuration *new_config);

#endif
