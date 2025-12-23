#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096

// Struct to hold the parsed request details
typedef struct {
    char method[16];
    char host[256];
    int port;
} ParsedRequest;

// Helper: Parse the raw HTTP buffer
int parse_request(char *buffer, ParsedRequest *req) {
    char url[2048], protocol[16];
    
    // 1. Read the Request Line (e.g., "GET http://google.com:80/ HTTP/1.1")
    // If this fails, it might be an empty request or malformed
    if (sscanf(buffer, "%s %s %s", req->method, url, protocol) < 3) {
        return -1; 
    }

    // 2. Parse URL to find Host and Port
    // Formats to handle:
    // A. http://example.com/path
    // B. http://example.com:8080/path
    // C. example.com:443 (CONNECT method, mostly HTTPS)

    char *host_start = url;
    
    // Skip "http://" if present
    if (strstr(url, "http://") == url) {
        host_start += 7;
    }

    // Find the end of the hostname (first slash or null terminator)
    char *path_start = strchr(host_start, '/');
    if (path_start) {
        *path_start = '\0'; // Temporarily terminate string to isolate host
    }

    // Check for specific port (e.g., example.com:8080)
    char *port_ptr = strchr(host_start, ':');
    if (port_ptr) {
        *port_ptr = '\0'; // Split host and port
        req->port = atoi(port_ptr + 1);
        strncpy(req->host, host_start, 255);
    } else {
        // Default ports
        if (strcmp(req->method, "CONNECT") == 0) 
            req->port = 443;
        else 
            req->port = 80;
            
        strncpy(req->host, host_start, 255);
    }

    return 0; // Success
}

int main() {
    WSADATA wsaData;
    SOCKET server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup Failed.\n");
        return 1;
    }

    // Create Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        return 1;
    }

    // Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed. Code: %d\n", WSAGetLastError());
        return 1;
    }

    // Listen
    listen(server_fd, 3);
    printf("Proxy Server listening on port %d...\n", PORT);

    while (1) {
        printf("\nWaiting for connection...\n");
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        
        if (new_socket == INVALID_SOCKET) continue;

        int valread = recv(new_socket, buffer, BUFFER_SIZE - 1, 0);
        if (valread > 0) {
            buffer[valread] = '\0';
            
            // --- PARSING PHASE ---
            ParsedRequest req;
            if (parse_request(buffer, &req) == 0) {
                printf("Parsed: Method=%s, Host=%s, Port=%d\n", req.method, req.host, req.port);
            } else {
                printf("Failed to parse request.\n");
            }
        }

        closesocket(new_socket);
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}