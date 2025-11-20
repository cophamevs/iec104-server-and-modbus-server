#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include "iec60870_slave.h"
#include "cs104_slave.h"
#include "cs101_slave.h"
#define FLOAT_EPSILON 1e-6f
#include "hal_thread.h"
#include "hal_time.h"

#include "cJSON/cJSON.h"

#include "server_config.h"

// Cấu hình cho gửi định kỳ
typedef struct {
    bool enabled;
    uint32_t period_ms;
    uint64_t last_sent;
} PeriodicConfig;

static PeriodicConfig periodic_M_ME_NC_1 = {false, 1000, 0};
static PeriodicConfig periodic_M_SP_TB_1 = {false, 1000, 0};
static float deadband_M_ME_NC_1_percent = 0.0f;
int ASDU = 1;
// Forward declarations
static CS104_Slave init_server(void);
static bool start_server(CS104_Slave slave);
static bool start_integrated_totals_thread(CS104_Slave slave);
static bool run_server_loop(CS104_Slave slave);
static bool process_json_input(const char* input, TypeID* type, double* value, 
                             int* address, int* qualifier);
static bool process_data_update(CS104_Slave slave, TypeID type, double value, 
                              int address, int qualifier);
static bool parse_config_from_json(const char* input);
static bool init_config_from_file(const char* filename);

// Constants
#define MS_PER_S (1000)
#define MAX_SELECT_ENTRIES 100
#define SELECT_TIMEOUT_MS 5000 // 5 giây timeout

// Thêm biến toàn cục cho offline_udt_time (đơn vị giây)
static int offline_udt_time = 300; // mặc định 5 phút
// Thêm mảng lưu thời gian cập nhật cuối cho mỗi loại dữ liệu
static uint64_t* last_offline_update_M_SP_TB_1 = NULL;
static uint64_t* last_offline_update_M_DP_TB_1 = NULL;
static uint64_t* last_offline_update_M_ME_TD_1 = NULL;
static uint64_t* last_offline_update_M_SP_NA_1 = NULL;
static uint64_t* last_offline_update_M_DP_NA_1 = NULL;
static uint64_t* last_offline_update_M_ME_NA_1 = NULL;
static uint64_t* last_offline_update_M_ME_NB_1 = NULL;
static uint64_t* last_offline_update_M_ME_NC_1 = NULL;
static uint64_t* last_offline_update_M_ME_ND_1 = NULL;

// IOA configuration for each supported type - initialized with base addresses from config
// extern DynamicIOAConfig config_M_SP_TB_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_DP_TB_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_ME_TD_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_IT_TB_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_SP_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_DP_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_ME_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_ME_NB_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_ME_NC_1 = {NULL, 0};
// extern DynamicIOAConfig config_M_ME_ND_1 = {NULL, 0};
// extern DynamicIOAConfig config_C_SC_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_C_DC_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_C_SE_NA_1 = {NULL, 0};
// extern DynamicIOAConfig config_C_SE_NC_1 = {NULL, 0};
DynamicIOAConfig config_M_SP_TB_1 = {NULL, 0};
DynamicIOAConfig config_M_DP_TB_1 = {NULL, 0};
DynamicIOAConfig config_M_ME_TD_1 = {NULL, 0};
DynamicIOAConfig config_M_IT_TB_1 = {NULL, 0};
DynamicIOAConfig config_M_SP_NA_1 = {NULL, 0};
DynamicIOAConfig config_M_DP_NA_1 = {NULL, 0};
DynamicIOAConfig config_M_ME_NA_1 = {NULL, 0};
DynamicIOAConfig config_M_ME_NB_1 = {NULL, 0};
DynamicIOAConfig config_M_ME_NC_1 = {NULL, 0};
DynamicIOAConfig config_M_ME_ND_1 = {NULL, 0};
DynamicIOAConfig config_C_SC_NA_1 = {NULL, 0};
DynamicIOAConfig config_C_DC_NA_1 = {NULL, 0};
DynamicIOAConfig config_C_SE_NA_1 = {NULL, 0};
DynamicIOAConfig config_C_SE_NC_1 = {NULL, 0};

typedef struct {
    int ioa;
    IMasterConnection connection; 
    uint64_t timestamp;
    bool isSelected;
} SelectEntry;
static SelectEntry selectTable[MAX_SELECT_ENTRIES];
static pthread_mutex_t selectTableMutex = PTHREAD_MUTEX_INITIALIZER;
// Hàm helper để quản lý trạng thái Select
static void storeSelectState(int ioa, IMasterConnection connection) {
    pthread_mutex_lock(&selectTableMutex);
    
    // Tìm entry trống hoặc entry cũ của IOA này
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
            // Kiểm tra timeout
            if((currentTime - selectTable[i].timestamp) < SELECT_TIMEOUT_MS) {
                result = true;
            } else {
                // Xóa entry đã timeout
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

// Cấu trúc dữ liệu cho các loại dữ liệu khác nhau
struct sM_SP_TB_1 {
    bool value;
    QualityDescriptor qualifier;
};
static struct sM_SP_TB_1* M_SP_TB_1_data = NULL;

struct sM_DP_TB_1 {
    DoublePointValue value;
    QualityDescriptor qualifier;
}; 
static struct sM_DP_TB_1* M_DP_TB_1_data = NULL;

struct sM_ME_TD_1 {
    float value;
    QualityDescriptor qualifier;
};
static struct sM_ME_TD_1* M_ME_TD_1_data = NULL;

struct sM_SP_NA_1 {
    bool value;
    QualityDescriptor qualifier;
};
static struct sM_SP_NA_1* M_SP_NA_1_data = NULL;

struct sM_DP_NA_1 {
    uint32_t value;
    QualityDescriptor qualifier;
};
static struct sM_DP_NA_1* M_DP_NA_1_data = NULL;

struct sM_ME_NA_1 {
    int16_t value;
    QualityDescriptor qualifier;
};
static struct sM_ME_NA_1* M_ME_NA_1_data = NULL;

struct sM_ME_NB_1 {
    int16_t value;
    QualityDescriptor qualifier;
};
static struct sM_ME_NB_1* M_ME_NB_1_data = NULL;

struct sM_ME_NC_1 {
    float value;
    QualityDescriptor qualifier;
};
static struct sM_ME_NC_1* M_ME_NC_1_data = NULL;

struct sM_ME_ND_1 {
    uint32_t value;
};
static struct sM_ME_ND_1* M_ME_ND_1_data = NULL;

static CS101_AppLayerParameters appLayerParameters;
struct sBinaryCounterReading* M_IT_TB_1_data = NULL;
bool running = true;
extern pthread_mutex_t M_SP_TB_1_mutex;
extern pthread_mutex_t M_DP_TB_1_mutex;
extern pthread_mutex_t M_ME_TD_1_mutex;
extern pthread_mutex_t M_SP_NA_1_mutex;
extern pthread_mutex_t M_DP_NA_1_mutex; 
extern pthread_mutex_t M_ME_NA_1_mutex;
extern pthread_mutex_t M_ME_NB_1_mutex;
extern pthread_mutex_t M_ME_NC_1_mutex;
extern pthread_mutex_t M_ME_ND_1_mutex;
pthread_mutex_t M_IT_TB_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_SP_TB_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_DP_TB_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_ME_TD_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_SP_NA_1_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t M_DP_NA_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_ME_NA_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_ME_NB_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_ME_NC_1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t M_ME_ND_1_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool update_m_sp_tb_1_data(CS104_Slave slave, int address, bool value, int qualifier);
static bool update_m_dp_tb_1_data(CS104_Slave slave, int address, DoublePointValue value, int qualifier);
static bool update_m_me_td_1_data(CS104_Slave slave, int address, float value, int qualifier);
static void update_m_it_tb_1_data(CS104_Slave slave, int address, uint32_t value);
static bool update_m_sp_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier);
static bool update_m_dp_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier);
static bool update_m_me_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier);
static bool update_m_me_nb_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier);
static bool update_m_me_nc_1_data(CS104_Slave slave, int address, float value, int qualifier);
static bool update_m_me_nd_1_data(CS104_Slave slave, int address, uint32_t value);


static bool IsClientConnected(CS104_Slave self)
{
    if(CS104_Slave_getOpenConnections(self) > 0) {
        return true;
    } else {
        return false;
    }
}



static CP56Time2a GetCP56Time2a(void)
{
    uint64_t timestamp_ms = Hal_getTimeInMs();
    time_t timestamp_s = timestamp_ms/MS_PER_S;
    struct tm timeinfo_tz = {0};

    localtime_r(&timestamp_s, &timeinfo_tz);

    // Create timestamp without timezone adjustment since it's handled by the IEC104 protocol
    CP56Time2a iec_time = CP56Time2a_createFromMsTimestamp(NULL, timestamp_ms);

    if(timeinfo_tz.tm_isdst == 1) {
        CP56Time2a_setSummerTime(iec_time, true);
    } else {
        CP56Time2a_setSummerTime(iec_time, false);
    }

    return iec_time;
}


