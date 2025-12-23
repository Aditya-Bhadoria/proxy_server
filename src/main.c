#include <stdio.h>
#include <winsock2.h>
#include "../include/proxy.h"

#pragma comment(lib, "Ws2_32.lib")

HANDLE hLogMutex;
ServerConfig server_config;

int main() {
    load_config("../config/server.conf");

    WSADATA wsaData;
    SOCKET server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    CreateDirectory("logs", NULL); 
    hLogMutex = CreateMutex(NULL, FALSE, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    
    address.sin_port = htons(server_config.port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed on port %d. Error: %d\n", server_config.port, WSAGetLastError());
        return 1;
    }
    listen(server_fd, 10);

    printf("Modular Proxy Server Active on Port %d\n", server_config.port);
    printf("Logging to: %s\n", server_config.log_path);
    printf("Blocked List: %s\n", server_config.blocked_file);

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