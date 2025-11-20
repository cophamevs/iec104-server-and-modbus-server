#!/bin/bash

# Test client manager functionality

echo "=== Testing Client Manager ==="

# Start server in background
./json-iec104-server-new > server_output.txt 2>&1 &
SERVER_PID=$!
echo "Started server with PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Test 1: No clients connected
echo ""
echo "Test 1: No clients connected"
echo '{"cmd":"get_connected_clients"}' | nc -q 0 localhost 2404 || true
sleep 0.5

# Test 2: Connect a client
echo ""
echo "Test 2: Connect a client and query connected clients"
# Start client in background
./json-iec104-client localhost 2404 > client_output.txt 2>&1 &
CLIENT_PID=$!
echo "Started client with PID: $CLIENT_PID"

# Wait for client to connect
sleep 2

# Query connected clients via stdin to server
echo '{"cmd":"get_connected_clients"}' > /tmp/test_cmd.json

# Kill client
kill $CLIENT_PID 2>/dev/null || true

# Wait a bit
sleep 1

# Test 3: After client disconnects
echo ""
echo "Test 3: After client disconnects"
echo '{"cmd":"get_connected_clients"}' > /tmp/test_cmd2.json

# Stop server
echo ""
echo "Stopping server..."
echo '{"cmd":"stop"}' > /tmp/test_stop.json

sleep 1
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Test Complete ==="
echo "Check server_output.txt for detailed logs"
