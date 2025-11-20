#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_PORT 2404
#define SERVER_IP "127.0.0.1"

// Simple function to connect to server and hold connection
void* client_connection(void* arg) {
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation failed\n");
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed\n");
        close(sock);
        return NULL;
    }

    printf("Test client connected to server\n");

    // Hold connection for 3 seconds
    sleep(3);

    close(sock);
    printf("Test client disconnected\n");

    return NULL;
}

int main() {
    printf("=== IEC104 Client Manager Test ===\n\n");

    printf("Instructions:\n");
    printf("1. Start the IEC104 server: ./json-iec104-server-new\n");
    printf("2. In another terminal, send: echo '{\"cmd\":\"get_connected_clients\"}' to stdin\n");
    printf("3. Run this test program to simulate client connections\n");
    printf("4. Query again to see the connected client\n\n");

    printf("Starting test client connection...\n");

    pthread_t thread;
    pthread_create(&thread, NULL, client_connection, NULL);
    pthread_join(thread, NULL);

    printf("\nTest complete.\n");
    printf("The server should have logged:\n");
    printf("  - Client connected: 127.0.0.1:xxxxx\n");
    printf("  - Client disconnected: 127.0.0.1:xxxxx\n");

    return 0;
}
