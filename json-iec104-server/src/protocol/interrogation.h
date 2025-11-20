#ifndef INTERROGATION_H
#define INTERROGATION_H

#include <stdbool.h>
#include "../../lib60870/lib60870-C/src/inc/api/iec60870_slave.h"
#include "../../lib60870/lib60870-C/src/inc/api/cs104_slave.h"
#include "../data/data_manager.h"

/**
 * Generic interrogation handler for IEC 60870-5-104
 * This replaces 9 duplicate blocks (~200 lines) with a single generic implementation
 * 
 * @param parameter User-defined parameter (typically CS104_Slave)
 * @param connection The master connection requesting interrogation
 * @param asdu The ASDU containing the interrogation command
 * @param qoi Qualifier of Interrogation (20 = station interrogation)
 * @return true if interrogation was handled successfully
 */
bool interrogationHandler(void* parameter, IMasterConnection connection,
                         CS101_ASDU asdu, uint8_t qoi);

/**
 * Send interrogation data for a specific data type
 * Helper function used internally by interrogationHandler
 * 
 * @param connection The master connection
 * @param ctx The data type context to send
 * @param asdu_addr The ASDU address to use
 * @return true on success
 */
bool send_interrogation_for_type(IMasterConnection connection,
                                DataTypeContext* ctx,
                                int asdu_addr);

/**
 * Create Information Object for a specific type
 * 
 * @param type_id The IEC104 TypeID
 * @param ioa The Information Object Address
 * @param value The data value
 * @return InformationObject instance or NULL on error
 */
InformationObject create_io_for_type(TypeID type_id, int ioa, const DataValue* value);

/**
 * Create Information Object for offline queueing
 * Converts non-timestamped types to their timestamped equivalents
 * 
 * @param original_type The original TypeID (e.g., M_ME_NC_1)
 * @param ioa The Information Object Address
 * @param data The data value (timestamp will be added)
 * @return InformationObject with timestamp or NULL if no offline support
 */
InformationObject create_offline_io_for_type(TypeID original_type, int ioa, const DataValue* data);

#endif // INTERROGATION_H
