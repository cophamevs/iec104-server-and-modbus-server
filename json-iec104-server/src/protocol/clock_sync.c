#include "clock_sync.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

bool clockSyncHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, CP56Time2a newTime)
{
    (void)parameter;  // Unused
    
    LOG_DEBUG("Clock sync handler triggered");
    
    // Parse CP56Time2a to struct tm
    struct tm tmTime;
    tmTime.tm_sec = CP56Time2a_getSecond(newTime);
    tmTime.tm_min = CP56Time2a_getMinute(newTime);
    tmTime.tm_hour = CP56Time2a_getHour(newTime);
    tmTime.tm_mday = CP56Time2a_getDayOfMonth(newTime);
    tmTime.tm_mon = CP56Time2a_getMonth(newTime) - 1;  // tm_mon is 0-11
    tmTime.tm_year = CP56Time2a_getYear(newTime) + 100;  // tm_year is years since 1900
    tmTime.tm_isdst = -1;  // Let mktime determine DST
    mktime(&tmTime);
    
    // Format to ISO8601 with timezone
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%FT%X%z", &tmTime);
    
    // Log JSON for external handler
    printf("{\"type\":\"C_CS_NA_1\",\"value\":\"%s\"}\n", buffer);
    fflush(stdout);
    
    LOG_INFO("Clock sync received: %s", buffer);

#ifdef CLOCKSYNC_FROM_IEC
    // Update system time using /usr/bin/date
    char cmd_buf[200];
    snprintf(cmd_buf, sizeof(cmd_buf), "sudo /usr/bin/date --set=\"%s\" >/dev/null", buffer);
    
    if (system(cmd_buf) != 0) {
        LOG_WARN("Failed to set system time");
        printf("{\"warn\":\"set system time failed\"}\n");
    } else {
        LOG_INFO("System time updated successfully");
        printf("{\"info\":\"set system time success\"}\n");
    }
    fflush(stdout);
#endif /* CLOCKSYNC_FROM_IEC */

    // Send activation confirmation
    CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
    IMasterConnection_sendASDU(connection, asdu);

    return true;
}
