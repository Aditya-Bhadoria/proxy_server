#include <stdio.h>
#include <ws2tcpip.h>
#include "../include/proxy.h"

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
    struct addrinfo hints, *res, *ptr;
    SOCKET sock;
    char port_str[10];
    sprintf(port_str, "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return INVALID_SOCKET;

    for (ptr = res; ptr != NULL; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    return sock;
}

void handle_https_tunnel(SOCKET client_socket, SOCKET server_socket) {
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    int max_sd, activity;
    char *success_msg = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_socket, success_msg, strlen(success_msg), 0);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client_socket, &readfds);
        FD_SET(server_socket, &readfds);
        max_sd = (client_socket > server_socket) ? client_socket : server_socket;

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) break; 

        if (FD_ISSET(client_socket, &readfds)) {
            int valread = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; 
            send(server_socket, buffer, valread, 0);
        }
        if (FD_ISSET(server_socket, &readfds)) {
            int valread = recv(server_socket, buffer, BUFFER_SIZE, 0);
            if (valread <= 0) break; 
            send(client_socket, buffer, valread, 0);
        }
    }
}

DWORD WINAPI handle_client_thread(LPVOID lpParam) {
    SOCKET client_socket = (SOCKET)lpParam;
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
    
    if (is_blocked(req.host)) {
        log_request(client_ip, req.host, 403);
        char *forbidden_msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nAccess Denied.";
        send(client_socket, forbidden_msg, strlen(forbidden_msg), 0);
        closesocket(client_socket);
        return 0;
    }

    printf("[Thread %lu] %s request to: %s\n", GetCurrentThreadId(), req.method, req.host);
    log_request(client_ip, req.host, 200);

    SOCKET server_socket = connect_to_upstream(req.host, req.port);
    if (server_socket == INVALID_SOCKET) {
        log_request(client_ip, req.host, 502);
        closesocket(client_socket);
        return 0;
    }

    if (strcmp(req.method, "CONNECT") == 0) {
        handle_https_tunnel(client_socket, server_socket);
    } else {
        send(server_socket, buffer, bytes_read, 0);
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