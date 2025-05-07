#include "proxy.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <string.h>
#define _GNU_SOURCE
#include <string.h>
#include <ctype.h>

char *strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;

    size_t needle_len = strlen(needle);
    while (*haystack) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}


// Helper functions
void send_error(int fd, int code, const char *msg) {
    const char *fmt = "HTTP/1.1 %d %s\r\n"
                      "Connection: close\r\n"
                      "Content-Length: 0\r\n\r\n";
    char buf[128];
    int len = snprintf(buf, sizeof(buf), fmt, code, msg);
    send(fd, buf, len, 0);
}

int extract_host(const char *url, char *host, size_t host_len) {
    const char *start = strstr(url, "://");
    start = start ? start + 3 : url;
    const char *end = strpbrk(start, ":/");
    
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (length >= host_len) return 0;
    
    strncpy(host, start, length);
    host[length] = '\0';
    
    // Extract port if specified
    char *port_ptr = strchr(host, ':');
    if (port_ptr) *port_ptr = '\0';
    
    return 1;
}

int connect_to_server(const char *host, int port, int *server_fd, SSL **ssl, SSL_CTX **ssl_ctx, int allow_untrusted) {
    printf("Connecting to %s:%d...\n", host, port);

    struct hostent *server = gethostbyname(host);
    if (!server) {
        perror("DNS resolution failed");
        return 0;
    }

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd < 0) {
        perror("Socket creation failed");
        return 0;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(*server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(*server_fd);
        return 0;
    }

    printf("Connected to %s:%d successfully!\n", host, port);

    if (port == 443) {  // Establish SSL for HTTPS
        *ssl_ctx = SSL_CTX_new(SSLv23_client_method());
        if (!*ssl_ctx) {
            perror("SSL_CTX_new failed");
            close(*server_fd);
            return 0;
        }

        if (!allow_untrusted) {
            SSL_CTX_set_verify(*ssl_ctx, SSL_VERIFY_PEER, NULL);

            // Get home directory for local CA cert path
            char ca_path[512];
            snprintf(ca_path, sizeof(ca_path), "%s/openssl/certs/ca-certificates.crt", getenv("HOME"));

            // Try user-installed CA certificates
            if (!SSL_CTX_load_verify_locations(*ssl_ctx, ca_path, NULL)) {
                perror("Failed to load user-installed CA certificates. Trying system CA...");

                if (!SSL_CTX_load_verify_locations(*ssl_ctx, "/etc/ssl/certs/ca-certificates.crt", NULL) &&
                    !SSL_CTX_load_verify_locations(*ssl_ctx, "/etc/pki/tls/certs/ca-bundle.crt", NULL)) {
                    perror("Failed to load system CA certificates.");
                    return 0;  // Abort if all fail
                }
            }
        } else {
            // Allow untrusted SSL (for debugging)
            SSL_CTX_set_verify(*ssl_ctx, SSL_VERIFY_NONE, NULL);
        }


        *ssl = SSL_new(*ssl_ctx);
        SSL_set_fd(*ssl, *server_fd);

        if (SSL_connect(*ssl) != 1) {
            printf("SSL handshake failed for %s:%d\n", host, port);
            ERR_print_errors_fp(stderr);
            
            SSL_free(*ssl);
            *ssl = NULL;  // Prevent dangling pointer
            SSL_CTX_free(*ssl_ctx);
            close(*server_fd);
            return 0;
        }

        printf("SSL handshake completed for %s:%d\n", host, port);
    }

    return 1;
}




void inject_header(char *buffer, size_t buf_size, const char *header) {
    char *headers_end = strstr(buffer, "\r\n\r\n");
    if (!headers_end) return;
    
    size_t existing_len = headers_end - buffer;
    size_t new_header_len = strlen(header);
    if (existing_len + new_header_len + 4 >= buf_size) return;

    memmove(buffer + new_header_len, buffer, existing_len + 4);
    memcpy(buffer, header, new_header_len);
}