static int GetDataTypeNumber(const char *str)
{
    if(strcmp(str, "M_SP_TB_1") == 0) {
        return M_SP_TB_1;
    }
    if(strcmp(str, "M_DP_TB_1") == 0) {
        return M_DP_TB_1;
    }
    if(strcmp(str, "M_ME_TD_1") == 0) {
        return M_ME_TD_1;
    }
    if(strcmp(str, "M_IT_TB_1") == 0) {
        return M_IT_TB_1;
    }
    if(strcmp(str, "M_ME_NC_1") == 0) {
        return M_ME_NC_1;
    }
    if(strcmp(str, "M_SP_NA_1") == 0) {
        return M_SP_NA_1;
    }
    if(strcmp(str, "command") == 0) {
        return 0;
    }
    return 0;
}

static int GetQualifierNumber(const char *str)
{
    if(strcmp(str, "IEC60870_QUALITY_GOOD") == 0) {
        return IEC60870_QUALITY_GOOD;
    }
    if(strcmp(str, "IEC60870_QUALITY_INVALID") == 0) {
        return IEC60870_QUALITY_INVALID;
    }
    if(strcmp(str, "IEC60870_QUALITY_OVERFLOW") == 0) {
        return IEC60870_QUALITY_OVERFLOW;
    }
    if(strcmp(str, "IEC60870_QUALITY_RESERVED") == 0) {
        return IEC60870_QUALITY_RESERVED;
    }
    if(strcmp(str, "IEC60870_QUALITY_ELAPSED_TIME_INVALID") == 0) {
        return IEC60870_QUALITY_ELAPSED_TIME_INVALID;
    }
    if(strcmp(str, "IEC60870_QUALITY_BLOCKED") == 0) {
        return IEC60870_QUALITY_BLOCKED;
    }
    if(strcmp(str, "IEC60870_QUALITY_SUBSTITUTED") == 0) {
        return IEC60870_QUALITY_SUBSTITUTED;
    }
    if(strcmp(str, "IEC60870_QUALITY_NON_TOPICAL") == 0) {
        return IEC60870_QUALITY_NON_TOPICAL;
    }
    return IEC60870_QUALITY_GOOD;
}

