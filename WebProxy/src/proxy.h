#ifndef PROXY_H
#define PROXY_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define BUFFER_SIZE 8192
#define MAX_CONNECTIONS 50
#define DEFAULT_HTTPS_PORT 443

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int allow_untrusted;
    char log_path[256];
} client_info;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int in_use;
    char host[256];  // Store hostname for checking against blocklist
} connection_entry;
extern int num_forbidden_sites;

extern connection_entry active_connections[MAX_CONNECTIONS];
/*Function Declaration*/
//myproxy.c
void start_proxy(int port, const char *forbidden_sites_path, const char *log_path, int allow_untrusted);
void close_forbidden_connections();
//connection.c
void *handle_client(void *client_socket);
int extract_host_and_path(const char *url, char *host, size_t host_len, char *path, size_t path_len);
void modify_request_headers(char *buffer, const char *host);
void forward_request(int server_fd, SSL *ssl, const char *buffer);
void handle_response(int client_fd, int server_fd, SSL *ssl);
//filtering.c
void load_forbidden_sites(const char *filename);
void sort_forbidden_sites();
int is_site_blocked(const char *host);
void reload_forbidden_sites(int signo);
//logging.c
void log_request(const char *log_path, const char *client_ip, const char *request_line, int status, int response_size);
#endif
