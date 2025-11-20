#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "cs104_slave.h"
#include "hal_time.h"
#include "../utils/logger.h"

// Generic ASDU handler for commands
bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu);

// Specific command handlers
bool handle_single_command(IMasterConnection connection, CS101_ASDU asdu);
bool handle_set_point_command(IMasterConnection connection, CS101_ASDU asdu);
bool handle_reset_process_command(IMasterConnection connection, CS101_ASDU asdu);

#endif // COMMAND_HANDLER_H