void forward_response(int client_fd, int server_fd, SSL *ssl, int is_head_request) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes;
    int header_sent = 0;

    while (1) {
        if (ssl) {
            bytes = SSL_read(ssl, buffer, sizeof(buffer));
        } else {
            bytes = recv(server_fd, buffer, sizeof(buffer), 0);
        }

        if (bytes <= 0) break;  // Stop reading when connection closes

        // If this is a HEAD request, only send headers (stop after "\r\n\r\n")
        if (is_head_request) {
            if (!header_sent) {
                char *headers_end = strstr(buffer, "\r\n\r\n");
                if (headers_end) {
                    send(client_fd, buffer, headers_end - buffer + 4, 0);
                    header_sent = 1;
                }
            }
            break;  // Stop sending anything else
        } else {
            send(client_fd, buffer, bytes, 0);
        }
    }
}



void tunnel_data(int client_fd, int server_fd) {
    fd_set fds;
    char buffer[BUFFER_SIZE];
    ssize_t bytes;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);
        FD_SET(server_fd, &fds);

        if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(client_fd, &fds)) {
            bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            send(server_fd, buffer, bytes, 0);
        }

        if (FD_ISSET(server_fd, &fds)) {
            bytes = recv(server_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            send(client_fd, buffer, bytes, 0);
        }
    }

    close(client_fd);
    close(server_fd);
}
int extract_content_length(const char *buffer) {
    const char *cl_header = strcasestr(buffer, "Content-Length:");
    if (!cl_header) return -1; // No Content-Length header found

    int content_length;
    if (sscanf(cl_header, "Content-Length: %d", &content_length) == 1) {
        return content_length;
    }
    return -1; // Return -1 if parsing fails
}
void replace_proxy_connection(char *buffer) {
    char *proxy_conn = strcasestr(buffer, "Proxy-Connection:");
    if (proxy_conn) {
        memcpy(proxy_conn, "Connection:", 11);
        char *value = proxy_conn + 11;
        while (*value == ' ' || *value == '\t') value++;
        if (strncasecmp(value, "keep-alive", 10) == 0) {
            snprintf(proxy_conn, sizeof(buffer) - (proxy_conn - buffer), "Connection: keep-alive\r\n");
        } else {
            snprintf(proxy_conn, sizeof(buffer) - (proxy_conn - buffer), "Connection: close\r\n");
        }
    }
}
void ensure_host_header(char *buffer, const char *host, const char *client_ip) {
    char *headers_end = strstr(buffer, "\r\n\r\n");
    if (!headers_end) return;  // No headers found

    // Ensure "Host:" header is present
    if (!strcasestr(buffer, "Host:")) {
        char host_header[256];
        int len = snprintf(host_header, sizeof(host_header), "Host: %s\r\n", host);
        memmove(headers_end + len, headers_end, strlen(headers_end) + 1);
        memcpy(headers_end, host_header, len);
    }

    // Handle "X-Forwarded-For"
    if (!strcasestr(buffer, "X-Forwarded-For:")) {
        // Ensure it is inserted properly with a newline before it
        char xff_new[256];
        int xff_len = snprintf(xff_new, sizeof(xff_new), "\r\nX-Forwarded-For: %s\r\n", client_ip);
        
        // Move existing headers forward to make space
        memmove(headers_end + xff_len, headers_end, strlen(headers_end) + 1);
        memcpy(headers_end, xff_new, xff_len);
    }
}


