#include "command_handler.h"
#include "../data/data_manager.h"
#include "../utils/logger.h"
#include <stdio.h>

bool handle_single_command(IMasterConnection connection, CS101_ASDU asdu)
{
    InformationObject io = CS101_ASDU_getElement(asdu, 0);
    int ioa = InformationObject_getObjectAddress(io);
    
    SingleCommand sc = (SingleCommand) io;
    
    bool select = SingleCommand_isSelect(sc);
    int qu = SingleCommand_getQU(sc);
    bool value = SingleCommand_getState(sc);

    LOG_INFO("Received C_SC_NA_1: IOA=%d, Select=%d, Value=%d, QU=%d", ioa, select, value, qu);

    // Send ACT_CON (Activation Confirmation)
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
    IMasterConnection_sendASDU(connection, asdu);

    // Log JSON for external handler
    // Format matches original server for compatibility
    if (select) {
        printf("{\"type\":\"C_SC_NA_1\",\"action\":\"select\",\"mode\":\"%s\",\"address\":%d}\n", 
               qu == 0 ? "direct" : "sbo", ioa);
    } else {
        printf("{\"type\":\"C_SC_NA_1\",\"action\":\"execute\",\"mode\":\"%s\",\"address\":%d,\"value\":\"%s\"}\n", 
               qu == 0 ? "direct" : "sbo", ioa, value ? "on" : "off");
        
        // Send ACT_TERM (Activation Termination) indicating completion
        CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
        IMasterConnection_sendASDU(connection, asdu);
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

    LOG_INFO("Received C_SE_NC_1: IOA=%d, Select=%d, Value=%.2f, QL=%d", ioa, select, value, ql);

    // Send ACT_CON
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
    IMasterConnection_sendASDU(connection, asdu);

    // Log JSON for external handler
    if (select) {
        printf("{\"type\":\"C_SE_NC_1\",\"action\":\"select\",\"mode\":\"%s\",\"address\":%d}\n", 
               ql == 0 ? "direct" : "sbo", ioa);
    } else {
        printf("{\"type\":\"C_SE_NC_1\",\"action\":\"execute\",\"mode\":\"%s\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n", 
               ql == 0 ? "direct" : "sbo", ioa, value, ql);
        
        // Send ACT_TERM
        CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
        IMasterConnection_sendASDU(connection, asdu);
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