static void *SendPeriodicData(void *arg) 
{
    CS104_Slave slave = (CS104_Slave)arg;
    printf("{\"info\":\"periodic send thread started\"}\n");
    
    while (running) {
        uint64_t now = Hal_getTimeInMs();

        // Chỉ gửi khi có client kết nối
        if (IsClientConnected(slave)) {
            // Kiểm tra và gửi M_ME_NC_1 định kỳ
            if (periodic_M_ME_NC_1.enabled && 
                config_M_ME_NC_1.count > 0 &&
                (now - periodic_M_ME_NC_1.last_sent) >= periodic_M_ME_NC_1.period_ms) {
                
                printf("{\"debug\":\"sending periodic M_ME_NC_1 data, points:%d\"}\n", config_M_ME_NC_1.count);
                
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false, CS101_COT_PERIODIC, ASDU, ASDU, false, false);

                pthread_mutex_lock(&M_ME_NC_1_mutex);
                for (int i = 0; i < config_M_ME_NC_1.count; i++) {
                    InformationObject io = (InformationObject)MeasuredValueShort_create(
                        NULL, config_M_ME_NC_1.ioa_list[i], 
                        M_ME_NC_1_data[i].value,
                        M_ME_NC_1_data[i].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                pthread_mutex_unlock(&M_ME_NC_1_mutex);

                // Thêm ASDU vào slave event queue giống simple_server
                CS104_Slave_enqueueASDU(slave, newAsdu);
                CS101_ASDU_destroy(newAsdu);
                periodic_M_ME_NC_1.last_sent = now;
            }

            // Kiểm tra và gửi M_SP_TB_1 định kỳ
            if (periodic_M_SP_TB_1.enabled && 
                config_M_SP_TB_1.count > 0 &&
                (now - periodic_M_SP_TB_1.last_sent) >= periodic_M_SP_TB_1.period_ms) {
                
                printf("{\"debug\":\"sending periodic M_SP_TB_1 data, points:%d\"}\n", config_M_SP_TB_1.count);
                
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false, CS101_COT_PERIODIC, ASDU, ASDU, false, false);

                pthread_mutex_lock(&M_SP_TB_1_mutex);
                for (int i = 0; i < config_M_SP_TB_1.count; i++) {
                    InformationObject io = (InformationObject)SinglePointInformation_create(
                        NULL, config_M_SP_TB_1.ioa_list[i], 
                        M_SP_TB_1_data[i].value,
                        M_SP_TB_1_data[i].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                pthread_mutex_unlock(&M_SP_TB_1_mutex);

                CS104_Slave_enqueueASDU(slave, newAsdu);
                CS101_ASDU_destroy(newAsdu);
                periodic_M_SP_TB_1.last_sent = now;
            }
        }

        Thread_sleep(100); // Sleep 100ms
    }
    return NULL;
}

static void *SendIntegratedTotalsPeriodic(void *arg)
{
    if (config_M_IT_TB_1.count <= 0) {
        printf("{\"info\":\"IntegratedTotals disabled: no IOAs configured\"}\n");
        return NULL;
    }

    CS104_Slave slave = arg;
    uint32_t sequenc_number = 0;
    time_t rawtime;
    struct tm * timeinfo;

    while (1) {
        time(&rawtime);
        timeinfo = localtime(&rawtime);

        if (IsClientConnected(slave) == true && timeinfo->tm_sec == 1) {
            CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false, CS101_COT_PERIODIC, 0, 1, false, false);

            pthread_mutex_lock(&M_IT_TB_1_mutex);
            for (int i = 0; i < config_M_IT_TB_1.count; i++) {
                BinaryCounterReading_setSequenceNumber(&M_IT_TB_1_data[i], sequenc_number);
                InformationObject io = (InformationObject) IntegratedTotals_create(NULL, 
                    config_M_IT_TB_1.ioa_list[i], &M_IT_TB_1_data[i]);
                CS101_ASDU_addInformationObject(newAsdu, io);
                InformationObject_destroy(io);
            }
            pthread_mutex_unlock(&M_IT_TB_1_mutex);

            CS104_Slave_enqueueASDU(slave, newAsdu);
            CS101_ASDU_destroy(newAsdu);
            sequenc_number++;
        }
        Thread_sleep(1000);
    }
    return NULL;
}


static void sigint_handler(int signalId)
{
    running = false;
}

static bool clockSyncHandler (void* parameter, IMasterConnection connection, CS101_ASDU asdu, CP56Time2a newTime)
{
    printf("{\"debug\":\"clockSyncHandler triggered\"}\n");
    struct tm tmTime;
    char buffer[100];
    
    tmTime.tm_sec = CP56Time2a_getSecond(newTime);
    tmTime.tm_min = CP56Time2a_getMinute(newTime);
    tmTime.tm_hour = CP56Time2a_getHour(newTime);
    tmTime.tm_mday = CP56Time2a_getDayOfMonth(newTime);
    tmTime.tm_mon = CP56Time2a_getMonth(newTime) - 1;
    tmTime.tm_year = CP56Time2a_getYear(newTime) + 100;
    mktime(&tmTime);
    strftime(buffer, sizeof(buffer), "%FT%X%z", &tmTime);
    printf ("{\"type\":\"C_CS_NA_1\",\"value\":\"%s\"}\n", buffer);

#ifdef CLOCKSYNC_FROM_IEC
    char cmd_buf[200];
    snprintf(cmd_buf, sizeof(cmd_buf), "sudo /usr/bin/date --set=\"%s\" >/dev/null", buffer);
    if (system(cmd_buf)) {
         printf("{\"warn\":\"set system time failed\"}\n");
    } else {
         printf("{\"info\":\"set system time success\"}\n");
    }
#endif /* CLOCKSYNC_FROM_IEC */

    return true;
}

static bool resetProcessHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, uint8_t qrp)
{
    // Print reset process command info 
    printf("{\"info\":\"received reset process command\"}\n");

    // Send activation confirmation
    IMasterConnection_sendACT_CON(connection, asdu, false);

    // Reset relevant process data based on qrp (Qualifier of Reset Process Command)
    if (qrp == 1) { // General reset process
        // Reset all internal data structures
        pthread_mutex_lock(&M_SP_TB_1_mutex);
        if (M_SP_TB_1_data && config_M_SP_TB_1.count > 0) {
            memset(M_SP_TB_1_data, 0, sizeof(struct sM_SP_TB_1) * config_M_SP_TB_1.count);
        }
        pthread_mutex_unlock(&M_SP_TB_1_mutex);

        pthread_mutex_lock(&M_DP_TB_1_mutex);
        if (M_DP_TB_1_data && config_M_DP_TB_1.count > 0) {
            memset(M_DP_TB_1_data, 0, sizeof(struct sM_DP_TB_1) * config_M_DP_TB_1.count);
        }
        pthread_mutex_unlock(&M_DP_TB_1_mutex);

        pthread_mutex_lock(&M_ME_TD_1_mutex);
        if (M_ME_TD_1_data && config_M_ME_TD_1.count > 0) {
            memset(M_ME_TD_1_data, 0, sizeof(struct sM_ME_TD_1) * config_M_ME_TD_1.count);
        }
        pthread_mutex_unlock(&M_ME_TD_1_mutex);

        pthread_mutex_lock(&M_IT_TB_1_mutex);
        if (M_IT_TB_1_data && config_M_IT_TB_1.count > 0) {
            for (int i = 0; i < config_M_IT_TB_1.count; i++) {
                BinaryCounterReading_setValue(&M_IT_TB_1_data[i], 0);
            }
        }
        pthread_mutex_unlock(&M_IT_TB_1_mutex);

        pthread_mutex_lock(&M_SP_NA_1_mutex);
        if (M_SP_NA_1_data && config_M_SP_NA_1.count > 0) {
            memset(M_SP_NA_1_data, 0, sizeof(struct sM_SP_NA_1) * config_M_SP_NA_1.count);
        }
        pthread_mutex_unlock(&M_SP_NA_1_mutex);

        pthread_mutex_lock(&M_ME_NC_1_mutex);
        if (M_ME_NC_1_data && config_M_ME_NC_1.count > 0) {
            memset(M_ME_NC_1_data, 0, sizeof(struct sM_ME_NC_1) * config_M_ME_NC_1.count);
        }
        pthread_mutex_unlock(&M_ME_NC_1_mutex);

        printf("{\"info\":\"reset all process data completed\"}\n");
    }
    else {
        printf("{\"warn\":\"unknown reset process qualifier: %d\"}\n", qrp);
    }

    // Send activation termination
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
    IMasterConnection_sendASDU(connection, asdu);

    return true;
}
// Helper function for calculating max IOAs
static int calcMaxIOAs(int maxASDUSize, int ioaSize) {
    return (maxASDUSize - 6) / ioaSize;
}
static bool interrogationHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, uint8_t qoi)
{
    printf("{\"info\":\"received interrogation for group %i\"}\n", qoi);

    const int maxASDUSize = appLayerParameters->maxSizeOfASDU; // Default 249 bytes for IEC104
    
    if (qoi == 20) { /* only handle station interrogation */
        IMasterConnection_sendACT_CON(connection, asdu, false);
        // M_SP_TB_1 (SinglePointWithCP56Time2a - 8 bytes per IOA)
        pthread_mutex_lock(&M_SP_TB_1_mutex);
        if (config_M_SP_TB_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 8);
            for (int i = 0; i < config_M_SP_TB_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false, 
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_SP_TB_1.count) end = config_M_SP_TB_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) SinglePointWithCP56Time2a_create(
                        NULL, config_M_SP_TB_1.ioa_list[j],
                        M_SP_TB_1_data[j].value, M_SP_TB_1_data[j].qualifier,
                        GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_SP_TB_1_mutex);

        // M_DP_TB_1 (DoublePointWithCP56Time2a - 8 bytes per IOA)
        pthread_mutex_lock(&M_DP_TB_1_mutex);
        if (config_M_DP_TB_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 8);
            for (int i = 0; i < config_M_DP_TB_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_DP_TB_1.count) end = config_M_DP_TB_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) DoublePointWithCP56Time2a_create(
                        NULL, config_M_DP_TB_1.ioa_list[j],
                        (DoublePointValue)(M_DP_TB_1_data[j].value),
                        M_DP_TB_1_data[j].qualifier,
                        GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_DP_TB_1_mutex);

        // M_ME_TD_1 (MeasuredValueNormalizedWithCP56Time2a - 10 bytes per IOA) 
        pthread_mutex_lock(&M_ME_TD_1_mutex);
        if (config_M_ME_TD_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 10);
            for (int i = 0; i < config_M_ME_TD_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_ME_TD_1.count) end = config_M_ME_TD_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) MeasuredValueNormalizedWithCP56Time2a_create(
                        NULL, config_M_ME_TD_1.ioa_list[j],
                        (float)(M_ME_TD_1_data[j].value),
                        M_ME_TD_1_data[j].qualifier,
                        GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_ME_TD_1_mutex);

        // M_SP_NA_1 (SinglePointInformation - 4 bytes per IOA)
        pthread_mutex_lock(&M_SP_NA_1_mutex);
        if (config_M_SP_NA_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 4);
            for (int i = 0; i < config_M_SP_NA_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_SP_NA_1.count) end = config_M_SP_NA_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) SinglePointInformation_create(
                        NULL, config_M_SP_NA_1.ioa_list[j],
                        (bool)(M_SP_NA_1_data[j].value),
                        M_SP_NA_1_data[j].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_SP_NA_1_mutex);
        // M_DP_NA_1 (DoublePointInformation - 4 bytes per IOA)
        pthread_mutex_lock(&M_DP_NA_1_mutex);
        if (config_M_DP_NA_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 4);
            for (int i = 0; i < config_M_DP_NA_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_DP_NA_1.count) end = config_M_DP_NA_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) DoublePointInformation_create(
                        NULL, config_M_DP_NA_1.ioa_list[j],
                        (uint32_t)(M_DP_NA_1_data[j].value),
                        M_DP_NA_1_data[j].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_DP_NA_1_mutex);
        // M_ME_NA_1 (MeasuredValueNormalized - 6 bytes per IOA)
        pthread_mutex_lock(&M_ME_NA_1_mutex);
        if (config_M_ME_NA_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 6);
            for (int i = 0; i < config_M_ME_NA_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_ME_NA_1.count) end = config_M_ME_NA_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) MeasuredValueNormalized_create(
                        NULL, config_M_ME_NA_1.ioa_list[j],
                        (int16_t)(M_ME_NA_1_data[j].value),
                        M_ME_NA_1_data[j].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_ME_NA_1_mutex);
        // M_ME_NB_1 (MeasuredValueScaled - 6 bytes per IOA)
        pthread_mutex_lock(&M_ME_NB_1_mutex);
        if (config_M_ME_NB_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 6);
            for (int i = 0; i < config_M_ME_NB_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_ME_NB_1.count) end = config_M_ME_NB_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) MeasuredValueScaled_create(
                        NULL, config_M_ME_NB_1.ioa_list[j],
                        (int16_t)(M_ME_NB_1_data[j].value),
                        M_ME_NB_1_data[j].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_ME_NB_1_mutex);
        // M_ME_NC_1 (MeasuredValueShort - 4 bytes per IOA)
        pthread_mutex_lock(&M_ME_NC_1_mutex);
        if (config_M_ME_NC_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 4);
            for (int i = 0; i < config_M_ME_NC_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_ME_NC_1.count) end = config_M_ME_NC_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) MeasuredValueShort_create(
                        NULL, config_M_ME_NC_1.ioa_list[j],
                        (float)(M_ME_NC_1_data[j].value),
                        M_ME_NC_1_data[j].qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_ME_NC_1_mutex);
        // M_ME_ND_1 (MeasuredValueNormalizedWithoutQuality - 4 bytes per IOA)
        pthread_mutex_lock(&M_ME_ND_1_mutex);
        if (config_M_ME_ND_1.count > 0) {
            const int maxIOAs = calcMaxIOAs(maxASDUSize, 4);
            for (int i = 0; i < config_M_ME_ND_1.count; i += maxIOAs) {
                CS101_ASDU newAsdu = CS101_ASDU_create(appLayerParameters, false,
                    CS101_COT_INTERROGATED_BY_STATION, ASDU, ASDU, false, false);
                
                int end = i + maxIOAs;
                if (end > config_M_ME_ND_1.count) end = config_M_ME_ND_1.count;
                
                for (int j = i; j < end; j++) {
                    InformationObject io = (InformationObject) MeasuredValueNormalizedWithoutQuality_create(
                        NULL, config_M_ME_ND_1.ioa_list[j],
                        (uint32_t)(M_ME_ND_1_data[j].value));
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
                IMasterConnection_sendASDU(connection, newAsdu);
                CS101_ASDU_destroy(newAsdu);
            }
        }
        pthread_mutex_unlock(&M_ME_ND_1_mutex);


        // Similar pattern for remaining types...
        // Each type needs its own maxIOAs calculation based on its IOA size

        IMasterConnection_sendACT_TERM(connection, asdu);
    }
    else {
        IMasterConnection_sendACT_CON(connection, asdu, true);
    }

    return true;
}


// // Thêm biến toàn cục để lưu mode_command
static char command_mode[64] = "direct";

static bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
{
    bool rc = false;
    if (CS101_ASDU_getCOT(asdu) == CS101_COT_ACTIVATION) {
        InformationObject io = CS101_ASDU_getElement(asdu, 0);

        if(io != NULL) {
            int object_address = InformationObject_getObjectAddress(io);
            switch (CS101_ASDU_getTypeID(asdu)) {
                case C_SC_NA_1: {
                    bool found = false;
                    for (int i = 0; i < config_C_SC_NA_1.count; i++) {
                        if (config_C_SC_NA_1.ioa_list[i] == object_address) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (found) {
                        SingleCommand sc = (SingleCommand)io;
                        bool isSelect = SingleCommand_isSelect(sc);

                        if (strcmp(command_mode, "direct") == 0) {
                            // Direct mode: thực hiện luôn khi nhận lệnh Execute
                            if (!isSelect) {
                                bool command_val = SingleCommand_getState(sc);
                                printf("{\"type\":\"C_SC_NA_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":%s}\n",
                                       object_address, command_val ? "true" : "false");
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                IMasterConnection_sendASDU(connection, asdu);
                            } else {
                                // Nếu gửi Select thì chỉ phản hồi OK
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SC_NA_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", object_address);
                            }
                        } else {
                            // SBO mode: giữ logic cũ
                            if (isSelect) {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SC_NA_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", object_address);
                            } else {
                                if (isPreviouslySelected(object_address, connection)) {
                                    clearSelectState(object_address, connection);
                                    bool command_val = SingleCommand_getState(sc);
                                    printf("{\"type\":\"C_SC_NA_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":%s}\n",
                                           object_address, command_val ? "true" : "false");
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                    CS101_ASDU_setNegative(asdu, false);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    IMasterConnection_sendASDU(connection, asdu);
                                } else {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                    CS101_ASDU_setNegative(asdu, true);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"error\":\"command not previously selected\"}\n");
                                }
                            }
                        }
                    } else {
                        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                        CS101_ASDU_setNegative(asdu, true);
                        IMasterConnection_sendASDU(connection, asdu);
                        printf("{\"error\":\"received address not in range: %d\"}\n", object_address);
                    }
                    rc = true;
                    break;
                }
                case C_DC_NA_1: {
                    bool found = false;
                    for (int i = 0; i < config_C_DC_NA_1.count; i++) {
                        if (config_C_DC_NA_1.ioa_list[i] == object_address) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        DoubleCommand dc = (DoubleCommand) io;
                        bool isSelect = DoubleCommand_isSelect(dc);
                        int value = DoubleCommand_getState(dc);
                        int qual = DoubleCommand_getQU(dc);

                        if (strcmp(command_mode, "direct") == 0) {
                            if (!isSelect) {
                                printf("{\"type\":\"C_DC_NA_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":%d,\"qualifier\":%d}\n",
                                    object_address, value, qual);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                IMasterConnection_sendASDU(connection, asdu);
                            } else {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_DC_NA_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", object_address);
                            }
                        } else {
                            if (isSelect) {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_DC_NA_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", object_address);
                            } else {
                                if (isPreviouslySelected(object_address, connection)) {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                    CS101_ASDU_setNegative(asdu, false);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"type\":\"C_DC_NA_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":%d,\"qualifier\":%d}\n",
                                        object_address, value, qual);
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    clearSelectState(object_address, connection);
                                } else {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    CS101_ASDU_setNegative(asdu, true);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"warn\":\"execute without select: %d\"}\n", object_address);
                                }
                            }
                        }
                    } else {
                        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                        CS101_ASDU_setNegative(asdu, true);
                        IMasterConnection_sendASDU(connection, asdu);
                        printf("{\"warn\":\"received address not in range: %d\"}\n", object_address);
                    }
                    rc = true;
                    break;
                }
                case C_SE_NA_1: {
                    bool found = false;
                    for (int i = 0; i < config_C_SE_NA_1.count; i++) {
                        if (config_C_SE_NA_1.ioa_list[i] == object_address) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (found) {
                        SetpointCommandNormalized sp = (SetpointCommandNormalized) io;
                        bool isSelect = SetpointCommandNormalized_isSelect(sp); 
                        float value = SetpointCommandNormalized_getValue(sp);
                        int qual = SetpointCommandNormalized_getQL(sp);

                        if (strcmp(command_mode, "direct") == 0) {
                            if (!isSelect) {
                                printf("{\"type\":\"C_SE_NA_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n",
                                    object_address, value, qual);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                IMasterConnection_sendASDU(connection, asdu);
                            } else {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SE_NA_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", object_address);
                            }
                        } else {
                            if (isSelect) {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SE_NA_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", object_address);
                            } else {
                                if (isPreviouslySelected(object_address, connection)) {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                    CS101_ASDU_setNegative(asdu, false);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"type\":\"C_SE_NA_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n",
                                        object_address, value, qual);
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    clearSelectState(object_address, connection);
                                } else {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    CS101_ASDU_setNegative(asdu, true);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"warn\":\"execute without select: %d\"}\n", object_address);
                                }
                            }
                        }
                    } else {
                        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                        CS101_ASDU_setNegative(asdu, true);
                        IMasterConnection_sendASDU(connection, asdu);
                        printf("{\"warn\":\"received address not in range: %d\"}\n", object_address);
                    }
                    rc = true;
                    break;
                }
                case C_SE_NC_1: {
                    bool found = false;
                    for (int i = 0; i < config_C_SE_NC_1.count; i++) {
                        if (config_C_SE_NC_1.ioa_list[i] == object_address) {
                            found = true;
                            break;
                        }
                    }

                    if (found) {
                        SetpointCommandShort sp = (SetpointCommandShort) io;
                        bool isSelect = SetpointCommandShort_isSelect(sp);
                        float val = SetpointCommandShort_getValue(sp);
                        int qual = SetpointCommandShort_getQL(sp);

                        if (strcmp(command_mode, "direct") == 0) {
                            if (!isSelect) {
                                printf("{\"type\":\"C_SE_NC_1\",\"action\":\"execute\",\"mode\":\"direct\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n",
                                    object_address, val, qual);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                IMasterConnection_sendASDU(connection, asdu);
                            } else {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SE_NC_1\",\"action\":\"select\",\"mode\":\"direct\",\"address\":%d}\n", object_address);
                            }
                        } else {
                            if (isSelect) {
                                storeSelectState(object_address, connection);
                                CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                CS101_ASDU_setNegative(asdu, false);
                                IMasterConnection_sendASDU(connection, asdu);
                                printf("{\"type\":\"C_SE_NC_1\",\"action\":\"select\",\"mode\":\"sbo\",\"address\":%d}\n", object_address);
                            } else {
                                if (isPreviouslySelected(object_address, connection)) {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
                                    CS101_ASDU_setNegative(asdu, false);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"type\":\"C_SE_NC_1\",\"action\":\"execute\",\"mode\":\"sbo\",\"address\":%d,\"value\":%.2f,\"qualifier\":%d}\n",
                                        object_address, val, qual);
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    clearSelectState(object_address, connection);
                                } else {
                                    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
                                    CS101_ASDU_setNegative(asdu, true);
                                    IMasterConnection_sendASDU(connection, asdu);
                                    printf("{\"warn\":\"execute without select: %d\"}\n", object_address);
                                }
                            }
                        }
                    } else {
                        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_IOA);
                        CS101_ASDU_setNegative(asdu, true);
                        IMasterConnection_sendASDU(connection, asdu);
                        printf("{\"warn\":\"received address not in range: %d\"}\n", object_address);
                    }
                    rc = true;
                    break;
                }
                default: {
                    CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_TYPE_ID);
                    IMasterConnection_sendASDU(connection, asdu);
                    printf("{\"warn\":\"received unknown type id: %d\"}\n", CS101_ASDU_getTypeID(asdu));
                    break;
                }
            }
            InformationObject_destroy(io);
        } else {
            CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_TYPE_ID);
            IMasterConnection_sendASDU(connection, asdu);
            printf("{\"warn\":\"received unknown type id: %d\"}\n", CS101_ASDU_getTypeID(asdu));
        }

    } else {
        CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_COT);
        IMasterConnection_sendASDU(connection, asdu);
    }

    rc = true;

    return rc;
}

static bool connectionRequestHandler(void* parameter, const char* ipAddress)
{
    printf("{\"info\":\"accept connection from %s\"}\n", ipAddress);
    return true;
}


static void ConnectionEventHandler(void* parameter, IMasterConnection connection, CS104_PeerConnectionEvent event)
{
    switch (event) {
    case CS104_CON_EVENT_ACTIVATED:
        printf("{\"info\":\"received STARTDT_CON\"}\n");
        break;
    case CS104_CON_EVENT_DEACTIVATED:
        printf("{\"info\":\"received STOPDT_CON\"}\n");
        break;
    case CS104_CON_EVENT_CONNECTION_OPENED:
    case CS104_CON_EVENT_CONNECTION_CLOSED:
        break;
    }
}


static void cleanup_data_structures() {
    // Free IOA configurations
    if (config_M_SP_TB_1.ioa_list) free(config_M_SP_TB_1.ioa_list);
    if (config_M_DP_TB_1.ioa_list) free(config_M_DP_TB_1.ioa_list);
    if (config_M_ME_TD_1.ioa_list) free(config_M_ME_TD_1.ioa_list);
    if (config_M_IT_TB_1.ioa_list) free(config_M_IT_TB_1.ioa_list);
    if (config_M_SP_NA_1.ioa_list) free(config_M_SP_NA_1.ioa_list);
    if (config_M_DP_NA_1.ioa_list) free(config_M_DP_NA_1.ioa_list);
    if (config_M_ME_NA_1.ioa_list) free(config_M_ME_NA_1.ioa_list);
    if (config_M_ME_NB_1.ioa_list) free(config_M_ME_NB_1.ioa_list);
    if (config_M_ME_NC_1.ioa_list) free(config_M_ME_NC_1.ioa_list);
    if (config_M_ME_ND_1.ioa_list) free(config_M_ME_ND_1.ioa_list);
    if (config_C_SC_NA_1.ioa_list) free(config_C_SC_NA_1.ioa_list);
    if (config_C_DC_NA_1.ioa_list) free(config_C_DC_NA_1.ioa_list);
    if (config_C_SE_NA_1.ioa_list) free(config_C_SE_NA_1.ioa_list);
    if (config_C_SE_NC_1.ioa_list) free(config_C_SE_NC_1.ioa_list);

    // Free data structures
    if (M_SP_TB_1_data) free(M_SP_TB_1_data);
    if (M_DP_TB_1_data) free(M_DP_TB_1_data);
    if (M_ME_TD_1_data) free(M_ME_TD_1_data);
    if (M_SP_NA_1_data) free(M_SP_NA_1_data);
    if (M_DP_NA_1_data) free(M_DP_NA_1_data);
    if (M_ME_NA_1_data) free(M_ME_NA_1_data);
    if (M_ME_NB_1_data) free(M_ME_NB_1_data);
    if (M_ME_NC_1_data) free(M_ME_NC_1_data);
    if (M_ME_ND_1_data) free(M_ME_ND_1_data);
}

