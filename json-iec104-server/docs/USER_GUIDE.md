# User Guide - IEC 60870-5-104 Server

**Version:** 1.0  
**Date:** 2025-11-20

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Installation](#installation)
3. [Configuration](#configuration)
4. [Running the Server](#running-the-server)
5. [Testing](#testing)
6. [Troubleshooting](#troubleshooting)
7. [Examples](#examples)

---

## Getting Started

### Prerequisites

- **Operating System:** Linux (Ubuntu 20.04+ recommended)
- **Compiler:** GCC 7.0+ or Clang 10.0+
- **Build Tools:** make, cmake
- **Libraries:** pthread, libm

### Quick Start

```bash
# 1. Clone the repository
cd json-iec104-server

# 2. Build lib60870
cd lib60870/lib60870-C
mkdir build && cd build
cmake ..
make
cd ../../..

# 3. Run tests
cd tests
make test

# 4. Create configuration
cp config.example.json config.json
# Edit config.json as needed

# 5. Build and run (when integrated)
make
./iec104-server config.json
```

---

## Installation

### Step 1: Install Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake git
```

#### CentOS/RHEL
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake git
```

### Step 2: Build lib60870

```bash
cd lib60870/lib60870-C
mkdir build && cd build
cmake ..
make
cd ../../..
```

### Step 3: Build Tests

```bash
cd tests
make all
```

### Step 4: Verify Installation

```bash
make test
```

Expected output:
```
========================================
Running Phase 1 Tests (data_types)...
========================================
All data_types tests passed!

... (all phases)

========================================
Running Phase 5 Tests (utils)...
========================================
All utils tests passed!
```

---

## Configuration

### Configuration File Format

The server uses JSON configuration files. Create `config.json`:

```json
{
  "offline_udt_time": 5000,
  "deadband_M_ME_NC_1_percent": 1.5,
  "asdu": 1,
  "command_mode": "direct",
  "port": 2404,
  "local_ip": "0.0.0.0",
  
  "M_SP_TB_1_config": [100, 101, 102],
  "M_DP_TB_1_config": [200, 201, 202],
  "M_ME_NC_1_config": [300, 301, 302],
  "M_IT_TB_1_config": [400, 401, 402]
}
```

### Configuration Parameters

#### Global Settings

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| `offline_udt_time` | uint32 | Offline update time (ms) | 5000 |
| `deadband_M_ME_NC_1_percent` | float | Deadband for float values (%) | 1.0 |
| `asdu` | int | ASDU address | 1 |
| `command_mode` | string | Command mode: "direct" or "select" | "direct" |
| `port` | int | TCP port | 2404 |
| `local_ip` | string | Local IP address | "0.0.0.0" |

#### Data Type Configurations

Each data type can have a configuration key:

| Config Key | Data Type | Description |
|------------|-----------|-------------|
| `M_SP_TB_1_config` | Single point with time | Boolean values |
| `M_DP_TB_1_config` | Double point with time | 2-bit values |
| `M_ME_TD_1_config` | Normalized measured with time | Float values |
| `M_IT_TB_1_config` | Integrated totals with time | Counter values |
| `M_SP_NA_1_config` | Single point | Boolean values |
| `M_DP_NA_1_config` | Double point | 2-bit values |
| `M_ME_NA_1_config` | Normalized measured | Float values |
| `M_ME_NB_1_config` | Scaled measured | Int16 values |
| `M_ME_NC_1_config` | Short floating point | Float values |
| `M_ME_ND_1_config` | Normalized without quality | Float values |

**Format:** Array of IOA (Information Object Address) integers

**Example:**
```json
"M_SP_TB_1_config": [100, 101, 102, 103, 104]
```

This configures 5 single-point values at IOAs 100-104.

### Configuration Examples

#### Minimal Configuration

```json
{
  "asdu": 1,
  "M_SP_TB_1_config": [100]
}
```

#### Full Configuration

```json
{
  "offline_udt_time": 10000,
  "deadband_M_ME_NC_1_percent": 2.0,
  "asdu": 47,
  "command_mode": "select",
  "port": 2404,
  "local_ip": "192.168.1.100",
  
  "M_SP_TB_1_config": [100, 101, 102, 103, 104],
  "M_DP_TB_1_config": [200, 201, 202],
  "M_ME_TD_1_config": [300, 301],
  "M_IT_TB_1_config": [400, 401, 402, 403],
  "M_SP_NA_1_config": [500, 501, 502],
  "M_DP_NA_1_config": [600, 601],
  "M_ME_NA_1_config": [700, 701, 702],
  "M_ME_NB_1_config": [800, 801],
  "M_ME_NC_1_config": [900, 901, 902, 903],
  "M_ME_ND_1_config": [1000, 1001]
}
```

---

## Running the Server

### Basic Usage

```bash
./iec104-server config.json
```

### With Logging

Set log level via environment variable:

```bash
# Debug level (all messages)
LOG_LEVEL=DEBUG ./iec104-server config.json

# Info level (default)
LOG_LEVEL=INFO ./iec104-server config.json

# Warn level (warnings and errors only)
LOG_LEVEL=WARN ./iec104-server config.json

# Error level (errors only)
LOG_LEVEL=ERROR ./iec104-server config.json
```

### Log Output

Logs are in JSON format:

```json
{"timestamp":"2025-11-20 19:03:30","level":"INFO","message":"Server started on port 2404"}
{"timestamp":"2025-11-20 19:03:31","level":"INFO","message":"Configuration parsed successfully"}
{"timestamp":"2025-11-20 19:03:32","level":"INFO","message":"Interrogation received: QOI=20"}
```

### Redirecting Logs

```bash
# To file
./iec104-server config.json > server.log 2>&1

# Errors to separate file
./iec104-server config.json 2> errors.log

# With log rotation (using logrotate)
./iec104-server config.json | rotatelogs logs/server.%Y%m%d.log 86400
```

---

## Testing

### Running Tests

#### All Tests
```bash
cd tests
make test
```

#### Individual Phase Tests
```bash
make test1  # Data types
make test2  # Data manager
make test3  # Config parser
make test4  # Interrogation
make test5  # Utils
```

#### Clean and Rebuild
```bash
make clean
make all
```

### Test Output

Successful test output:
```
===========================================
Running data_types test suite
===========================================

Testing get_data_type_info()...
  M_SP_TB_1 found
  M_ME_NC_1 found
  Invalid type returns NULL

... (more tests)

===========================================
All data_types tests passed!
===========================================
```

### Memory Leak Testing

Use valgrind to check for memory leaks:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_data_manager
```

Expected output:
```
==12345== HEAP SUMMARY:
==12345==     in use at exit: 0 bytes in 0 blocks
==12345==   total heap usage: 100 allocs, 100 frees, 10,000 bytes allocated
==12345== 
==12345== All heap blocks were freed -- no leaks are possible
```

---

## Troubleshooting

### Common Issues

#### 1. Configuration Not Loading

**Symptom:**
```json
{"timestamp":"...","level":"ERROR","message":"Failed to open config file: config.json"}
```

**Solution:**
- Check file exists: `ls -l config.json`
- Check file permissions: `chmod 644 config.json`
- Check JSON syntax: `python -m json.tool config.json`

#### 2. Invalid IOA Configuration

**Symptom:**
```json
{"timestamp":"...","level":"ERROR","message":"Invalid IOA at index 1 in M_SP_TB_1_config"}
```

**Solution:**
- Ensure all IOAs are integers
- Check JSON array format: `[100, 101, 102]`
- Remove trailing commas

#### 3. Port Already in Use

**Symptom:**
```
Failed to bind to port 2404: Address already in use
```

**Solution:**
```bash
# Find process using port
sudo lsof -i :2404

# Kill process
sudo kill -9 <PID>

# Or use different port in config
"port": 2405
```

#### 4. Memory Allocation Failures

**Symptom:**
```json
{"timestamp":"...","level":"ERROR","message":"Failed to allocate memory for M_SP_TB_1 IOA list"}
```

**Solution:**
- Check available memory: `free -h`
- Reduce number of configured IOAs
- Check for memory leaks: `valgrind ./iec104-server config.json`

#### 5. Compilation Errors

**Symptom:**
```
fatal error: iec60870_slave.h: No such file or directory
```

**Solution:**
```bash
# Rebuild lib60870
cd lib60870/lib60870-C
rm -rf build
mkdir build && cd build
cmake ..
make
```

### Debug Mode

Enable debug logging for detailed information:

```c
// In code
logger_set_level(LOG_LEVEL_DEBUG);
```

Or via environment:
```bash
LOG_LEVEL=DEBUG ./iec104-server config.json
```

### Getting Help

1. Check logs for error messages
2. Run tests to verify installation: `make test`
3. Check configuration syntax
4. Review documentation in `docs/`
5. Check GitHub issues (if applicable)

---

## Examples

### Example 1: Simple Server

**config.json:**
```json
{
  "asdu": 1,
  "port": 2404,
  "M_SP_TB_1_config": [100, 101, 102]
}
```

**Usage:**
```c
#include "src/data/data_manager.h"
#include "src/config/config_parser.h"

int main() {
    // Initialize
    logger_init(LOG_LEVEL_INFO);
    init_data_contexts();
    
    // Load config
    if (!init_config_from_file("config.json")) {
        LOG_ERROR("Failed to load configuration");
        return 1;
    }
    
    LOG_INFO("Server initialized successfully");
    
    // ... create IEC104 server ...
    
    // Cleanup
    cleanup_data_contexts();
    return 0;
}
```

### Example 2: Updating Data

```c
#include "src/data/data_manager.h"

void update_sensor_value(CS104_Slave slave, int ioa, bool value) {
    // Get context for single point with time
    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    
    // Create new value
    DataValue new_value = {
        .type = DATA_VALUE_TYPE_BOOL,
        .value.bool_val = value,
        .quality = IEC60870_QUALITY_GOOD,
        .has_quality = true,
        .has_timestamp = true
    };
    
    // Get current time
    CP56Time2a_createFromMsTimestamp(&new_value.timestamp, 
                                     Hal_getTimeInMs());
    
    // Update data
    if (update_data(ctx, slave, ioa, &new_value)) {
        LOG_INFO("Data updated for IOA %d", ioa);
        // Send spontaneous update to client
    }
}
```

### Example 3: Custom Interrogation Handler

```c
#include "src/protocol/interrogation.h"

// Set up server
CS104_Slave slave = CS104_Slave_create(10, 10);
CS104_Slave_setLocalPort(slave, 2404);

// Set interrogation handler
CS104_Slave_setInterrogationHandler(slave, 
                                    interrogationHandler, 
                                    NULL);

// Start server
CS104_Slave_start(slave);
LOG_INFO("Server started on port 2404");

// ... event loop ...

// Cleanup
CS104_Slave_stop(slave);
CS104_Slave_destroy(slave);
```

### Example 4: Configuration Validation

```c
#include "src/config/config_parser.h"

bool validate_config(const char* filename) {
    // Try to parse config
    if (!init_config_from_file(filename)) {
        LOG_ERROR("Configuration validation failed");
        return false;
    }
    
    // Check each data type
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        DataTypeContext* ctx = &g_data_contexts[i];
        if (ctx->config.count > 0) {
            LOG_INFO("Type %s: %d IOAs configured",
                    ctx->type_info->name,
                    ctx->config.count);
        }
    }
    
    LOG_INFO("Configuration valid");
    return true;
}
```

---

## Best Practices

### 1. Configuration Management

- Keep config files in version control
- Use meaningful IOA ranges (e.g., 100-199 for sensors)
- Document IOA assignments
- Validate config before deployment

### 2. Error Handling

- Always check return values
- Use appropriate log levels
- Clean up resources on error
- Provide meaningful error messages

### 3. Performance

- Use deadband for analog values
- Configure appropriate offline_udt_time
- Monitor memory usage
- Profile critical paths

### 4. Security

- Bind to specific IP if needed
- Use firewall rules
- Monitor connections
- Log security events

### 5. Monitoring

- Enable structured logging
- Use log aggregation tools
- Monitor error rates
- Set up alerts

---

## Appendix

### A. Supported Data Types

| TypeID | Name | Description |
|--------|------|-------------|
| 30 | M_SP_TB_1 | Single point with CP56Time2a |
| 31 | M_DP_TB_1 | Double point with CP56Time2a |
| 34 | M_ME_TD_1 | Normalized measured with CP56Time2a |
| 37 | M_IT_TB_1 | Integrated totals with CP56Time2a |
| 1 | M_SP_NA_1 | Single point |
| 3 | M_DP_NA_1 | Double point |
| 9 | M_ME_NA_1 | Normalized measured |
| 11 | M_ME_NB_1 | Scaled measured |
| 13 | M_ME_NC_1 | Short floating point |
| 21 | M_ME_ND_1 | Normalized without quality |

### B. Default Values

| Parameter | Default Value |
|-----------|---------------|
| offline_udt_time | 5000 ms |
| deadband_M_ME_NC_1_percent | 1.0% |
| asdu | 1 |
| command_mode | "direct" |
| port | 2404 |
| local_ip | "0.0.0.0" |

### C. Error Codes

See `docs/API.md` for complete error code reference.

---

**Last Updated:** 2025-11-20 19:03  
**Version:** 1.0  
**For Support:** See documentation in `docs/` directory
