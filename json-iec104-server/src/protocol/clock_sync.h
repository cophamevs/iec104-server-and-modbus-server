#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include "cs104_slave.h"

bool clockSyncHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, CP56Time2a newTime);

#endif // CLOCK_SYNC_H
