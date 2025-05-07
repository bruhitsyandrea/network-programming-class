#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_PACKET_SIZE 65535

int create_socket() {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("Socket creation failed");
		exit(1);
	}
	return sock;
}

void connect_to_server(int sock, struct sockaddr_in *server_addr, const char *ip, int port) {
	memset(server_addr, 0, sizeof(*server_addr));
	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &server_addr->sin_addr) <= 0) {
		perror("Invalid IP address");
		exit(EXIT_FAILURE);
	}

	struct timeval timeout;
	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
			perror("Failed to set socket timeout");
			//close(sock);
			exit(EXIT_FAILURE);
		}
}

void construct_packet(char *packet, int seq_num, const char *data, size_t data_len) {
    if (data_len > MAX_PACKET_SIZE - sizeof(int)) {
        fprintf(stderr, "Data size exceeds packet capacity.\n");
        exit(EXIT_FAILURE);
    }
    memcpy(packet, &seq_num, sizeof(int));
    memcpy(packet + sizeof(int), data, data_len);
}


void send_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, FILE *file, int mtu) {
    char packet[mtu];
    char data[mtu - sizeof(int)];
    int seq_num = 0;
    size_t bytes_read;  // Track the actual number of bytes read from the file

    while ((bytes_read = fread(data, 1, sizeof(data), file)) > 0) {
        construct_packet(packet, seq_num++, data, bytes_read);  // Use bytes_read for packet construction
        printf("Sending packet with seq_num: %d, size: %zu bytes, data: %.*s\n",
               seq_num - 1,
               bytes_read + sizeof(int),  // Log total size (seq_num + data)
               (int)bytes_read,
               data);

        if (sendto(sock, packet, bytes_read + sizeof(int), 0, (struct sockaddr *)server_addr, addr_len) < 0) {
            perror("Failed to send packet");
            exit(EXIT_FAILURE);
        }
    }

    // Send EOF marker
    int eof_marker = -1;
    sendto(sock, &eof_marker, sizeof(int), 0, (struct sockaddr *)server_addr, addr_len);
    printf("Sent end-of-file marker.\n");
}


void receive_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, FILE *output, int mtu) {
    char packet[mtu];
    char data[mtu - sizeof(int)];
    int seq_num;
    struct sockaddr_in recv_addr;
    socklen_t recv_addr_len = sizeof(recv_addr);
    int received_seq[100000] = {0};
    int total_packets = 0;

    while (1) {
        printf("Waiting to receive packet...\n");
        int received = recvfrom(sock, packet, sizeof(packet), 0, (struct sockaddr *)&recv_addr, &recv_addr_len);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Timeout reached while waiting for packets.\n");
                break;
            }
            perror("Failed to receive packet");
            exit(EXIT_FAILURE);
        }

        memcpy(&seq_num, packet, sizeof(int));  // Extract sequence number

        if (seq_num == -1) {
            printf("End of file received. Stopping packet reception.\n");
            break;
        }

        printf("Received packet with seq_num: %d, size: %d bytes, data: %.*s\n",
               seq_num, received, (int)(received - sizeof(int)), packet + sizeof(int));

        received_seq[seq_num] = 1;
        total_packets++;

        // Calculate the actual data size
        int data_size = received - sizeof(int);
        if (data_size > 0) {
            memcpy(data, packet + sizeof(int), data_size);  // Copy only the valid data
            long offset = (long)seq_num * (mtu - sizeof(int));
            fseek(output, offset, SEEK_SET);  // Seek to the correct offset
            fwrite(data, 1, data_size, output);  // Write valid data to the file
            fflush(output);  // Flush the buffer to ensure data is written to disk

            printf("Reconstructed data for seq_num %d at offset %ld: %.*s\n",
                   seq_num, offset, data_size, packet + sizeof(int));
        }
    }

    // Validate if all packets were received
    for (int i = 0; i < total_packets; i++) {
        if (received_seq[i] == 0) {
            fprintf(stderr, "Packet loss detected: Missing seq_num %d\n", i);
            exit(EXIT_FAILURE);
        }
    }
    printf("File successfully reconstructed.\n");
}



void parse_input(int argc, char *argv[], char *ip, int *port, int *mtu, char *in_file, char *out_file) {
	printf("argc: %d\n", argc);
	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	if (argc != 6) {
		fprintf(stderr, "Usage: %s <server_ip> <server_port> <mtu> <in_file> <out_file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	strncpy(ip, argv[1], 16);
	ip[15] = '\0';

	*port = atoi(argv[2]);
	if (*port <= 0 || *port > 65535) {
		fprintf(stderr, "Invalid port number. Must be between 1 and 65535.\n");
		exit(EXIT_FAILURE);
	}

	*mtu = atoi(argv[3]);
	if (*mtu <= sizeof(int)) {
		fprintf(stderr, "Invalid MTU size. Must be greater than %lu.\n", sizeof(int));
		exit(EXIT_FAILURE);
	}

	strncpy(in_file, argv[4], 255);
	in_file[254] = '\0';
	strncpy(out_file, argv[5], 255);
	out_file[254] = '\0';

}

void create_output_path(const char *path) {
	char *dir = strdup(path);
	char *parent_dir = dirname(dir);

	struct stat st = {0};
	if (stat(parent_dir, &st) == -1) {
		if (mkdir(parent_dir, 0700) != 0) {
			perror("Failed to create output directory");
			free(dir);
			exit(EXIT_FAILURE);
		}
	}
	free(dir);
}

int main(int argc, char *argv[]) {
	char ip[16];
	int port;
	int mtu;
	char in_file[256];
	char out_file[256];

	parse_input(argc, argv, ip, &port, &mtu, in_file, out_file);

	int sock = create_socket();
	struct sockaddr_in server_addr;
	connect_to_server(sock, &server_addr, ip, port);

	FILE *input_file = fopen(in_file, "rb");
	if (!input_file) {
		perror("Failed to open input file");
		exit(EXIT_FAILURE);
	}

	FILE *output_file = fopen(out_file, "wb");
	if (!output_file) {
		perror("Failed to open output file");
		fclose(input_file);
		exit(EXIT_FAILURE);
	}

	printf("Files opened successfully.\n");

	send_file(sock, &server_addr, sizeof(server_addr), input_file, mtu);
	receive_file(sock, &server_addr, sizeof(server_addr), output_file, mtu);

	fclose(input_file);
	fclose(output_file);
	close(sock);

	return 0;
}







