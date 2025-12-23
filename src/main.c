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

// Global Mutex for thread-safe logging
HANDLE hLogMutex;

typedef struct {
    char method[16];
    char host[256];
    int port;
} ParsedRequest;

// Helper: Get current timestamp
void get_timestamp(char *buf) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, 64, "%Y-%m-%d %H:%M:%S", t);
}

// Helper: Thread-safe file logging
void log_request(char *client_ip, char *url, int status_code) {
    // 1. Acquire Lock (Wait if another thread is writing)
    WaitForSingleObject(hLogMutex, INFINITE);

    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        char time_buf[64];
        get_timestamp(time_buf);
        fprintf(fp, "[%s] Client: %s | Request: %s | Status: %d\n", time_buf, client_ip, url, status_code);
        fclose(fp);
    }
    
    // 2. Release Lock
    ReleaseMutex(hLogMutex);
}

// Helper: Check blocked list
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

SOCKET connect_to_upstream(char *host, int port) {
    struct addrinfo hints, *res;
    SOCKET sock;
    char port_str[10];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     
    hints.ai_socktype = SOCK_STREAM; 

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return INVALID_SOCKET;

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) == SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    freeaddrinfo(res);
    return sock;
}

int parse_request(char *buffer, ParsedRequest *req) {
    char buf_copy[BUFFER_SIZE];
    strcpy(buf_copy, buffer); 

    char url[2048], protocol[16];
    if (sscanf(buf_copy, "%s %s %s", req->method, url, protocol) < 3) return -1;

    char *host_start = url;
    if (strstr(url, "http://") == url) host_start += 7;

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

DWORD WINAPI handle_client_thread(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)lpParam;
    
    // Get Client IP for logging
    struct sockaddr_in addr;
    int len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &len);
    char *client_ip = inet_ntoa(addr.sin_addr);

    char buffer[BUFFER_SIZE];
    ParsedRequest req;

    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        closesocket(client_socket);
        return 0;
    }
    
    if (parse_request(buffer, &req) < 0) {
        closesocket(client_socket);
        return 0;
    }
    
    // Check Filter
    if (is_blocked(req.host)) {
        printf("[Thread %lu] BLOCKED: %s\n", GetCurrentThreadId(), req.host);
        
        // Log the block
        log_request(client_ip, req.host, 403);

        char *forbidden_msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nAccess Denied.";
        send(client_socket, forbidden_msg, strlen(forbidden_msg), 0);
        closesocket(client_socket);
        return 0;
    }

    printf("[Thread %lu] ALLOWED: %s\n", GetCurrentThreadId(), req.host);
    
    // Log the success
    log_request(client_ip, req.host, 200);

    SOCKET server_socket = connect_to_upstream(req.host, req.port);
    if (server_socket == INVALID_SOCKET) {
        // Log error
        log_request(client_ip, req.host, 502);
        char *msg = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_socket, msg, strlen(msg), 0);
        closesocket(client_socket);
        return 0;
    }

    send(server_socket, buffer, bytes_read, 0);

    while (1) {
        int n = recv(server_socket, buffer, BUFFER_SIZE, 0);
        if (n <= 0) break; 
        send(client_socket, buffer, n, 0);
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

    // Initialize Mutex
    hLogMutex = CreateMutex(NULL, FALSE, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) return 1;
    listen(server_fd, 10);

    printf("Proxy Server Active on Port %d\n", PORT);

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