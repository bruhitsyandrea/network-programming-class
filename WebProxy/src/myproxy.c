#include "proxy.h"
#include <stdio.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>


volatile atomic_int running = 1; // Global flag to stop proxy on SIGINT
int server_fd;

void handle_sigint(int signo) {
    printf("\nSIGINT received: Reloading forbidden sites list...\n");
    load_forbidden_sites("forbidden_sites.txt");
    printf("Blocklist updated. New count: %d sites\n", num_forbidden_sites);
}


void close_forbidden_connections() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (active_connections[i].in_use && is_site_blocked(active_connections[i].host)) {
            printf("Closing connection to forbidden site: %s\n", active_connections[i].host);
            close(active_connections[i].client_fd);
            active_connections[i].in_use = 0;  // Mark connection as closed
        }
    }
}


connection_entry active_connections[MAX_CONNECTIONS] = {0};  // Initialize to zero


void start_proxy(int port, const char *forbidden_sites_path, const char *log_path, int allow_untrusted) {
    load_forbidden_sites(forbidden_sites_path);
    signal(SIGPIPE, SIG_IGN);

    //Reload forbidden sites on Ctrl+C
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);



    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    printf("Proxy server running on port %d...\n", port);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("Accept failed");
            continue;
        }

        // Allocate memory for client info struct
        client_info *info = malloc(sizeof(client_info));
        if (!info) {
            perror("Memory allocation failed");
            close(client_fd);
            continue;
        }

        // Populate the struct and create a thread
        info->client_fd = client_fd;
        info->client_addr = client_addr;
        info->allow_untrusted = allow_untrusted;
        strncpy(info->log_path, log_path, sizeof(info->log_path));
        info->log_path[sizeof(info->log_path) - 1] = '\0';  // Ensure null termination


        

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, info) != 0) {
            perror("Thread creation failed");
            free(info);
            close(client_fd);
            continue;
        }

        pthread_detach(thread);
    }

    printf("Shutting down proxy...\n");
    close(server_fd);
}



int main(int argc, char *argv[]) {
    /*if (argc != 7) {
        fprintf(stderr, "Usage: %s -p <port> -a <forbidden_sites_file> -l <log_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
*/
    int port = -1;
    char *forbidden_sites_path = NULL;
    char *log_path = NULL;

    int allow_untrusted = 0;

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();


    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-a") == 0) forbidden_sites_path = argv[++i];
        else if (strcmp(argv[i], "-l") == 0) log_path = argv[++i];
        else if (strcmp(argv[i], "-untrusted") == 0) allow_untrusted = 1;
    }

    if (argc < 7 || (argc != 7 + allow_untrusted)) {
        fprintf(stderr, "Usage: %s -p <port> -a <forbidden_file> -l <log_file> [-untrusted]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    start_proxy(port, forbidden_sites_path, log_path, allow_untrusted);
    return 0;
}
