#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdbool.h>
#include "../../lib60870/lib60870-C/src/inc/api/cs104_slave.h"

/**
 * Input Handler Module
 *
 * Processes JSON commands from stdin:
 * - {"cmd":"stop"} - Shutdown server
 * - {"cmd":"get_connected_clients"} - Query connected clients
 * - {"cmd":"get_queue_count"} - Get number of queued ASDUs
 * - {"type":"M_SP_TB_1","address":100,"value":1,"qualifier":0} - Data update
 */

/**
 * Initialize input handler
 *
 * @param slave_instance The CS104_Slave instance to use for data updates
 */
void input_handler_init(CS104_Slave slave_instance);

/**
 * Process a single line of JSON input
 *
 * @param line The JSON string to process
 * @return true to continue running, false to shutdown
 */
bool input_handler_process_line(const char* line);

/**
 * Cleanup input handler resources
 */
void input_handler_cleanup(void);

#endif // INPUT_HANDLER_H
