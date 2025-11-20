#include "command_handler.h"
#include "../data/data_manager.h"
#include "../utils/logger.h"
#include "hal_time.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

// External configuration
extern char command_mode[64];

// SBO State Management
#define MAX_SELECT_ENTRIES 100
#define SELECT_TIMEOUT_MS 5000

typedef struct {
    int ioa;
    IMasterConnection connection; 
    uint64_t timestamp;
    bool isSelected;
} SelectEntry;

static SelectEntry selectTable[MAX_SELECT_ENTRIES];
static pthread_mutex_t selectTableMutex = PTHREAD_MUTEX_INITIALIZER;

static void storeSelectState(int ioa, IMasterConnection connection) {
    pthread_mutex_lock(&selectTableMutex);
    
    int freeIndex = -1;
    for(int i = 0; i < MAX_SELECT_ENTRIES; i++) {
        if(!selectTable[i].isSelected) {
            freeIndex = i;
            break;
        }
        if(selectTable[i].ioa == ioa) {
            freeIndex = i;
            break; 
        }
    }
    
    if(freeIndex >= 0) {
        selectTable[freeIndex].ioa = ioa;
        selectTable[freeIndex].connection = connection;
        selectTable[freeIndex].timestamp = Hal_getTimeInMs();
        selectTable[freeIndex].isSelected = true;
    }
    
    pthread_mutex_unlock(&selectTableMutex);
}