static bool init_config_from_file(const char* filename);

int main(int argc, char** argv) {
    // Setup signal handler
    signal(SIGINT, sigint_handler);

    // Initialize server
    CS104_Slave slave = init_server();
    if (!slave) {
        goto exit_program;
    }

    // Load configuration
    if (!init_config_from_file("/home/pi/iec104_config.json")) {
        printf("{\"error\":\"configuration initialization failed\"}\n");
        goto exit_program;
    }

    // Start server
    if (!start_server(slave)) {
        goto exit_program;
    }

    // Start background tasks
    if (!start_integrated_totals_thread(slave)) {
        goto exit_program;
    }

    // Run main server loop
    run_server_loop(slave);

exit_program:
    cleanup_data_structures();
    CS104_Slave_stop(slave);
    CS104_Slave_destroy(slave);
    return 0;
}

static bool allow_offline_update(uint64_t* last_update_arr, int idx) {
    uint64_t now = Hal_getTimeInMs();
    if (last_update_arr == NULL) return true;
    if (now - last_update_arr[idx] >= (uint64_t)offline_udt_time * 1000) {
        last_update_arr[idx] = now;
        return true;
    }
    return false;
}
static bool update_m_sp_tb_1_data(CS104_Slave slave, int address, bool value, int qualifier)
{
    bool rc = false;
    if (address < 0 || address >= config_M_SP_TB_1.count) {
        printf("{\"error\":\"M_SP_TB_1 address %d out of range\"}\n", address);
        return false;
    }
    
    pthread_mutex_lock(&M_SP_TB_1_mutex);
    bool changed = false;
    if(M_SP_TB_1_data[address].value != value) {
        M_SP_TB_1_data[address].value = value;
        changed = true;
    }
    if(M_SP_TB_1_data[address].qualifier != qualifier) {
        M_SP_TB_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_SP_TB_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_SP_TB_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_dp_tb_1_data(CS104_Slave slave, int address, DoublePointValue value, int qualifier)
{
    bool rc = false;
    if (address < 0 || address >= config_M_DP_TB_1.count) {
        printf("{\"error\":\"M_DP_TB_1 address %d out of range\"}\n", address);
        return false;
    }
    
    pthread_mutex_lock(&M_DP_TB_1_mutex);
    bool changed = false;

    if(M_DP_TB_1_data[address].value != value) {
        M_DP_TB_1_data[address].value = value;
        changed = true;
    }
    if(M_DP_TB_1_data[address].qualifier != qualifier) {
        M_DP_TB_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_DP_TB_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_DP_TB_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_me_td_1_data(CS104_Slave slave, int address, float value, int qualifier) 
{
    bool rc = false;
    if (address < 0 || address >= config_M_ME_TD_1.count) {
        printf("{\"error\":\"M_ME_TD_1 address %d out of range\"}\n", address);
        return false;
    }

    pthread_mutex_lock(&M_ME_TD_1_mutex);
    bool changed = false;
    if(M_ME_TD_1_data[address].value != value) {
        M_ME_TD_1_data[address].value = value;
        changed = true;
    }
    if(M_ME_TD_1_data[address].qualifier != qualifier) {
        M_ME_TD_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_ME_TD_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_ME_TD_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static void update_m_it_tb_1_data(CS104_Slave slave, int address, uint32_t value)
{
    if (address < 0 || address >= config_M_IT_TB_1.count) {
        printf("{\"error\":\"M_IT_TB_1 address %d out of range\"}\n", address);
        return;
    }
    
    pthread_mutex_lock(&M_IT_TB_1_mutex);
    BinaryCounterReading_setValue(&M_IT_TB_1_data[address], value);
    pthread_mutex_unlock(&M_IT_TB_1_mutex);
}

static bool update_m_sp_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier)
{
    bool rc = false;
    pthread_mutex_lock(&M_SP_NA_1_mutex);
    bool changed = false;
    if (M_SP_NA_1_data[address].value != value) {
        M_SP_NA_1_data[address].value = value;
        changed = true;
    }
    if (M_SP_NA_1_data[address].qualifier != qualifier) {
        M_SP_NA_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_SP_NA_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_SP_NA_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_dp_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier)
{
    bool rc = false;
    pthread_mutex_lock(&M_DP_NA_1_mutex);
    bool changed = false;
    if (M_DP_NA_1_data[address].value != value) {
        M_DP_NA_1_data[address].value = value;
        changed = true;
    }
    if (M_DP_NA_1_data[address].qualifier != qualifier) {
        M_DP_NA_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_DP_NA_1_mutex);
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_DP_NA_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_me_na_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier)
{
    bool rc = false;
   
    if (address < 0 || address >= config_M_ME_NA_1.count) {
        printf("{\"error\":\"M_ME_NA_1 address %d out of range\"}\n", address);
        pthread_mutex_unlock(&M_ME_NA_1_mutex);
        return false;
    }
    pthread_mutex_lock(&M_ME_NA_1_mutex);
    bool changed = false;
    if (M_ME_NA_1_data[address].value != value) {
        M_ME_NA_1_data[address].value = value;
        changed = true;
    }
    if (M_ME_NA_1_data[address].qualifier != qualifier) {
        M_ME_NA_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_ME_NA_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_ME_NA_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_me_nb_1_data(CS104_Slave slave, int address, uint32_t value, int qualifier)
{
    bool rc = false;
    if (address < 0 || address >= config_M_ME_NB_1.count) {
        printf("{\"error\":\"M_ME_NB_1 address %d out of range\"}\n", address);
        return false;
    }
    pthread_mutex_lock(&M_ME_NB_1_mutex);
    bool changed = false;
    if (M_ME_NB_1_data[address].value != value) {
        M_ME_NB_1_data[address].value = value;
        changed = true;
    }
    if (M_ME_NB_1_data[address].qualifier != qualifier) {
        M_ME_NB_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_ME_NB_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_ME_NB_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_me_nc_1_data(CS104_Slave slave, int address, float value, int qualifier)
{
    bool rc = false;
    if (address < 0 || address >= config_M_ME_NC_1.count) {
        printf("{\"error\":\"M_ME_NC_1 address %d out of range\"}\n", address);
        return false;
    }
    pthread_mutex_lock(&M_ME_NC_1_mutex);
    bool changed = false;
    float old = M_ME_NC_1_data[address].value;
    float diff = fabsf(old - value);
    float percent;
    if (fabsf(old) > FLOAT_EPSILON) {
        percent = (diff / fabsf(old)) * 100.0f;
    } else {
        percent = (diff > FLOAT_EPSILON) ? 100.0f : 0.0f;
    }
    if ((percent >= deadband_M_ME_NC_1_percent) && (diff > FLOAT_EPSILON)) {
        M_ME_NC_1_data[address].value = value;
        changed = true;
    }
    if (M_ME_NC_1_data[address].qualifier != qualifier) {
        M_ME_NC_1_data[address].qualifier = qualifier;
        changed = true;
    }
    pthread_mutex_unlock(&M_ME_NC_1_mutex);
    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_ME_NC_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool update_m_me_nd_1_data(CS104_Slave slave, int address, uint32_t value)
{
    bool rc = false;
    if (address < 0 || address >= config_M_ME_ND_1.count) {
        printf("{\"error\":\"M_ME_ND_1 address %d out of range\"}\n", address);
        return false;
    }


    pthread_mutex_lock(&M_ME_ND_1_mutex);
    bool changed = false;
    if (M_ME_ND_1_data[address].value != value) {
        M_ME_ND_1_data[address].value = value;
        changed = true;
    }
    
    pthread_mutex_unlock(&M_ME_ND_1_mutex);

    // Allow update to return true when allow_offline_update is true which is mean client is offline
    // or when the value or qualifier has changed + client is online
    if (changed) {
        if (IsClientConnected(slave) || allow_offline_update(last_offline_update_M_ME_ND_1, address)) {
            rc = true;
        }
    }
    return rc;
}

static bool parse_config_from_json(const char* input) {
    bool status = true;

    cJSON *json = cJSON_Parse(input);
    if (json == NULL) {
        printf("{\"error\":\"failed to parse config json\"}\n");
        return false;
    }
    // Parse offline_udt_time from config
    cJSON *offline_udt_time_json = cJSON_GetObjectItemCaseSensitive(json, "offline_udt_time");
    if (cJSON_IsNumber(offline_udt_time_json)) {
        offline_udt_time = offline_udt_time_json->valueint;
        printf("{\"info\":\"offline_udt_time set to %d seconds\"}\n", offline_udt_time);
    }
    // Parse ASDU address if present
    cJSON *asdu_json = cJSON_GetObjectItemCaseSensitive(json, "ASDU");
    if (cJSON_IsNumber(asdu_json)) {
        ASDU = asdu_json->valueint;
    }
        // Parse deadband for M_ME_NC_1
    cJSON *me_nc_1_deadband = cJSON_GetObjectItemCaseSensitive(json, "M_ME_NC_1_deadband_percent");
    if (cJSON_IsNumber(me_nc_1_deadband)) {
        deadband_M_ME_NC_1_percent = (float)me_nc_1_deadband->valuedouble;
    }
    // Parse M_SP_TB_1 config
    cJSON *m_sp_tb_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_SP_TB_1_config");
    if (cJSON_IsArray(m_sp_tb_1_config)) {
        config_M_SP_TB_1.count = cJSON_GetArraySize(m_sp_tb_1_config);
        config_M_SP_TB_1.ioa_list = (int*)malloc(config_M_SP_TB_1.count * sizeof(int));
        
        if (config_M_SP_TB_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_SP_TB_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_sp_tb_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_SP_TB_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_SP_TB_1_data 
            M_SP_TB_1_data = (struct sM_SP_TB_1*)malloc(config_M_SP_TB_1.count * sizeof(struct sM_SP_TB_1));
            if (M_SP_TB_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_SP_TB_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_SP_TB_1.count; i++) {
                    M_SP_TB_1_data[i].value = false;
                    M_SP_TB_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
                // allocate memory for last_offline_update_M_SP_TB_1
                last_offline_update_M_SP_TB_1 = (uint64_t*)malloc(config_M_SP_TB_1.count * sizeof(uint64_t));
                if (last_offline_update_M_SP_TB_1 != NULL) {
                    for (int i = 0; i < config_M_SP_TB_1.count; i++) last_offline_update_M_SP_TB_1[i] = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_SP_TB_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_DP_TB_1 config
    cJSON *m_dp_tb_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_DP_TB_1_config");
    if (cJSON_IsArray(m_dp_tb_1_config)) {
        config_M_DP_TB_1.count = cJSON_GetArraySize(m_dp_tb_1_config);
        config_M_DP_TB_1.ioa_list = (int*)malloc(config_M_DP_TB_1.count * sizeof(int));
        
        if (config_M_DP_TB_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_DP_TB_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_dp_tb_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_DP_TB_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_DP_TB_1_data
            M_DP_TB_1_data = (struct sM_DP_TB_1*)malloc(config_M_DP_TB_1.count * sizeof(struct sM_DP_TB_1));
            if (M_DP_TB_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_DP_TB_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_DP_TB_1.count; i++) {
                    M_DP_TB_1_data[i].value = IEC60870_DOUBLE_POINT_INTERMEDIATE;
                    M_DP_TB_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
                // allocate memory for last_offline_update_M_DP_TB_1
                last_offline_update_M_DP_TB_1 = (uint64_t*)malloc(config_M_DP_TB_1.count * sizeof(uint64_t));
                if (last_offline_update_M_DP_TB_1 != NULL) {
                    for (int i = 0; i < config_M_DP_TB_1.count; i++) last_offline_update_M_DP_TB_1[i] = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_DP_TB_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_ME_TD_1 config  
    cJSON *m_me_td_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_ME_TD_1_config");
    if (cJSON_IsArray(m_me_td_1_config)) {
        config_M_ME_TD_1.count = cJSON_GetArraySize(m_me_td_1_config);
        config_M_ME_TD_1.ioa_list = (int*)malloc(config_M_ME_TD_1.count * sizeof(int));
        
        if (config_M_ME_TD_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_ME_TD_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_me_td_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_ME_TD_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_ME_TD_1_data
            M_ME_TD_1_data = (struct sM_ME_TD_1*)malloc(config_M_ME_TD_1.count * sizeof(struct sM_ME_TD_1));
            if (M_ME_TD_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_ME_TD_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_ME_TD_1.count; i++) {
                    M_ME_TD_1_data[i].value = 0.0f;
                    M_ME_TD_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_ME_TD_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_IT_TB_1 config
    cJSON *m_it_tb_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_IT_TB_1_config");
    if (cJSON_IsArray(m_it_tb_1_config)) {
        config_M_IT_TB_1.count = cJSON_GetArraySize(m_it_tb_1_config);
        config_M_IT_TB_1.ioa_list = (int*)malloc(config_M_IT_TB_1.count * sizeof(int));
        
        if (config_M_IT_TB_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_IT_TB_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_it_tb_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_IT_TB_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_IT_TB_1_data
            M_IT_TB_1_data = malloc(config_M_IT_TB_1.count * sizeof(struct sBinaryCounterReading));;
            if (M_IT_TB_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_IT_TB_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_IT_TB_1.count; i++) {
                    BinaryCounterReading_setValue(&M_IT_TB_1_data[i], 0);
                    BinaryCounterReading_setInvalid(&M_IT_TB_1_data[i], IEC60870_QUALITY_INVALID);
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_IT_TB_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_SP_NA_1 config
    cJSON *m_sp_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_SP_NA_1_config");
    if (cJSON_IsArray(m_sp_na_1_config)) {
        config_M_SP_NA_1.count = cJSON_GetArraySize(m_sp_na_1_config);
        config_M_SP_NA_1.ioa_list = (int*)malloc(config_M_SP_NA_1.count * sizeof(int));
        
        if (config_M_SP_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_SP_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_sp_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_SP_NA_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_SP_NA_1_data
            M_SP_NA_1_data = (struct sM_SP_NA_1*)malloc(config_M_SP_NA_1.count * sizeof(struct sM_SP_NA_1));
            if (M_SP_NA_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_SP_NA_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_SP_NA_1.count; i++) {
                    M_SP_NA_1_data[i].value = false;
                    M_SP_NA_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
                // allocate memory for last_offline_update_M_SP_NA_1
                last_offline_update_M_SP_NA_1 = (uint64_t*)malloc(config_M_SP_NA_1.count * sizeof(uint64_t));
                if (last_offline_update_M_SP_NA_1 != NULL) {
                    for (int i = 0; i < config_M_SP_NA_1.count; i++) last_offline_update_M_SP_NA_1[i] = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_SP_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_DP_NA_1 config
    cJSON *m_dp_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_DP_NA_1_config");
    if (cJSON_IsArray(m_dp_na_1_config)) {
        config_M_DP_NA_1.count = cJSON_GetArraySize(m_dp_na_1_config);
        config_M_DP_NA_1.ioa_list = (int*)malloc(config_M_DP_NA_1.count * sizeof(int));
        
        if (config_M_DP_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_DP_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_dp_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_DP_NA_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_DP_NA_1_data
            M_DP_NA_1_data = (struct sM_DP_NA_1*)malloc(config_M_DP_NA_1.count * sizeof(struct sM_DP_NA_1));
            if (M_DP_NA_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_DP_NA_1_data\"}\n");
                status = false;
            } else {
                // Initialize values 
                for (int i = 0; i < config_M_DP_NA_1.count; i++) {
                    M_DP_NA_1_data[i].value = 0;
                    M_DP_NA_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_DP_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_ME_NA_1 config
    cJSON *m_me_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_ME_NA_1_config");
    if (cJSON_IsArray(m_me_na_1_config)) {
        config_M_ME_NA_1.count = cJSON_GetArraySize(m_me_na_1_config);
        config_M_ME_NA_1.ioa_list = (int*)malloc(config_M_ME_NA_1.count * sizeof(int));
        
        if (config_M_ME_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_ME_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_me_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_ME_NA_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_ME_NA_1_data
            M_ME_NA_1_data = (struct sM_ME_NA_1*)malloc(config_M_ME_NA_1.count * sizeof(struct sM_ME_NA_1));
            if (M_ME_NA_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_ME_NA_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_ME_NA_1.count; i++) {
                    M_ME_NA_1_data[i].value = 0;
                    M_ME_NA_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
                // Allocate memory for last_offline_update_M_ME_NA_1
                last_offline_update_M_ME_NA_1 = (uint64_t*)malloc(config_M_ME_NA_1.count * sizeof(uint64_t));
                if (last_offline_update_M_ME_NA_1 != NULL) {
                    for (int i = 0; i < config_M_ME_NA_1.count; i++) last_offline_update_M_ME_NA_1[i] = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_ME_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_ME_NB_1 config
    cJSON *m_me_nb_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_ME_NB_1_config");
    if (cJSON_IsArray(m_me_nb_1_config)) {
        config_M_ME_NB_1.count = cJSON_GetArraySize(m_me_nb_1_config);
        config_M_ME_NB_1.ioa_list = (int*)malloc(config_M_ME_NB_1.count * sizeof(int));
        
        if (config_M_ME_NB_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_ME_NB_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_me_nb_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_ME_NB_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_ME_NB_1_data
            M_ME_NB_1_data = (struct sM_ME_NB_1*)malloc(config_M_ME_NB_1.count * sizeof(struct sM_ME_NB_1));
            if (M_ME_NB_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_ME_NB_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_ME_NB_1.count; i++) {
                    M_ME_NB_1_data[i].value = 0;
                    M_ME_NB_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_ME_NB_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_ME_NC_1 config
    cJSON *m_me_nc_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_ME_NC_1_config");
    if (cJSON_IsArray(m_me_nc_1_config)) {
        config_M_ME_NC_1.count = cJSON_GetArraySize(m_me_nc_1_config);
        config_M_ME_NC_1.ioa_list = (int*)malloc(config_M_ME_NC_1.count * sizeof(int));
        
        if (config_M_ME_NC_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_ME_NC_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_me_nc_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_ME_NC_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_ME_NC_1_data
            M_ME_NC_1_data = (struct sM_ME_NC_1*)malloc(config_M_ME_NC_1.count * sizeof(struct sM_ME_NC_1));
            if (M_ME_NC_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_ME_NC_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_ME_NC_1.count; i++) {
                    M_ME_NC_1_data[i].value = 0.0f;
                    M_ME_NC_1_data[i].qualifier = IEC60870_QUALITY_INVALID;
                }
                // Allocate memory for last_offline_update_M_ME_NC_1
                last_offline_update_M_ME_NC_1 = (uint64_t*)malloc(config_M_ME_NC_1.count * sizeof(uint64_t));
                if (last_offline_update_M_ME_NC_1 != NULL) {
                    for (int i = 0; i < config_M_ME_NC_1.count; i++) last_offline_update_M_ME_NC_1[i] = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_ME_NC_1 config\"}\n");
            status = false;
        }
    }

    // Parse M_ME_ND_1 config
    cJSON *m_me_nd_1_config = cJSON_GetObjectItemCaseSensitive(json, "M_ME_ND_1_config");
    if (cJSON_IsArray(m_me_nd_1_config)) {
        config_M_ME_ND_1.count = cJSON_GetArraySize(m_me_nd_1_config);
        config_M_ME_ND_1.ioa_list = (int*)malloc(config_M_ME_ND_1.count * sizeof(int));
        
        if (config_M_ME_ND_1.ioa_list != NULL) {
            for (int i = 0; i < config_M_ME_ND_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(m_me_nd_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_M_ME_ND_1.ioa_list[i] = item->valueint;
                }
            }
            // Allocate memory for M_ME_ND_1_data
            M_ME_ND_1_data = (struct sM_ME_ND_1*)malloc(config_M_ME_ND_1.count * sizeof(struct sM_ME_ND_1));
            if (M_ME_ND_1_data == NULL) {
                printf("{\"error\":\"failed to allocate memory for M_ME_ND_1_data\"}\n");
                status = false;
            } else {
                // Initialize values
                for (int i = 0; i < config_M_ME_ND_1.count; i++) {
                    M_ME_ND_1_data[i].value = 0;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for M_ME_ND_1 config\"}\n");
            status = false;
        }
    }

    // Parse command configs without data allocation
    // Parse C_SC_NA_1 config
    cJSON *c_sc_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "C_SC_NA_1_config");
    if (cJSON_IsArray(c_sc_na_1_config)) {
        config_C_SC_NA_1.count = cJSON_GetArraySize(c_sc_na_1_config);
        config_C_SC_NA_1.ioa_list = (int*)malloc(config_C_SC_NA_1.count * sizeof(int));
        
        if (config_C_SC_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_C_SC_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(c_sc_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_C_SC_NA_1.ioa_list[i] = item->valueint;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for C_SC_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse C_DC_NA_1 config
    cJSON *c_dc_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "C_DC_NA_1_config");
    if (cJSON_IsArray(c_dc_na_1_config)) {
        config_C_DC_NA_1.count = cJSON_GetArraySize(c_dc_na_1_config);
        config_C_DC_NA_1.ioa_list = (int*)malloc(config_C_DC_NA_1.count * sizeof(int));
        
        if (config_C_DC_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_C_DC_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(c_dc_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_C_DC_NA_1.ioa_list[i] = item->valueint;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for C_DC_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse C_SE_NA_1 config
    cJSON *c_se_na_1_config = cJSON_GetObjectItemCaseSensitive(json, "C_SE_NA_1_config");
    if (cJSON_IsArray(c_se_na_1_config)) {
        config_C_SE_NA_1.count = cJSON_GetArraySize(c_se_na_1_config);
        config_C_SE_NA_1.ioa_list = (int*)malloc(config_C_SE_NA_1.count * sizeof(int));
        
        if (config_C_SE_NA_1.ioa_list != NULL) {
            for (int i = 0; i < config_C_SE_NA_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(c_se_na_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_C_SE_NA_1.ioa_list[i] = item->valueint;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for C_SE_NA_1 config\"}\n");
            status = false;
        }
    }

    // Parse C_SE_NC_1 config
    cJSON *c_se_nc_1_config = cJSON_GetObjectItemCaseSensitive(json, "C_SE_NC_1_config");
    if (cJSON_IsArray(c_se_nc_1_config)) {
        config_C_SE_NC_1.count = cJSON_GetArraySize(c_se_nc_1_config);
        config_C_SE_NC_1.ioa_list = (int*)malloc(config_C_SE_NC_1.count * sizeof(int));
        
        if (config_C_SE_NC_1.ioa_list != NULL) {
            for (int i = 0; i < config_C_SE_NC_1.count; i++) {
                cJSON *item = cJSON_GetArrayItem(c_se_nc_1_config, i);
                if (cJSON_IsNumber(item)) {
                    config_C_SE_NC_1.ioa_list[i] = item->valueint;
                }
            }
        } else {
            printf("{\"error\":\"failed to allocate memory for C_SE_NC_1 config\"}\n");
            status = false;
        }
    }
    
    // Parse mode_command config (default to "direct" if not present)
    // static char command_mode[64] = "direct";
    cJSON *mode_command = cJSON_GetObjectItemCaseSensitive(json, "mode_command");
    if (cJSON_IsString(mode_command) && mode_command->valuestring != NULL) {
        if (strcmp(mode_command->valuestring, "sbo") == 0 || strcmp(mode_command->valuestring, "direct") == 0) {
            strncpy(command_mode, mode_command->valuestring, sizeof(command_mode) - 1);
            command_mode[sizeof(command_mode) - 1] = '\0';
        } else {
            // If invalid value, fallback to "direct"
            strncpy(command_mode, "direct", sizeof(command_mode) - 1);
            command_mode[sizeof(command_mode) - 1] = '\0';
        }
    }

    // Parse periodic configuration
    cJSON *periodic = cJSON_GetObjectItemCaseSensitive(json, "periodic");
    if (cJSON_IsObject(periodic)) {
        cJSON *me_nc_1 = cJSON_GetObjectItemCaseSensitive(periodic, "M_ME_NC_1");
        if (cJSON_IsObject(me_nc_1)) {
            cJSON *enabled = cJSON_GetObjectItemCaseSensitive(me_nc_1, "enabled");
            cJSON *period = cJSON_GetObjectItemCaseSensitive(me_nc_1, "period_ms");
            
            // Đổi logic kiểm tra enabled 
            if (enabled != NULL) {
                periodic_M_ME_NC_1.enabled = cJSON_IsTrue(enabled);
            }
            
            if (period && cJSON_IsNumber(period)) {
                periodic_M_ME_NC_1.period_ms = period->valueint;
            }

            printf("{\"info\":\"Read periodic config - enabled:%d, period:%d ms\"}\n", 
                periodic_M_ME_NC_1.enabled, periodic_M_ME_NC_1.period_ms);
        }
                // Xử lý M_SP_TB_1
        cJSON *sp_tb_1 = cJSON_GetObjectItemCaseSensitive(periodic, "M_SP_TB_1");
        if (cJSON_IsObject(sp_tb_1)) {
            cJSON *enabled = cJSON_GetObjectItemCaseSensitive(sp_tb_1, "enabled");
            cJSON *period = cJSON_GetObjectItemCaseSensitive(sp_tb_1, "period_ms");
            
            if (enabled != NULL) {
                periodic_M_SP_TB_1.enabled = cJSON_IsTrue(enabled);
            }
            if (period && cJSON_IsNumber(period)) {
                periodic_M_SP_TB_1.period_ms = period->valueint;
            }
            
            printf("{\"info\":\"Read periodic M_SP_TB_1 config - enabled:%d, period:%d ms\"}\n", 
                   periodic_M_SP_TB_1.enabled, periodic_M_SP_TB_1.period_ms);
        }
    }

    cJSON_Delete(json);
    return status;
}

static bool init_config_from_file(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("{\"error\":\"failed to open config file: %s\"}\n", filename);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);

    if (length <= 0 || length > 65536) {
        printf("{\"error\":\"invalid config file size\"}\n");
        fclose(file);
        return false;
    }

    char* buffer = (char*)malloc(length + 1);
    if (buffer == NULL) {
        printf("{\"error\":\"failed to allocate memory for config\"}\n");
        fclose(file);
        return false;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';  // null-terminate
    fclose(file);

    bool result = parse_config_from_json(buffer);
    free(buffer);
    return result;
}

void* log_queue_count_periodic(void* arg) {
    CS104_Slave slave = (CS104_Slave)arg;
    while (running) {
        int queue_count = 0;
        if (slave && CS104_Slave_isRunning(slave)) {
            queue_count = CS104_Slave_getNumberOfQueueEntries(slave, NULL);
        }
        printf("{\"info\":\"queue_count=%d\"}\n", queue_count);
        Thread_sleep(1000);
    }
    return NULL;
}

// Server initialization and control functions
static CS104_Slave init_server(void) {
    CS104_Slave slave = CS104_Slave_create(20000, 20000);
    if (!slave) {
        printf("{\"error\":\"failed to create slave\"}\n"); 
        return NULL;
    }

    CS104_Slave_setLocalAddress(slave, IP_ADDRESS_LOCAL);
    CS104_Slave_setServerMode(slave, CS104_MODE_SINGLE_REDUNDANCY_GROUP);

    appLayerParameters = CS104_Slave_getAppLayerParameters(slave);

    CS104_Slave_setClockSyncHandler(slave, clockSyncHandler, NULL);
    CS104_Slave_setInterrogationHandler(slave, interrogationHandler, NULL);
    CS104_Slave_setResetProcessHandler(slave, resetProcessHandler, NULL);
    CS104_Slave_setASDUHandler(slave, asduHandler, NULL);
    CS104_Slave_setConnectionRequestHandler(slave, connectionRequestHandler, NULL);
    CS104_Slave_setConnectionEventHandler(slave, ConnectionEventHandler, NULL);
    pthread_t log_thread;
    pthread_create(&log_thread, NULL, log_queue_count_periodic, slave);
    pthread_detach(log_thread);

    return slave;
}

static bool start_server(CS104_Slave slave) {
    CS104_Slave_start(slave);
    if (!CS104_Slave_isRunning(slave)) {
        printf("{\"error\":\"server start failed\"}\n");
        return false;
    }

    // Start periodic sending thread if enabled
    printf("{\"debug\":\"periodic enabled:%d, points:%d\"}\n", 
           periodic_M_ME_NC_1.enabled, config_M_ME_NC_1.count);
           
    if (periodic_M_ME_NC_1.enabled && config_M_ME_NC_1.count > 0) {
        pthread_t periodic_thread;
        if (pthread_create(&periodic_thread, NULL, SendPeriodicData, slave) != 0) {
            printf("{\"error\":\"periodic send thread creation failed\"}\n");
            return false;
        }
        pthread_detach(periodic_thread);
        printf("{\"info\":\"periodic send thread started for M_ME_NC_1 with %d points\"}\n", 
               config_M_ME_NC_1.count);
    } else {
        printf("{\"info\":\"periodic send not enabled or no points configured\"}\n");
    }

    printf("{\"info\":\"server start succeeded\"}\n");
    return true;
}

static bool start_integrated_totals_thread(CS104_Slave slave) {
    pthread_t IntegratedTotals_thread;
    if (pthread_create(&IntegratedTotals_thread, NULL, SendIntegratedTotalsPeriodic, slave) != 0) {
        printf("{\"error\":\"integrated totals thread creating failed\"}\n");
        return false;
    }
    pthread_detach(IntegratedTotals_thread);
    return true;
}

// JSON processing functions
static bool process_json_input(const char* input, TypeID* type, double* value, 
                             int* address, int* qualifier) {
    int status_count = 0;
    cJSON *json = cJSON_Parse(input);
    if (json == NULL) {
        printf("{\"error\":\"invalid json input\"}\n");
        return false;
    }

    // Parse type
    cJSON *type_json = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (cJSON_IsString(type_json) && (type_json->valuestring != NULL)) {
        *type = GetDataTypeNumber(type_json->valuestring);
        status_count++;
    } else {
        printf("{\"error\":\"invalid data type\"}\n");
    }

    // Parse value
    cJSON *value_json = cJSON_GetObjectItemCaseSensitive(json, "value");
    if (cJSON_IsNumber(value_json)) {
        *value = value_json->valuedouble;
        status_count++;
    } else {
        printf("{\"error\":\"invalid data value\"}\n");
    }

    // Parse address
    cJSON *address_json = cJSON_GetObjectItemCaseSensitive(json, "address");
    if (cJSON_IsNumber(address_json)) {
        *address = (int)(address_json->valuedouble);
        status_count++;
    } else {
        printf("{\"error\":\"invalid data address\"}\n");
    }

    // Parse qualifier
    cJSON *qualifier_json = cJSON_GetObjectItemCaseSensitive(json, "qualifier");
    if (cJSON_IsString(qualifier_json) && (qualifier_json->valuestring != NULL)) {
        *qualifier = GetQualifierNumber(qualifier_json->valuestring);
        status_count++;
    } else {
        printf("{\"error\":\"invalid data qualifier\"}\n");
    }

    cJSON_Delete(json);
    return (status_count == 4);
}

// Data update handling
static bool process_data_update(CS104_Slave slave, TypeID type, double value, 
                              int address, int qualifier) {
    int ioa_index = -1;
    
    switch(type) {
        case M_SP_TB_1:
            for (int i = 0; i < config_M_SP_TB_1.count; i++) {
                if (config_M_SP_TB_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_sp_tb_1_data(slave, ioa_index, (bool)value, qualifier) && 
                    IsClientConnected(slave)) {
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                    InformationObject io = (InformationObject) SinglePointWithCP56Time2a_create(
                            NULL, address, (bool)value, qualifier, GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                    CS104_Slave_enqueueASDU(slave, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_SP_TB_1\"}\n", address);
            }
            break;

        case M_DP_TB_1:
            for (int i = 0; i < config_M_DP_TB_1.count; i++) {
                if (config_M_DP_TB_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_dp_tb_1_data(slave, ioa_index, (DoublePointValue)value, qualifier) && IsClientConnected(slave)) {
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                    InformationObject io = (InformationObject) DoublePointWithCP56Time2a_create(
                            NULL, address, (DoublePointValue)value, qualifier, GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                    CS104_Slave_enqueueASDU(slave, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_DP_TB_1\"}\n", address);
            }
            break;
        case M_ME_TD_1:
            for (int i = 0; i < config_M_ME_TD_1.count; i++) {
                if (config_M_ME_TD_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_me_td_1_data(slave, ioa_index, (float)value, qualifier) && IsClientConnected(slave)) {
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                    InformationObject io = (InformationObject) MeasuredValueNormalizedWithCP56Time2a_create(
                            NULL, address, (float)value, qualifier, GetCP56Time2a());
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                    CS104_Slave_enqueueASDU(slave, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_ME_TD_1\"}\n", address);
            }
            break;
        case M_IT_TB_1:
            for (int i = 0; i < config_M_IT_TB_1.count; i++) {
                if (config_M_IT_TB_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                update_m_it_tb_1_data(slave, ioa_index, (uint64_t)(value) % UINT32_MAX);
            } else {
                printf("{\"error\":\"IOA %d not configured for M_IT_TB_1\"}\n", address);
            }
            break;
        case M_SP_NA_1:
            for (int i = 0; i < config_M_SP_NA_1.count; i++) {
                if (config_M_SP_NA_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_sp_na_1_data(slave, ioa_index, (uint32_t)value, qualifier) && IsClientConnected(slave)) {
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                    InformationObject io = (InformationObject) SinglePointInformation_create(
                            NULL, address, (bool)value, qualifier);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                    CS104_Slave_enqueueASDU(slave, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_SP_NA_1\"}\n", address);
            }
            break;
        case M_ME_ND_1:
            for (int i = 0; i < config_M_ME_ND_1.count; i++) {
                if (config_M_ME_ND_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_me_nd_1_data(slave, ioa_index, (uint32_t)value) && IsClientConnected(slave)) {
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                    InformationObject io = (InformationObject) MeasuredValueNormalized_create(
                            NULL, address, (float)value, 0);
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                    CS104_Slave_enqueueASDU(slave, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_ME_ND_1\"}\n", address);
            }
            break;
        case M_ME_NC_1:
            for (int i = 0; i < config_M_ME_NC_1.count; i++) {
                if (config_M_ME_NC_1.ioa_list[i] == address) {
                    ioa_index = i;
                    break;
                }
            }
            
            if (ioa_index >= 0) {
                if (update_m_me_nc_1_data(slave, ioa_index, (float)value, qualifier)) {
                    if (IsClientConnected(slave)) {
                        CS101_ASDU newAsdu = CS101_ASDU_create(
                            appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                        InformationObject io = (InformationObject) MeasuredValueShort_create(
                                NULL, address, (float)value, qualifier);
                        CS101_ASDU_addInformationObject(newAsdu, io);
                        InformationObject_destroy(io);
                        CS104_Slave_enqueueASDU(slave, newAsdu);
                        CS101_ASDU_destroy(newAsdu);
                    } else {
                        printf("{\"error\":\"client not connected, cannot send M_ME_NC_1 update\"}\n");
                        // Lưu trữ dưới dạng queue của M_ME_TF_1 (Measured value, short floating point with time tag)
                        CS101_ASDU newAsdu = CS101_ASDU_create(
                            appLayerParameters, false, CS101_COT_SPONTANEOUS, ASDU, ASDU, false, false);
                        InformationObject io = (InformationObject) MeasuredValueShortWithCP56Time2a_create(
                                NULL, address, (float)value, qualifier, GetCP56Time2a());
                        CS101_ASDU_addInformationObject(newAsdu, io);
                        InformationObject_destroy(io);
                        CS104_Slave_enqueueASDU(slave, newAsdu);
                        CS101_ASDU_destroy(newAsdu);
                    }
                }
            } else {
                printf("{\"error\":\"IOA %d not configured for M_ME_NC_1\"}\n", address);
            }
            break;
        default:
            printf("{\"error\":\"unknown type\"}\n");
            break;
    }
    return true;
}

// Main server loop function
static bool run_server_loop(CS104_Slave slave) {
    char input[1000];
    TypeID type;
    double value;
    int address;
    int qualifier;
    
    setvbuf(stdout, NULL, _IOLBF, 1024);

    while (running) {
        if(fgets(input, 1000, stdin) == NULL) {
            printf("{\"error\":\"get input string failed\"}\n");
            continue;
        }

        if (process_json_input(input, &type, &value, &address, &qualifier)) {
            if (type == 0 && value == 0) {
                running = false;
                printf("{\"info\":\"shutdown server\"}\n");
                continue;
            }
            process_data_update(slave, type, value, address, qualifier);
        }
        memset(input, 0, sizeof(input));
    }
    
    return true;
}
