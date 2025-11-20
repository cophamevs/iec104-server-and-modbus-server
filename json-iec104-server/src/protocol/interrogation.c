#include "interrogation.h"
#include "../data/data_manager.h"
#include "../data/data_types.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <string.h>

// External globals (will be refactored later)
extern CS101_AppLayerParameters alParameters;
extern int ASDU;

/**
 * Calculate maximum number of Information Objects per ASDU for SQ=0 (individual)
 */
static int calcMaxIOAs_SQ0(int maxASDUSize, int ioSize) {
    // ASDU overhead: 6 bytes
    // Each IO includes: IOA (3 bytes) + data
    return (maxASDUSize - 6) / ioSize;
}

/**
 * Calculate maximum number of Information Objects per ASDU for SQ=1 (sequence)
 * For consecutive IOAs, only first IO has IOA, rest are just data
 */
static int calcMaxIOAs_SQ1(int maxASDUSize, int ioSizeWithIOA, int ioSizeNoIOA) {
    // ASDU overhead: 6 bytes
    // First IO: ioSizeWithIOA (includes IOA)
    // Remaining IOs: ioSizeNoIOA (no IOA, saves 3 bytes each)
    // Formula: 6 + ioSizeWithIOA + (n-1) * ioSizeNoIOA <= maxASDUSize
    // Solving: n <= (maxASDUSize - 6 - ioSizeWithIOA) / ioSizeNoIOA + 1
    if (maxASDUSize <= 6 + ioSizeWithIOA) {
        return 1;
    }
    return ((maxASDUSize - 6 - ioSizeWithIOA) / ioSizeNoIOA) + 1;
}

/**
 * Get IO size without IOA (for SQ=1 sequence mode)
 */
static int getIOSizeNoIOA(int ioSizeWithIOA) {
    // Remove IOA size (3 bytes) from total IO size
    return ioSizeWithIOA - 3;
}

/**
 * Create InformationObject based on data type
 * This is the key function that handles all 10 data types generically
 */
InformationObject create_io_for_type(TypeID type_id, int ioa, const DataValue* data) {
    switch (type_id) {
        case M_SP_TB_1:
            return (InformationObject)SinglePointWithCP56Time2a_create(
                NULL, ioa, data->value.bool_val, data->quality, (CP56Time2a)&data->timestamp);

        case M_DP_TB_1:
            return (InformationObject)DoublePointWithCP56Time2a_create(
                NULL, ioa, data->value.dp_val, data->quality, (CP56Time2a)&data->timestamp);

        case M_ME_TD_1:
            return (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
                NULL, ioa, data->value.float_val, data->quality, (CP56Time2a)&data->timestamp);

        case M_IT_TB_1: {
            // Integrated totals with time tag - BinaryCounterReading is a pointer type
            BinaryCounterReading bcr = BinaryCounterReading_create(NULL, 
                (int32_t)data->value.uint32_val, 0, false, false, false);
            InformationObject io = (InformationObject)IntegratedTotalsWithCP56Time2a_create(
                NULL, ioa, bcr, (CP56Time2a)&data->timestamp);
            BinaryCounterReading_destroy(bcr);
            return io;
        }

        case M_SP_NA_1:
            return (InformationObject)SinglePointInformation_create(
                NULL, ioa, data->value.bool_val, data->quality);

        case M_DP_NA_1:
            return (InformationObject)DoublePointInformation_create(
                NULL, ioa, data->value.dp_val, data->quality);

        case M_ME_NA_1:
            return (InformationObject)MeasuredValueNormalized_create(
                NULL, ioa, data->value.float_val, data->quality);

        case M_ME_NB_1:
            return (InformationObject)MeasuredValueScaled_create(
                NULL, ioa, data->value.int16_val, data->quality);

        case M_ME_NC_1:
            return (InformationObject)MeasuredValueShort_create(
                NULL, ioa, data->value.float_val, data->quality);

        case M_ME_ND_1:
            // Measured value normalized without quality
            return (InformationObject)MeasuredValueNormalizedWithoutQuality_create(
                NULL, ioa, data->value.float_val);

        // Offline equivalent types (with timestamps)
        case M_ME_TF_1:
            // Short floating point with time tag (offline equivalent of M_ME_NC_1)
            return (InformationObject)MeasuredValueShortWithCP56Time2a_create(
                NULL, ioa, data->value.float_val, data->quality, (CP56Time2a)&data->timestamp);

        case M_ME_TB_1:
            // Scaled value with time tag (offline equivalent of M_ME_NB_1)
            return (InformationObject)MeasuredValueScaledWithCP56Time2a_create(
                NULL, ioa, data->value.int16_val, data->quality, (CP56Time2a)&data->timestamp);

        default:
            LOG_ERROR("Unknown type_id %d in create_io_for_type", type_id);
            return NULL;
    }
}

/**
 * Create InformationObject for offline queueing
 * Converts non-timestamped types to their timestamped equivalents
 */
InformationObject create_offline_io_for_type(TypeID original_type, int ioa, const DataValue* data) {
    const DataTypeInfo* info = get_data_type_info(original_type);
    if (!info || info->offline_equivalent == 0) {
        return NULL;  // No offline support for this type
    }
    
    // Create IO using the offline equivalent type (which has timestamp)
    return create_io_for_type(info->offline_equivalent, ioa, data);
}

/**
 * Send interrogation data for one data type with SQ=1 optimization
 * Detects consecutive IOA sequences and uses sequence mode for efficiency
 */
