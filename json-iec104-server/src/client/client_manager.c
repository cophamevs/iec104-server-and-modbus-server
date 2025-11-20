#include "client_manager.h"
#include "../utils/logger.h"
#include "../../cJSON/cJSON.h"
#include "hal_time.h"
#include <string.h>
#include <pthread.h>

// Client information structure
typedef struct {
    char ip_address[128];
    IMasterConnection connection;
    uint64_t connect_time;
    bool is_active;
} ConnectedClient;

// Client tracking state
static ConnectedClient clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int client_count = 0;
static bool initialized = false;

void client_manager_init(void) {
    if (initialized) return;

    pthread_mutex_lock(&clients_mutex);

    // Initialize all client slots
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].is_active = false;
        clients[i].connection = NULL;
        clients[i].ip_address[0] = '\0';
        clients[i].connect_time = 0;
    }

    client_count = 0;
    initialized = true;

    pthread_mutex_unlock(&clients_mutex);

    LOG_INFO("Client manager initialized (max clients: %d)", MAX_CLIENTS);
}

void client_manager_cleanup(void) {
    if (!initialized) return;

    pthread_mutex_lock(&clients_mutex);

    // Clear all client data
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].is_active = false;
        clients[i].connection = NULL;
    }

    client_count = 0;
    initialized = false;

    pthread_mutex_unlock(&clients_mutex);

    LOG_INFO("Client manager cleaned up");
}

static void add_client(IMasterConnection connection, const char* ip_address) {
    pthread_mutex_lock(&clients_mutex);

    // Check if client already exists
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active && clients[i].connection == connection) {
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }

    // Find empty slot
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].is_active) {
            strncpy(clients[i].ip_address, ip_address, sizeof(clients[i].ip_address) - 1);
            clients[i].ip_address[sizeof(clients[i].ip_address) - 1] = '\0';
            clients[i].connection = connection;
            clients[i].connect_time = Hal_getTimeInMs();
            clients[i].is_active = true;
            client_count++;
            LOG_INFO("Client connected: %s (total: %d)", ip_address, client_count);
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

static void remove_client(IMasterConnection connection) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active && clients[i].connection == connection) {
            LOG_INFO("Client disconnected: %s (total: %d)",
                     clients[i].ip_address, client_count - 1);
            clients[i].is_active = false;
            clients[i].connection = NULL;
            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void client_connection_event_handler(void* parameter, IMasterConnection connection,
                                     CS104_PeerConnectionEvent event) {
    (void)parameter;

    switch (event) {
        case CS104_CON_EVENT_CONNECTION_OPENED: {
            char peerAddr[128];
            IMasterConnection_getPeerAddress(connection, peerAddr, sizeof(peerAddr));
            add_client(connection, peerAddr);
            break;
        }
        case CS104_CON_EVENT_CONNECTION_CLOSED:
            remove_client(connection);
            break;
        case CS104_CON_EVENT_ACTIVATED:
            LOG_DEBUG("Connection activated (STARTDT)");
            break;
        case CS104_CON_EVENT_DEACTIVATED:
            LOG_DEBUG("Connection deactivated (STOPDT)");
            break;
    }
}

char* client_manager_get_clients_json(void) {
    cJSON* response = cJSON_CreateObject();
    cJSON* clients_array = cJSON_CreateArray();

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active) {
            cJSON* client_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(client_obj, "ip", clients[i].ip_address);
            cJSON_AddNumberToObject(client_obj, "connect_time", (double)clients[i].connect_time);
            cJSON_AddItemToArray(clients_array, client_obj);
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    cJSON_AddItemToObject(response, "connected_clients", clients_array);
    cJSON_AddNumberToObject(response, "count", client_count);

    char* json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    return json_str;
}
