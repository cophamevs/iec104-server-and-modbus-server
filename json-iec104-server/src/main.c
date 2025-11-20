#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "cs104_slave.h"
#include "hal_thread.h"

#include "config/config_parser.h"
#include "data/data_manager.h"
#include "protocol/interrogation.h"
#include "protocol/command_handler.h"
#include "protocol/clock_sync.h"
#include "threads/periodic_sender.h"
#include "client/client_manager.h"
#include "input/input_handler.h"
#include "utils/logger.h"

// Global variables
static CS104_Slave slave = NULL;
static bool running = true;

// Configuration globals (externed in other modules)
uint32_t offline_udt_time = 0;
float deadband_M_ME_NC_1_percent = 0.0f;
int ASDU = 1;
char command_mode[64] = "direct";
int tcpPort = 2404;
char local_ip[64] = "0.0.0.0";
CS101_AppLayerParameters alParameters = NULL;

// Signal handler
void sigint_handler(int signalId) {
    (void)signalId;
    running = false;
}

int main(int argc, char** argv) {
    // Setup signal handler
    signal(SIGINT, sigint_handler);

    // Initialize logger
    logger_init(LOG_LEVEL_INFO);

    // Initialize modules
    init_data_contexts();
    client_manager_init();

    // Load configuration
    const char* config_file = "iec104_config.json";
    if (argc > 1) {
        config_file = argv[1];
    }

    if (!init_config_from_file(config_file)) {
        LOG_ERROR("Failed to load configuration");
        return 1;
    }

    // Create slave
    slave = CS104_Slave_create(200000, 200000);
    if (!slave) {
        LOG_ERROR("Failed to create slave instance");
        return 1;
    }

    // Get AppLayerParameters for global usage
    alParameters = CS104_Slave_getAppLayerParameters(slave);

    // Configure slave
    CS104_Slave_setLocalPort(slave, tcpPort);
    CS104_Slave_setLocalAddress(slave, local_ip);
    CS104_Slave_setServerMode(slave, CS104_MODE_SINGLE_REDUNDANCY_GROUP);

    // Set callbacks
    CS104_Slave_setInterrogationHandler(slave, interrogationHandler, NULL);
    CS104_Slave_setASDUHandler(slave, asduHandler, NULL);
    CS104_Slave_setClockSyncHandler(slave, clockSyncHandler, NULL);
    CS104_Slave_setConnectionEventHandler(slave, client_connection_event_handler, NULL);

    // Start server
    CS104_Slave_start(slave);
    if (!CS104_Slave_isRunning(slave)) {
        LOG_ERROR("Failed to start server");
        goto cleanup;
    }

    LOG_INFO("IEC 60870-5-104 Server started on %s:%d (ASDU=%d)", local_ip, tcpPort, ASDU);

    // Start periodic sender thread
    start_periodic_sender(slave);

    // Initialize input handler
    input_handler_init(slave);

    // Main loop - read stdin and process commands
    char buffer[1024];
    while (running) {
        if (fgets(buffer, sizeof(buffer), stdin)) {
            // Remove newline
            buffer[strcspn(buffer, "\n")] = 0;
            if (strlen(buffer) > 0) {
                if (!input_handler_process_line(buffer)) {
                    // Shutdown requested
                    running = false;
                }
            }
        } else {
            // EOF or error
            Thread_sleep(100);
        }
    }

cleanup:
    // Cleanup in reverse order of initialization
    LOG_INFO("Shutting down server...");

    input_handler_cleanup();
    stop_periodic_sender();

    if (slave) {
        CS104_Slave_stop(slave);
        CS104_Slave_destroy(slave);
    }

    cleanup_data_contexts();
    client_manager_cleanup();

    LOG_INFO("Server stopped");
    return 0;
}
