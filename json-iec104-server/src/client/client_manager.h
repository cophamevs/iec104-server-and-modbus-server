#ifndef CLIENT_MANAGER_H
#define CLIENT_MANAGER_H

#include <stdbool.h>
#include "../../lib60870/lib60870-C/src/inc/api/iec60870_slave.h"
#include "../../lib60870/lib60870-C/src/inc/api/cs104_slave.h"

/**
 * Client Management Module
 *
 * Tracks connected IEC 60870-5-104 clients including:
 * - IP addresses and connection times
 * - Connection/disconnection events
 * - Active client queries
 */

#define MAX_CLIENTS 10

/**
 * Initialize client manager
 */
void client_manager_init(void);

/**
 * Cleanup client manager resources
 */
void client_manager_cleanup(void);

/**
 * Connection event handler for CS104_Slave
 * Called automatically by lib60870 when clients connect/disconnect
 *
 * @param parameter User-defined parameter (unused)
 * @param connection The client connection
 * @param event Connection event type
 */
void client_connection_event_handler(void* parameter, IMasterConnection connection,
                                     CS104_PeerConnectionEvent event);

/**
 * Get JSON string of all connected clients
 * Returns allocated string that must be freed by caller
 *
 * @return JSON string like: {"connected_clients":[{"ip":"127.0.0.1:1234","connect_time":123456}],"count":1}
 */
char* client_manager_get_clients_json(void);

#endif // CLIENT_MANAGER_H
