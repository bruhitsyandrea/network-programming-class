#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>

void create_log_directory(const char *log_path) {
    char dir_path[256];
    strncpy(dir_path, log_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0'; // Keep only the directory path

        // Create directory if it doesn't exist
        if (mkdir(dir_path, 0755) && errno != EEXIST) {
            fprintf(stderr, "Failed to create log directory: %s\n", strerror(errno));
        }
    }
}


// Open log file safely
FILE *open_log_file(const char *log_path) {
    create_log_directory(log_path);

    FILE *log_file = fopen(log_path, "a");  // Use "a" mode to append to the file
    if (!log_file) {
        perror("Failed to open log file");
        return NULL; 
    }
    return log_file;
}

void log_request(const char *log_path, const char *client_ip, const char *request_line, int status, int response_size) {
    FILE *log_file = open_log_file(log_path);
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000Z", tm_info);

    // DEBUG: Print logged status to verify
    printf("[LOG] %s %s \"%s\" %d %d\n", time_str, client_ip, request_line, status, response_size);

    fprintf(log_file, "%s %s \"%s\" %d %d\n", time_str, client_ip, request_line, status, response_size);
    fclose(log_file);
}



