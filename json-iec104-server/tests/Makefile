# Makefile for Phase 1, 2, 3, 4 & 5 tests
CC = gcc
CFLAGS = -Wall -Wextra -g -I../lib60870/lib60870-C/src/inc/api -I../lib60870/lib60870-C/src/hal/inc -I../lib60870/lib60870-C/config
LDFLAGS = ../lib60870/lib60870-C/build/liblib60870.a -lpthread -lm

# Source files
DATA_TYPES_SRC = ../src/data/data_types.c
DATA_MANAGER_SRC = ../src/data/data_manager.c
CONFIG_PARSER_SRC = ../src/config/config_parser.c
INTERROGATION_SRC = ../src/protocol/interrogation.c
ERROR_CODES_SRC = ../src/utils/error_codes.c
LOGGER_SRC = ../src/utils/logger.c
CJSON_SRC = ../cJSON/cJSON.c

# Test source files
TEST_DATA_TYPES_SRC = test_data_types.c
TEST_DATA_MANAGER_SRC = test_data_manager.c
TEST_CONFIG_PARSER_SRC = test_config_parser.c
TEST_INTERROGATION_SRC = test_interrogation.c
TEST_UTILS_SRC = test_utils.c

# Test executables
TEST_DATA_TYPES = test_data_types
TEST_DATA_MANAGER = test_data_manager
TEST_CONFIG_PARSER = test_config_parser
TEST_INTERROGATION = test_interrogation
TEST_UTILS = test_utils

all: $(TEST_DATA_TYPES) $(TEST_DATA_MANAGER) $(TEST_CONFIG_PARSER) $(TEST_INTERROGATION) $(TEST_UTILS)

# Phase 1 test
$(TEST_DATA_TYPES): $(TEST_DATA_TYPES_SRC) $(DATA_TYPES_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 2 test (now uses logger)
$(TEST_DATA_MANAGER): $(TEST_DATA_MANAGER_SRC) $(DATA_MANAGER_SRC) $(DATA_TYPES_SRC) $(LOGGER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 3 test (now uses logger)
$(TEST_CONFIG_PARSER): $(TEST_CONFIG_PARSER_SRC) $(CONFIG_PARSER_SRC) $(DATA_MANAGER_SRC) $(DATA_TYPES_SRC) $(CJSON_SRC) $(LOGGER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 4 test (now uses logger)
$(TEST_INTERROGATION): $(TEST_INTERROGATION_SRC) $(INTERROGATION_SRC) $(DATA_MANAGER_SRC) $(DATA_TYPES_SRC) $(LOGGER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 5 test
$(TEST_UTILS): $(TEST_UTILS_SRC) $(ERROR_CODES_SRC) $(LOGGER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_DATA_TYPES) $(TEST_DATA_MANAGER) $(TEST_CONFIG_PARSER) $(TEST_INTERROGATION) $(TEST_UTILS)
	@echo "========================================"
	@echo "Running Phase 1 Tests (data_types)..."
	@echo "========================================"
	./$(TEST_DATA_TYPES)
	@echo ""
	@echo "========================================"
	@echo "Running Phase 2 Tests (data_manager)..."
	@echo "========================================"
	./$(TEST_DATA_MANAGER)
	@echo ""
	@echo "========================================"
	@echo "Running Phase 3 Tests (config_parser)..."
	@echo "========================================"
	./$(TEST_CONFIG_PARSER)
	@echo ""
	@echo "========================================"
	@echo "Running Phase 4 Tests (interrogation)..."
	@echo "========================================"
	./$(TEST_INTERROGATION)
	@echo ""
	@echo "========================================"
	@echo "Running Phase 5 Tests (utils)..."
	@echo "========================================"
	./$(TEST_UTILS)

test1: $(TEST_DATA_TYPES)
	@echo "========================================"
	@echo "Running Phase 1 Tests only..."
	@echo "========================================"
	./$(TEST_DATA_TYPES)

test2: $(TEST_DATA_MANAGER)
	@echo "========================================"
	@echo "Running Phase 2 Tests only..."
	@echo "========================================"
	./$(TEST_DATA_MANAGER)

test3: $(TEST_CONFIG_PARSER)
	@echo "========================================"
	@echo "Running Phase 3 Tests only..."
	@echo "========================================"
	./$(TEST_CONFIG_PARSER)

test4: $(TEST_INTERROGATION)
	@echo "========================================"
	@echo "Running Phase 4 Tests only..."
	@echo "========================================"
	./$(TEST_INTERROGATION)

test5: $(TEST_UTILS)
	@echo "========================================"
	@echo "Running Phase 5 Tests only..."
	@echo "========================================"
	./$(TEST_UTILS)

clean:
	rm -f $(TEST_DATA_TYPES) $(TEST_DATA_MANAGER) $(TEST_CONFIG_PARSER) $(TEST_INTERROGATION) $(TEST_UTILS)

.PHONY: all test test1 test2 test3 test4 test5 clean
