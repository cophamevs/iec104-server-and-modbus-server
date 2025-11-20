#ifndef PERIODIC_SENDER_H
#define PERIODIC_SENDER_H

#include "cs104_slave.h"
#include <stdbool.h>

typedef struct {
    bool enabled;
    int period_ms;
    uint64_t last_sent;
} PeriodicConfig;

// Global periodic configs (exposed for config parser)
extern PeriodicConfig g_periodic_M_ME_NC_1;
extern PeriodicConfig g_periodic_M_SP_TB_1;

void start_periodic_sender(CS104_Slave slave);
void stop_periodic_sender(void);

#endif // PERIODIC_SENDER_H
