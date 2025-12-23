#ifndef PROXY_H
#define PROXY_H

#include <winsock2.h>
#include <windows.h>

#define PORT 8888
#define BUFFER_SIZE 4096

#define BLOCKED_FILE "../config/blocked.txt" 
#define LOG_FILE "logs/proxy.log"

typedef struct {
    char method[16];
    char host[256];
    int port;
} ParsedRequest;

void log_request(char *client_ip, char *url, int status_code);
int parse_request(char *buffer, ParsedRequest *req);
int is_blocked(char *host);
SOCKET connect_to_upstream(char *host, int port);
void handle_https_tunnel(SOCKET client_socket, SOCKET server_socket);
DWORD WINAPI handle_client_thread(LPVOID lpParam);

#endif