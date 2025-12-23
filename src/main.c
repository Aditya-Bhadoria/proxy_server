#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096
#define BLOCKED_FILE "blocked.txt"
#define LOG_FILE "logs/proxy.log"

HANDLE hLogMutex;

typedef struct {
    char method[16];
    char host[256];
    int port;
} ParsedRequest;

// --- LOGGING HELPERS ---
void get_timestamp(char *buf) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, 64, "%Y-%m-%d %H:%M:%S", t);
}

void log_request(char *client_ip, char *url, int status_code) {
    WaitForSingleObject(hLogMutex, INFINITE);
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        char time_buf[64];
        get_timestamp(time_buf);
        fprintf(fp, "[%s] Client: %s | Request: %s | Status: %d\n", time_buf, client_ip, url, status_code);
        fclose(fp);
    }
    ReleaseMutex(hLogMutex);
}

int is_blocked(char *host) {
    FILE *file = fopen(BLOCKED_FILE, "r");
    if (!file) return 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strstr(host, line) != NULL) {
            fclose(file);
            return 1; 
        }
    }
    fclose(file);
    return 0;
}

// --- NETWORKING HELPERS ---

SOCKET connect_to_upstream(char *host, int port) {
    struct addrinfo hints, *res, *ptr;
    SOCKET sock;
    char port_str[10];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // Force IPv4 (More stable)
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        printf("DNS Resolution failed for %s\n", host);
        return INVALID_SOCKET;
    }

    // Attempt to connect to each address returned by DNS until one works
    for (ptr = res; ptr != NULL; ptr = ptr->ai_next) {
        
        // Create a SOCKET for connecting to server
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue; // Try next address
        }

        // Connect to server.
        if (connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock); // Failed, close and try next
            sock = INVALID_SOCKET;
            continue;
        }

        break; // Success! We are connected.
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCKET) {
        printf("Failed to connect to upstream %s. Error: %d\n", host, WSAGetLastError());
    }

    return sock;
}

int parse_request(char *buffer, ParsedRequest *req) {
    char buf_copy[BUFFER_SIZE];
    strcpy(buf_copy, buffer); 

    char url[2048], protocol[16];
    // Attempt to parse line: "METHOD URL PROTOCOL"
    if (sscanf(buf_copy, "%s %s %s", req->method, url, protocol) < 3) return -1;

    char *host_start = url;
    // For normal requests: http://example.com/path
    if (strstr(url, "http://") == url) host_start += 7;
    // For CONNECT requests: example.com:443 (no protocol prefix)
    
    char *path_start = strchr(host_start, '/');
    if (path_start) *path_start = '\0';

    char *port_ptr = strchr(host_start, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        req->port = atoi(port_ptr + 1);
        strncpy(req->host, host_start, 255);
    } else {
        req->port = (strcmp(req->method, "CONNECT") == 0) ? 443 : 80;
        strncpy(req->host, host_start, 255);
    }
    return 0;
}

// --- HTTPS TUNNELING ---
// This function shovels data between client and server blindly
void handle_https_tunnel(SOCKET client_socket, SOCKET server_socket) {
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    int max_sd, activity;

    // 1. Send "200 Connection Established" to client
    // This tells the browser: "The tunnel is open, start sending encrypted data."
    char *success_msg = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, success_msg, strlen(success_msg), 0);

    // 2. Enter Tunnel Loop
    while (1) {
        // Reset the file descriptor set
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        FD_SET(server_socket, &readfds);

        // Select needs to know the range of sockets (ignored in Windows, but good practice)
        max_sd = (client_socket > server_socket) ? client_socket : server_socket;

        // Wait indefinitely for data on EITHER socket
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) break; // Error

        // A. Data from CLIENT -> Send to SERVER
        if (FD_ISSET(client_socket, &readfds)) {
            int valread = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; // Connection closed
            send(server_socket, buffer, valread, 0);
        }

        // B. Data from SERVER -> Send to CLIENT
        if (FD_ISSET(server_socket, &readfds)) {
            int valread = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; // Connection closed
            send(client_socket, buffer, valread, 0);
        }
    }
}

// --- THREAD WORKER ---
DWORD WINAPI handle_client_thread(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)lpParam;
    
    struct sockaddr_in addr;
    int len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &len);
    char *client_ip = inet_ntoa(addr.sin_addr);

    char buffer[BUFFER_SIZE];
    ParsedRequest req;

    // Peek at the request without removing it from the buffer yet? 
    // Actually, for simplicity, we read it. For HTTPS, the first packet is the CONNECT header.
    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        closesocket(client_socket);
        return 0;
    }
    
    if (parse_request(buffer, &req) < 0) {
        closesocket(client_socket);
        return 0;
    }
    
    if (is_blocked(req.host)) {
        printf("[Thread %lu] BLOCKED: %s\n", GetCurrentThreadId(), req.host);
        log_request(client_ip, req.host, 403);
        char *forbidden_msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nAccess Denied.";
        send(client_socket, forbidden_msg, strlen(forbidden_msg), 0);
        closesocket(client_socket);
        return 0;
    }

    printf("[Thread %lu] %s request to: %s:%d\n", GetCurrentThreadId(), req.method, req.host, req.port);
    log_request(client_ip, req.host, 200);

    SOCKET server_socket = connect_to_upstream(req.host, req.port);
    if (server_socket == INVALID_SOCKET) {
        log_request(client_ip, req.host, 502);
        char *msg = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_socket, msg, strlen(msg), 0);
        closesocket(client_socket);
        return 0;
    }

    // --- CHECK FOR HTTPS (CONNECT) ---
    if (strcmp(req.method, "CONNECT") == 0) {
        // Hand off to the tunnel handler
        handle_https_tunnel(client_socket, server_socket);
    } 
    else {
        // Standard HTTP Forwarding
        // 1. Send the original request we already read
        send(server_socket, buffer, bytes_read, 0);
        
        // 2. Loop remaining data
        while (1) {
            int n = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (n <= 0) break; 
            send(client_socket, buffer, n, 0);
        }
    }

    closesocket(server_socket);
    closesocket(client_socket);
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    CreateDirectory("logs", NULL); // Auto-create logs folder
    hLogMutex = CreateMutex(NULL, FALSE, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) return 1;
    listen(server_fd, 10);

    printf("HTTPS Proxy Server Active on Port %d\n", PORT);

    while (1) {
        client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket != INVALID_SOCKET) {
            HANDLE hThread = CreateThread(NULL, 0, handle_client_thread, (LPVOID)client_socket, 0, NULL);
            if (hThread) CloseHandle(hThread);
        }
    }

    CloseHandle(hLogMutex);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}