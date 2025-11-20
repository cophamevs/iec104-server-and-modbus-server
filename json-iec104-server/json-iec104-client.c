#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "iec60870_common.h"
#include "cs104_connection.h"
#include "hal_time.h"
#include "hal_thread.h"
#include "cJSON/cJSON.h"

// Global variables
static bool running = true;
static CS104_Connection connection = NULL;
const char* QualityDescriptor_toString(QualityDescriptor qd) {
    switch (qd) {
        case IEC60870_QUALITY_GOOD:
            return "IEC60870_QUALITY_GOOD";
        case IEC60870_QUALITY_INVALID:
            return "IEC60870_QUALITY_INVALID";
        case IEC60870_QUALITY_RESERVED:
            return "IEC60870_QUALITY_RESERVED";
        case IEC60870_QUALITY_NON_TOPICAL:
            return "IEC60870_QUALITY_NON_TOPICAL";
        case IEC60870_QUALITY_SUBSTITUTED:
            return "IEC60870_QUALITY_SUBSTITUTED";
        case IEC60870_QUALITY_BLOCKED:
            return "IEC60870_QUALITY_BLOCKED";
        case IEC60870_QUALITY_ELAPSED_TIME_INVALID:
            return "IEC60870_QUALITY_ELAPSED_TIME_INVALID";
        default:
            return "UNKNOWN_QUALITY";
    }
}

// Connection event handler
static void connectionHandler(void* parameter, CS104_Connection connection, CS104_ConnectionEvent event) {
    switch (event) {
        case CS104_CONNECTION_OPENED:
            printf("{\"info\":\"connection_opened\"}\n");
            break;
        case CS104_CONNECTION_CLOSED:
            printf("{\"info\":\"connection_closed\"}\n"); 
            break;
        case CS104_CONNECTION_STARTDT_CON_RECEIVED:
            printf("{\"info\":\"startdt_con_received\"}\n");
            break;
        case CS104_CONNECTION_STOPDT_CON_RECEIVED:
            printf("{\"info\":\"stopdt_con_received\"}\n");
            break;
        default:
            break;
    }
}

static bool asduReceivedHandler(void* parameter, int address, CS101_ASDU asdu) {
    int elements = CS101_ASDU_getNumberOfElements(asdu);
    TypeID typeId = CS101_ASDU_getTypeID(asdu);

    for (int i = 0; i < elements; i++) {
        switch (typeId) {

            case M_SP_TB_1: {
                SinglePointWithCP56Time2a io = (SinglePointWithCP56Time2a) CS101_ASDU_getElement(asdu, i);
                int ioa = InformationObject_getObjectAddress((InformationObject) io);
                bool val = SinglePointInformation_getValue((SinglePointInformation) io);
                QualityDescriptor qd = SinglePointInformation_getQuality((SinglePointInformation) io);

                printf("{\"type\":\"M_SP_TB_1\",\"value\":%s,\"address\":%d,\"qualifier\":\"%s\"}\n",
                       val ? "1" : "0",
                       ioa,
                       QualityDescriptor_toString(qd));

                SinglePointWithCP56Time2a_destroy(io);
                break;
            }

            case M_ME_NC_1: {
                MeasuredValueShort io = (MeasuredValueShort) CS101_ASDU_getElement(asdu, i);
                int ioa = InformationObject_getObjectAddress((InformationObject) io);
                float val = MeasuredValueShort_getValue(io);
                QualityDescriptor qd = MeasuredValueShort_getQuality(io);

                printf("{\"type\":\"M_ME_NC_1\",\"value\":%.3f,\"address\":%d,\"qualifier\":\"%s\"}\n",
                       val, ioa, QualityDescriptor_toString(qd));

                MeasuredValueShort_destroy(io);
                break;
            }

            case M_ME_NA_1: {
                MeasuredValueScaled io = (MeasuredValueScaled) CS101_ASDU_getElement(asdu, i);
                int ioa = InformationObject_getObjectAddress((InformationObject) io);
                int val = MeasuredValueScaled_getValue(io);
                QualityDescriptor qd = MeasuredValueScaled_getQuality(io);

                printf("{\"type\":\"M_ME_NA_1\",\"value\":%d,\"address\":%d,\"qualifier\":\"%s\"}\n",
                       val, ioa, QualityDescriptor_toString(qd));

                MeasuredValueScaled_destroy(io);
                break;
            }

            case M_SP_NA_1: {
                SinglePointInformation io = (SinglePointInformation) CS101_ASDU_getElement(asdu, i);
                int ioa = InformationObject_getObjectAddress((InformationObject) io);
                bool val = SinglePointInformation_getValue(io);
                QualityDescriptor qd = SinglePointInformation_getQuality(io);

                printf("{\"type\":\"M_SP_NA_1\",\"value\":%s,\"address\":%d,\"qualifier\":\"%s\"}\n",
                       val ? "true" : "false",
                       ioa,
                       QualityDescriptor_toString(qd));

                SinglePointInformation_destroy(io);
                break;
            }

            case M_ME_TE_1: {
                MeasuredValueScaledWithCP56Time2a io = (MeasuredValueScaledWithCP56Time2a) CS101_ASDU_getElement(asdu, i);
                int ioa = InformationObject_getObjectAddress((InformationObject) io);
                int val = MeasuredValueScaled_getValue((MeasuredValueScaled) io);
                QualityDescriptor qd = MeasuredValueScaled_getQuality((MeasuredValueScaled) io);

                printf("{\"type\":\"M_ME_TE_1\",\"value\":%d,\"address\":%d,\"qualifier\":\"%s\"}\n",
                       val, ioa, QualityDescriptor_toString(qd));

                MeasuredValueScaledWithCP56Time2a_destroy(io);
                break;
            }

            default:
                // Không xử lý type không xác định
                break;
        }
    }

    return true;
}

