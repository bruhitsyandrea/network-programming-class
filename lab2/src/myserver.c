#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_PACKET_SIZE 65535
//#define MAX_SEQ_NUM 100000

void start_server(int port) {
    int sock;
    struct sockaddr_in server_addr, client_addr;
    char buffer[MAX_PACKET_SIZE];
    socklen_t client_addr_len = sizeof(client_addr);

    // Create socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to port
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 30;  // 30-second timeout
    timeout.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Failed to set socket timeout");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", port);

    int received_eof = 0;  // Track if EOF marker is received

    while (1) {
        int received = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Receive timed out. No packets received in the last 30 seconds.\n");
                break;  // Exit gracefully on timeout
            }
            perror("Failed to receive packet");
            continue;
        }

        // Extract sequence number
        int seq_num;
        memcpy(&seq_num, buffer, sizeof(int));

        if (seq_num == -1) {
            printf("Received end-of-file marker from client.\n");
            received_eof = 1;  // Mark EOF as received
            break;
        }

        // Log received packet
        printf("Server received packet with seq_num: %d, size: %d bytes, data: %.*s\n",
               seq_num,
               received,
               (int)(received - sizeof(int)),
               buffer + sizeof(int));

        // Echo packet back to client
        int sent = sendto(sock, buffer, received, 0, (struct sockaddr *)&client_addr, client_addr_len);
        if (sent < 0) {
            perror("Failed to send packet");
        } else {
            printf("Echoed packet with seq_num: %d, size: %d bytes, data: %.*s\n",
                   seq_num,
                   sent,
                   (int)(sent - sizeof(int)),
                   buffer + sizeof(int));
        }
    }

    // Send EOF marker back to the client if received
    if (received_eof) {
        int eof_marker = -1;
        sendto(sock, &eof_marker, sizeof(int), 0, (struct sockaddr *)&client_addr, client_addr_len);
        printf("Sent end-of-file marker to client.\n");
    }

    close(sock);
}



int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int port = atoi(argv[1]);
	if (port <= 0 || port > 65535) {
		fprintf(stderr, "Invalid port number. Must be between 1 and 65535.\n");
		exit(EXIT_FAILURE);
	}

	start_server(port);
	return 0;
}