static bool isPreviouslySelected(int ioa, IMasterConnection connection) {
    bool result = false;
    uint64_t currentTime = Hal_getTimeInMs();
    
    pthread_mutex_lock(&selectTableMutex);
    
    for(int i = 0; i < MAX_SELECT_ENTRIES; i++) {
        if(selectTable[i].isSelected && 
           selectTable[i].ioa == ioa &&
           selectTable[i].connection == connection) {
            if((currentTime - selectTable[i].timestamp) < SELECT_TIMEOUT_MS) {
                result = true;
            } else {
                selectTable[i].isSelected = false;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&selectTableMutex);
    return result;
}

static void clearSelectState(int ioa, IMasterConnection connection) {
    pthread_mutex_lock(&selectTableMutex);
    
    for(int i = 0; i < MAX_SELECT_ENTRIES; i++) {
        if(selectTable[i].isSelected && 
           selectTable[i].ioa == ioa &&
           selectTable[i].connection == connection) {
            selectTable[i].isSelected = false;
            break;
        }
    }
    
    pthread_mutex_unlock(&selectTableMutex);
}

bool handle_single_command(IMasterConnection connection, CS101_ASDU asdu)
{
    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    int ioa = InformationObject_getObjectAddress(io);
    
    SingleCommand sc = (SingleCommand) io;
    
    bool select = SingleCommand_isSelect(sc);
    int qu = SingleCommand_getQU(sc);
    bool value = SingleCommand_getState(sc);

    LOG_INFO("Received C_SC_NA_1: IOA=%d, Select=%d, Value=%d, QU=%d, Mode=%s", 
             ioa, select, value, qu, command_mode);

    bool isDirectMode = (strcmp(command_mode, "direct") == 0);

    if (isDirectMode) {
        // Direct mode: Execute immediately if not select
        if (!select) {
            printf("{\"type\":\"C_SC_NA_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":\"%s\"}\n", 
                   ioa, value ? "on" : "off");
            
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
            IMasterConnection_sendASDU(connection, asdu);
        } else {
            // Select in direct mode: Just confirm
            storeSelectState(ioa, connection);
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            printf("{\"type\":\"C_SC_NA_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", ioa);
        }
    } else {
        // SBO mode
        if (select) {
            storeSelectState(ioa, connection);
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            printf("{\"type\":\"C_SC_NA_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", ioa);
        } else {
            if (isPreviouslySelected(ioa, connection)) {
                clearSelectState(ioa, connection);
                printf("{\"type\":\"C_SC_NA_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":\"%s\"}\n", 
                       ioa, value ? "on" : "off");
                
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                IMasterConnection_sendASDU(connection, asdu);
                
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                IMasterConnection_sendASDU(connection, asdu);
            } else {
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                CS101_ASDU_setNegative(asdu, true);
                IMasterConnection_sendASDU(connection, asdu);
                
                printf("{\"error\":\"command not previously selected\"}\n");
            }
        }
    }

    InformationObject_destroy(io);
    return true;
}

bool handle_set_point_command(IMasterConnection connection, CS101_ASDU asdu)
{
    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    int ioa = InformationObject_getObjectAddress(io);
    
    SetpointCommandShort sp = (SetpointCommandShort) io;
    
    bool select = SetpointCommandShort_isSelect(sp);
    int ql = SetpointCommandShort_getQL(sp);
    float value = SetpointCommandShort_getValue(sp);

    LOG_INFO("Received C_SE_NC_1: IOA=%d, Select=%d, Value=%.2f, QL=%d, Mode=%s", 
             ioa, select, value, ql, command_mode);

    bool isDirectMode = (strcmp(command_mode, "direct") == 0);

    if (isDirectMode) {
        if (!select) {
            printf("{\"type\":\"C_SE_NC_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n", 
                   ioa, value, ql);
            
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
            IMasterConnection_sendASDU(connection, asdu);
        } else {
            storeSelectState(ioa, connection);
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            printf("{\"type\":\"C_SE_NC_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", ioa);
        }
    } else {
        if (select) {
            storeSelectState(ioa, connection);
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            IMasterConnection_sendASDU(connection, asdu);
            
            printf("{\"type\":\"C_SE_NC_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", ioa);
        } else {
            if (isPreviouslySelected(ioa, connection)) {
                clearSelectState(ioa, connection);
                printf("{\"type\":\"C_SE_NC_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n", 
                       ioa, value, ql);
                
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                IMasterConnection_sendASDU(connection, asdu);
                
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                IMasterConnection_sendASDU(connection, asdu);
            } else {
                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                CS101_ASDU_setNegative(asdu, true);
                IMasterConnection_sendASDU(connection, asdu);
                
                printf("{\"warn\":\"execute without select: %d\"}\n", ioa);
            }
        }
    }

    InformationObject_destroy(io);
    return true;
}

bool handle_reset_process_command(IMasterConnection connection, CS101_ASDU asdu)
{
    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    int ioa = InformationObject_getObjectAddress(io);

    ResetProcessCommand rpc = (ResetProcessCommand) io;
    uint8_t qrp = ResetProcessCommand_getQRP(rpc);

    LOG_INFO("Received C_RP_NA_1 (Reset Process): IOA=%d, QRP=%d", ioa, qrp);

    // Send ACT_CON (Activation Confirmation)
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
    IMasterConnection_sendASDU(connection, asdu);

    // Output JSON for external handler to perform actual reset
    printf("{\"type\":\"C_RP_NA_1\",\"action\":\"reset\",\"address\":%d,\"qrp\":%d}\n",
           ioa, qrp);
    fflush(stdout);

    // Execute reset if QRP is 1 (General Reset)
    if (qrp == 1) {
        reset_all_data();
    }

    // Send ACT_TERM (Activation Termination) indicating completion
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
    IMasterConnection_sendASDU(connection, asdu);

    InformationObject_destroy(io);
    return true;
}

bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
{
    TypeID typeId = CS101_ASDU_getTypeID(asdu);

    if (typeId == C_SC_NA_1) {
        return handle_single_command(connection, asdu);
    }
    else if (typeId == C_SE_NC_1) {
        return handle_set_point_command(connection, asdu);
    }
    else if (typeId == C_RP_NA_1) {
        return handle_reset_process_command(connection, asdu);
    }

    LOG_WARN("Unsupported ASDU type: %d", typeId);
    return false;
}