// Note: The above handler assumes that the JSON output format is consistent with the expected structure.
// Process commands


// Signal handler
static void sigint_handler(int signalId) {
    running = false;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 1024);

    const char* serverIp = "100.81.136.34";
    int port = IEC_60870_5_104_DEFAULT_PORT;
    
    if (argc > 1) serverIp = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    signal(SIGINT, sigint_handler);

    connection = CS104_Connection_create(serverIp, port);
    if (!connection) {
        printf("{\"error\":\"failed to create connection\"}\n");
        return 1;
    }

    CS104_APCIParameters apciParams = CS104_Connection_getAPCIParameters(connection);
    apciParams->t0 = 30;
    apciParams->t1 = 15;
    apciParams->w = 10;

    CS104_Connection_setConnectionHandler(connection, connectionHandler, NULL);
    CS104_Connection_setASDUReceivedHandler(connection, (CS101_ASDUReceivedHandler)asduReceivedHandler, NULL);

    if (!CS104_Connection_connect(connection)) {
        printf("{\"error\":\"connection failed\"}\n");
        CS104_Connection_destroy(connection);
        return 1;
    }

    CS104_Connection_sendStartDT(connection);

    char input[1000];

    while (running) {
        if (fgets(input, sizeof(input), stdin) != NULL) {
            cJSON *json = cJSON_Parse(input);
            if (json) {
                // Xử lý các lệnh điều khiển hệ thống
                cJSON *cmd = cJSON_GetObjectItem(json, "command");
                if (cJSON_IsString(cmd)) {
                    if (strcmp(cmd->valuestring, "interrogation") == 0) {
                        CS104_Connection_sendInterrogationCommand(
                            connection, CS101_COT_ACTIVATION, 1, IEC60870_QOI_STATION);
                        printf("{\"info\":\"interrogation command sent\"}\n");
                    }
                    else if (strcmp(cmd->valuestring, "start") == 0) {
                        running = true;
                        printf("{\"info\":\"program started\"}\n");
                    }
                    else if (strcmp(cmd->valuestring, "stop") == 0) {
                        running = false;
                        printf("{\"info\":\"program stopping\"}\n");
                    }
                    cJSON_Delete(json);
                    continue;  // không xử lý tiếp phần "type"
                }

                // Xử lý các loại type (ví dụ: C_SC_NA_1)
                cJSON *type = cJSON_GetObjectItem(json, "type");
                if (cJSON_IsString(type)) {
                    if (strcmp(type->valuestring, "C_SC_NA_1") == 0) {
                        cJSON *action = cJSON_GetObjectItem(json, "action");
                        cJSON *addr_json = cJSON_GetObjectItem(json, "address");

                        if (!cJSON_IsString(action) || !cJSON_IsNumber(addr_json)) {
                            // printf("{\"error\":\"invalid action or address in C_SC_NA_1\"}\n");
                            cJSON_Delete(json);
                            continue;
                        }

                        int ioa = addr_json->valueint;
                        bool isSelect = strcmp(action->valuestring, "select") == 0;
                        bool value = false;
                        int qu = 0;

                        // Chỉ khi "execute" mới cần có value
                        if (!isSelect) {
                            cJSON *val_json = cJSON_GetObjectItem(json, "value");
                            if (!cJSON_IsBool(val_json)) {
                                printf("{\"error\":\"missing or invalid value for execute\"}\n");
                                cJSON_Delete(json);
                                continue;
                            }
                            value = cJSON_IsTrue(val_json);
                            cJSON *qu_json = cJSON_GetObjectItem(json, "qu");
                            if (qu_json && cJSON_IsNumber(qu_json)) {
                                qu = qu_json->valueint;
                            }
                        }

                        CS101_ASDU asdu = CS101_ASDU_create(CS104_Connection_getAppLayerParameters(connection),
                                                            false, CS101_COT_ACTIVATION, 1, 1, false, false);

                        SingleCommand sc = SingleCommand_create(NULL, ioa, value, isSelect, qu);
                        CS101_ASDU_addInformationObject(asdu, (InformationObject) sc);
                        InformationObject_destroy((InformationObject) sc);

                        CS104_Connection_sendASDU(connection, asdu);
                        CS101_ASDU_destroy(asdu);

                        // printf("{\"info\":\"C_SC_NA_1 %s sent\",\"address\":%d,\"value\":%s,\"qu\":%d}\n",
                        //     isSelect ? "select" : "execute", ioa, value ? "true" : "false", qu);
                    }
                    else if (strcmp(type->valuestring, "C_SE_NC_1") == 0) {
                        cJSON *action = cJSON_GetObjectItem(json, "action");
                        cJSON *addr_json = cJSON_GetObjectItem(json, "address");

                        if (!cJSON_IsString(action) || !cJSON_IsNumber(addr_json)) {
                            printf("{\"error\":\"invalid action or address in C_SE_NC_1\"}\n");
                            cJSON_Delete(json);
                            continue;
                        }

                        int ioa = addr_json->valueint;
                        bool isSelect = strcmp(action->valuestring, "select") == 0;
                        float value = 0.0f;
                        int qualifier = 0;

                        if (!isSelect) {
                            cJSON *val_json = cJSON_GetObjectItem(json, "value");
                            if (!cJSON_IsNumber(val_json)) {
                                printf("{\"error\":\"missing or invalid value for execute\"}\n");
                                cJSON_Delete(json);
                                continue;
                            }
                            value = (float) val_json->valuedouble;

                            cJSON *qual_json = cJSON_GetObjectItem(json, "qualifier");
                            if (cJSON_IsNumber(qual_json)) {
                                qualifier = qual_json->valueint;
                            }
                        }

                        CS101_ASDU asdu = CS101_ASDU_create(CS104_Connection_getAppLayerParameters(connection),
                                                            false, CS101_COT_ACTIVATION, 1, 1, false, false);

                        SetpointCommandShort sp = SetpointCommandShort_create(NULL, ioa, value, isSelect, qualifier);
                        CS101_ASDU_addInformationObject(asdu, (InformationObject) sp);
                        InformationObject_destroy((InformationObject) sp);

                        CS104_Connection_sendASDU(connection, asdu);
                        CS101_ASDU_destroy(asdu);

                        printf("{\"info\":\"C_SE_NC_1 %s sent\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n",
                            isSelect ? "select" : "execute", ioa, value, qualifier);
                    }
                }

                cJSON_Delete(json);
            } else {
                printf("{\"error\":\"invalid json format\"}\n");
            }
        }
    }


    CS104_Connection_destroy(connection);
    return 0;
}