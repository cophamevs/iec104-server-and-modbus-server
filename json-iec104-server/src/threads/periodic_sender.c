#include "periodic_sender.h"
#include "../data/data_manager.h"
#include "../protocol/interrogation.h" // For create_io_for_type
#include "../utils/logger.h"
#include "hal_time.h"
#include "hal_thread.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

// External globals
extern int ASDU;

// Global periodic configs
PeriodicConfig g_periodic_M_ME_NC_1 = {false, 5000, 0};
PeriodicConfig g_periodic_M_SP_TB_1 = {false, 5000, 0};

static pthread_t periodic_thread;
static bool running = false;
static CS104_Slave slave_instance = NULL;

// Helper to calculate max IOAs for SQ=0
static int calcMaxIOAs_SQ0(int maxASDUSize, int ioSize) {
    return (maxASDUSize - 6) / ioSize;
}

// Helper to calculate max IOAs for SQ=1
static int calcMaxIOAs_SQ1(int maxASDUSize, int ioSizeWithIOA, int ioSizeNoIOA) {
    if (maxASDUSize <= 6 + ioSizeWithIOA) {
        return 1;
    }
    return ((maxASDUSize - 6 - ioSizeWithIOA) / ioSizeNoIOA) + 1;
}

// Get IO size without IOA (for SQ=1)
static int getIOSizeNoIOA(int ioSizeWithIOA) {
    return ioSizeWithIOA - 3;
}

static void send_periodic_data_for_type(DataTypeContext* ctx, PeriodicConfig* config) {
    if (!config->enabled || ctx->config.count == 0) return;

    uint64_t now = Hal_getTimeInMs();
    if (now - config->last_sent < config->period_ms) return;

    LOG_DEBUG("Sending periodic data for %s", ctx->type_info->name);

    pthread_mutex_lock(&ctx->mutex);

    CS101_AppLayerParameters alParams = CS104_Slave_getAppLayerParameters(slave_instance);
    int maxASDUSize = alParams->maxSizeOfASDU;
    int ioSizeWithIOA = ctx->type_info->io_size;
    int ioSizeNoIOA = getIOSizeNoIOA(ioSizeWithIOA);

    // Process IOAs with SQ=1 optimization
    int i = 0;
    while (i < ctx->config.count) {
        // Find consecutive sequence length
        int seq_len = 1;
        while (i + seq_len < ctx->config.count &&
               ctx->config.ioa_list[i + seq_len] == ctx->config.ioa_list[i + seq_len - 1] + 1) {
            seq_len++;
        }

        bool useSequence = (seq_len >= 3);

        if (useSequence) {
            // SQ=1 mode for consecutive IOAs
            int maxIOAs = calcMaxIOAs_SQ1(maxASDUSize, ioSizeWithIOA, ioSizeNoIOA);

            for (int j = 0; j < seq_len; j += maxIOAs) {
                int chunk_len = (j + maxIOAs < seq_len) ? maxIOAs : (seq_len - j);

                CS101_ASDU newAsdu = CS101_ASDU_create(
                    alParams, true,  // SQ=1
                    CS101_COT_PERIODIC,
                    0, ASDU, false, false
                );

                if (newAsdu) {
                    for (int k = 0; k < chunk_len; k++) {
                        InformationObject io = create_io_for_type(
                            ctx->type_id,
                            ctx->config.ioa_list[i + j + k],
                            &ctx->data_array[i + j + k]
                        );

                        if (io) {
                            CS101_ASDU_addInformationObject(newAsdu, io);
                            InformationObject_destroy(io);
                        }
                    }

                    CS104_Slave_enqueueASDU(slave_instance, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            }
        } else {
            // SQ=0 mode for non-consecutive
            int maxIOAs = calcMaxIOAs_SQ0(maxASDUSize, ioSizeWithIOA);

            for (int j = 0; j < seq_len; j += maxIOAs) {
                int chunk_len = (j + maxIOAs < seq_len) ? maxIOAs : (seq_len - j);

                CS101_ASDU newAsdu = CS101_ASDU_create(
                    alParams, false,  // SQ=0
                    CS101_COT_PERIODIC,
                    0, ASDU, false, false
                );

                if (newAsdu) {
                    for (int k = 0; k < chunk_len; k++) {
                        InformationObject io = create_io_for_type(
                            ctx->type_id,
                            ctx->config.ioa_list[i + j + k],
                            &ctx->data_array[i + j + k]
                        );

                        if (io) {
                            CS101_ASDU_addInformationObject(newAsdu, io);
                            InformationObject_destroy(io);
                        }
                    }

                    CS104_Slave_enqueueASDU(slave_instance, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            }
        }

        i += seq_len;
    }

    pthread_mutex_unlock(&ctx->mutex);
    config->last_sent = now;
}

void* periodic_sender_thread(void* arg) {
    (void)arg;
    LOG_INFO("Periodic sender thread started");

    while (running) {
        if (is_client_connected(slave_instance)) {
            send_periodic_data_for_type(get_data_context(M_ME_NC_1), &g_periodic_M_ME_NC_1);
            send_periodic_data_for_type(get_data_context(M_SP_TB_1), &g_periodic_M_SP_TB_1);
        }
        
        Thread_sleep(100); // Check every 100ms
    }

    LOG_INFO("Periodic sender thread stopped");
    return NULL;
}

void start_periodic_sender(CS104_Slave slave) {
    if (running) return;

    slave_instance = slave;
    running = true;
    
    if (pthread_create(&periodic_thread, NULL, periodic_sender_thread, NULL) != 0) {
        LOG_ERROR("Failed to create periodic sender thread");
        running = false;
    }
}

void stop_periodic_sender(void) {
    if (!running) return;

    running = false;
    pthread_join(periodic_thread, NULL);
}
