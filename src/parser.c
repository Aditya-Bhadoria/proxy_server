#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/proxy.h" 

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