/*handle_client implementation*/
void *handle_client(void *arg) {
    client_info *info = (client_info *)arg;
    int client_fd = info->client_fd;
    struct sockaddr_in client_addr = info->client_addr;
    char log_path[256];
    strncpy(log_path, info->log_path, sizeof(log_path));  // Copy log_path from struct
    log_path[sizeof(log_path) - 1] = '\0'; 
    free(info);

    char buffer[BUFFER_SIZE] = {0};
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    SSL_CTX *ssl_ctx = NULL;
    SSL *ssl = NULL;
    int server_fd = -1;
    int target_port = 443;
    char host[128] = {0};
    char method[16], url[256], version[16];
    //size_t received;
    //ssize_t rc;

    // Read client request
    ssize_t total_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (total_received <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[total_received] = '\0';

    // Parse request line
    if (sscanf(buffer, "%15s %255s %15s", method, url, version) != 3) {
        send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }
    
    if (strstr(version, "HTTP/1.0")) {
        strcpy(version, "HTTP/1.1");
    } else if (!strstr(version, "HTTP/1.")) {
        send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

    // Extract host
    if (!extract_host(url, host, sizeof(host))) {
        send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

    // Check if site is blocked
    if (is_site_blocked(host)) {
        printf("Blocking site: %s\n", host);
        send_error(client_fd, 403, "Forbidden");

        // Log the correct status
        log_request(log_path, client_ip, buffer, 403, 0);

        close(client_fd);
        return NULL;
    }



    // Store connection in active_connections[]
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!active_connections[i].in_use) {
            active_connections[i].client_fd = client_fd;
            active_connections[i].client_addr = client_addr;
            active_connections[i].in_use = 1;
            strncpy(active_connections[i].host, host, sizeof(active_connections[i].host) - 1);
            active_connections[i].host[sizeof(active_connections[i].host) - 1] = '\0';
            break;
        }
    }

    // Extract path (fix request formatting)
    char *path = strchr(url + 7, '/');
    if (!path) path = "/";

    // Connect to remote server
    if (!connect_to_server(host, target_port, &server_fd, &ssl, &ssl_ctx, 0)) {
        send_error(client_fd, 502, "Bad Gateway");
        close(client_fd);
        return NULL;
    }
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        send_error(client_fd, 501, "Not Implemented");
        close(client_fd);
        return NULL;
    }


    // Correct request formatting
    char new_request[BUFFER_SIZE];
    snprintf(new_request, sizeof(new_request),
             "%s %s %s\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: MyProxy/1.0\r\n\r\n",
             method, path, version, host);

    //printf("Forwarding request:\n%s\n", new_request);
    if (target_port == 443) {
        printf("Sending HTTPS request using SSL_write\n");
        SSL_write(ssl, new_request, strlen(new_request));
    } else {
        printf("Sending HTTP request using send()\n");
        send(server_fd, new_request, strlen(new_request), 0);
    }


    // Read response from server
    ensure_host_header(buffer, host, client_ip);
    int is_head_request = (strcmp(method, "HEAD") == 0);
    forward_response(client_fd, server_fd, ssl, is_head_request);
    log_request(log_path, client_ip, buffer, 200, strlen(buffer));
    ensure_host_header(buffer, host, client_ip);


    /*char resp_buffer[BUFFER_SIZE];
    if (target_port == 443) {
        received = SSL_read(ssl, resp_buffer, sizeof(resp_buffer) -1);
    } else {
        received = recv(server_fd, resp_buffer, sizeof(resp_buffer) -1, 0);
    }

    if (received > 0) {
        resp_buffer[received] = '\0';
        send(client_fd, resp_buffer, received, 0);
    }*/
    /*ssize_t received = recv(server_fd, resp_buffer, sizeof(resp_buffer) - 1, 0);
    if (received > 0) {
        resp_buffer[received] = '\0';
        if (!strstr(resp_buffer, "HTTP/1.")) {
            printf("Fixing response: Adding HTTP/1.1 header\n");
            char fixed_response[BUFFER_SIZE];
            snprintf(fixed_response, sizeof(fixed_response), "HTTP/1.1 200 OK\r\n%s", resp_buffer);
            send(client_fd, fixed_response, strlen(fixed_response), 0);
        } else {
            send(client_fd, resp_buffer, received, 0);
        }
    }*/

    // Cleanup
    if (ssl) SSL_free(ssl);
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    close(server_fd);
    close(client_fd);
    return NULL;
}


