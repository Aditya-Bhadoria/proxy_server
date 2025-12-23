#include <stdio.h>
#include <time.h>
#include "../include/proxy.h"

void get_timestamp(char *buf) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, 64, "%Y-%m-%d %H:%M:%S", t);
}

void log_request(char *client_ip, char *url, int status_code) {
    WaitForSingleObject(hLogMutex, INFINITE);
    
    FILE *fp = fopen(server_config.log_path, "a");
    if (fp) {
        char time_buf[64];
        get_timestamp(time_buf);
        fprintf(fp, "[%s] Client: %s | Request: %s | Status: %d\n", time_buf, client_ip, url, status_code);
        fclose(fp);
    }
    
    ReleaseMutex(hLogMutex);
}