bool send_interrogation_for_type(IMasterConnection connection,
                                 DataTypeContext* ctx,
                                 int asdu_addr) {
    (void)asdu_addr; // Use configured ASDU instead

    if (!connection || !ctx) {
        LOG_ERROR("Invalid parameters to send_interrogation_for_type");
        return false;
    }

    pthread_mutex_lock(&ctx->mutex);

    if (ctx->config.count > 0) {
        int maxASDUSize = alParameters->maxSizeOfASDU;
        int ioSizeWithIOA = ctx->type_info->io_size;
        int ioSizeNoIOA = getIOSizeNoIOA(ioSizeWithIOA);

        LOG_DEBUG("Sending %s: count=%d", ctx->type_info->name, ctx->config.count);

        // Process IOAs in blocks (consecutive sequences use SQ=1, others use SQ=0)
        int i = 0;
        while (i < ctx->config.count) {
            // Find length of consecutive sequence starting at i
            int seq_len = 1;
            while (i + seq_len < ctx->config.count &&
                   ctx->config.ioa_list[i + seq_len] == ctx->config.ioa_list[i + seq_len - 1] + 1) {
                seq_len++;
            }

            // Decide: use SQ=1 for sequences of 3+ consecutive IOAs, else SQ=0
            bool useSequence = (seq_len >= 3);

            if (useSequence) {
                // SQ=1: Sequence mode for consecutive IOAs
                int maxIOAs = calcMaxIOAs_SQ1(maxASDUSize, ioSizeWithIOA, ioSizeNoIOA);

                // Send consecutive IOAs in chunks using SQ=1
                for (int j = 0; j < seq_len; j += maxIOAs) {
                    int chunk_len = (j + maxIOAs < seq_len) ? maxIOAs : (seq_len - j);

                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        alParameters, true,  // SQ=1 (sequence mode)
                        CS101_COT_INTERROGATED_BY_STATION,
                        0, ASDU, false, false
                    );

                    if (!newAsdu) {
                        LOG_ERROR("Failed to create ASDU for %s", ctx->type_info->name);
                        pthread_mutex_unlock(&ctx->mutex);
                        return false;
                    }

                    // Add consecutive IOs to ASDU
                    for (int k = 0; k < chunk_len; k++) {
                        InformationObject io = create_io_for_type(ctx->type_id,
                            ctx->config.ioa_list[i + j + k], &ctx->data_array[i + j + k]);

                        if (io) {
                            CS101_ASDU_addInformationObject(newAsdu, io);
                            InformationObject_destroy(io);
                        }
                    }

                    IMasterConnection_sendASDU(connection, newAsdu);
                    CS101_ASDU_destroy(newAsdu);

                    LOG_DEBUG("Sent %s SQ=1: IOAs %d-%d (%d values)",
                             ctx->type_info->name,
                             ctx->config.ioa_list[i + j],
                             ctx->config.ioa_list[i + j + chunk_len - 1],
                             chunk_len);
                }
            } else {
                // SQ=0: Individual mode for non-consecutive or short sequences
                int maxIOAs = calcMaxIOAs_SQ0(maxASDUSize, ioSizeWithIOA);

                // Send individual IOAs
                for (int j = 0; j < seq_len; j += maxIOAs) {
                    int chunk_len = (j + maxIOAs < seq_len) ? maxIOAs : (seq_len - j);

                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        alParameters, false,  // SQ=0 (individual mode)
                        CS101_COT_INTERROGATED_BY_STATION,
                        0, ASDU, false, false
                    );

                    if (!newAsdu) {
                        LOG_ERROR("Failed to create ASDU for %s", ctx->type_info->name);
                        pthread_mutex_unlock(&ctx->mutex);
                        return false;
                    }

                    for (int k = 0; k < chunk_len; k++) {
                        InformationObject io = create_io_for_type(ctx->type_id,
                            ctx->config.ioa_list[i + j + k], &ctx->data_array[i + j + k]);

                        if (io) {
                            CS101_ASDU_addInformationObject(newAsdu, io);
                            InformationObject_destroy(io);
                        }
                    }

                    IMasterConnection_sendASDU(connection, newAsdu);
                    CS101_ASDU_destroy(newAsdu);
                }
            }

            i += seq_len;
        }
    }

    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

/**
 * Main interrogation handler
 * This replaces the old 240-line function with ~80 lines
 */
bool interrogationHandler(void* parameter, IMasterConnection connection,
                         CS101_ASDU asdu, uint8_t qoi) {
    (void)parameter; // Unused in this implementation

    LOG_INFO("Interrogation received: QOI=%d", qoi);

    if (qoi == 20) {  // Station interrogation (QOI=20)
        // Send ACT-CON (activation confirmation)
        IMasterConnection_sendACT_CON(connection, asdu, false);

        int asdu_addr = CS101_ASDU_getCA(asdu);
        LOG_INFO("Station interrogation for ASDU=%d", asdu_addr);

        // Iterate through all data types and send their data
        // This single loop replaces 9 duplicate blocks!
        for (int i = 0; i < DATA_TYPE_COUNT; i++) {
            if (!send_interrogation_for_type(connection, &g_data_contexts[i], asdu_addr)) {
                LOG_WARN("Failed to send interrogation for type %s",
                       g_data_contexts[i].type_info->name);
                // Continue with other types even if one fails
            }
        }

        // Send ACT-TERM (activation termination)
        IMasterConnection_sendACT_TERM(connection, asdu);

        LOG_INFO("Station interrogation completed");
        return true;
    } else {
        // Unsupported QOI - send negative confirmation
        LOG_WARN("Unsupported QOI=%d, sending negative ACT-CON", qoi);
        IMasterConnection_sendACT_CON(connection, asdu, true);
        return false;
    }
}
