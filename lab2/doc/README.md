# README
**Name**: Yi-Wen Chen
**Student ID**: 1886079

## File in Submission

1. **src/myclient.c**:
It contains functions of the program

2. **src/myserver.c**:
It contains functions of the program

3. **Makefile**:
It create the program and save it into bin

4. **bin/**:
The bin is initially empty, and after ```make```, save myweb to bin. The command ```make clean``` erase the exutable created. 

4. **doc/README.md**:
The documentation include the 5 testcases that I came up to test the functionality of myweb

## Test Cases
For this lab I struggled to sent to file back to my local so I came up with these few files for testing. <br>

check the result by running
diff diri/<testfilename> diro/<outfilename>

1. diri/testing
echo "This is a test file." > diri/testfile

The outfile should also be "This is a test file."
Check by using"diff diri/test_testfile diro outfil

2. add disired bytes to test the function
dd if=/dev/zero bs=1 count=<bytes-t0-add> >> diri/<filename>


bytes I have tested and results:

bytes | result
------|--------
 0    | success
 100  | success
 200  | success
 300  | success
 400  | success
 500  | success
 700  | success
 1000 | success
 5000 | sucesss

example: 
I changed the input to 700 bytes <br> ```
dd if=/dev/zero bs=1 count=700 of=diri/small_testfile
700+0 records in
700+0 records out
700 bytes transferred in 0.004093 secs (171024 bytes/sec)```

and then I run the server and client:
```
./myserver 9090 &                                    
./myclient 127.0.0.1 9090 512 diri/small_testfile diro/small_outfile
diff diri/small_testfile diro/small_outfile
[1] 81277
argc: 6
argv[0]: ./myclient
argv[1]: 127.0.0.1
argv[2]: 9090
argv[3]: 512
argv[4]: diri/small_testfile
argv[5]: diro/small_outfile
Server is listening on port 9090
Files opened successfully.
Sending packet with seq_num: 0, size: 512 bytes, data: 
Sending packet with seq_num: 1, size: 196 bytes, data: 
Sent end-of-file marker.
Waiting to receive packet...
Server received packet with seq_num: 0, size: 512 bytes, data: 
Echoed packet with seq_num: 0, size: 512 bytes, data: 
Server received packet with seq_num: 1, size: 196 bytes, data: 
Echoed packet with seq_num: 1, size: 196 bytes, data: 
Received end-of-file marker from client.
Sent end-of-file marker to client.
Received packet with seq_num: 0, size: 512 bytes, data: 
Reconstructed data for seq_num 0 at offset 0: 
Waiting to receive packet...
Received packet with seq_num: 1, size: 196 bytes, data: 
Reconstructed data for seq_num 1 at offset 508: 
Waiting to receive packet...
End of file received. Stopping packet reception.
File successfully reconstructed.
[1]  + done       ./myserver 9090
```
3. I also tried big files and it failed after 20000 bytes it crashes. For the past days I have been suffering to make it work by editing my send_file() function and receive_file() function in my client.c. However, it always seems sort or close yet pretty far from close. <br>

The command that I used to create the large_testfile is:
```dd if=/dev/zero bs=1M count=10 of=diri/large_testfile```
It result in
```
diff diri/large_testfile diro/large_outfile 
Binary files diri/large_testfile and diro/large_outfile differ
```
4. As a result, I wasn't able to come up with five testcases since it got stock half way.

## Function implementation
**myserver.c**
In this file, I created two functions <br>
```void start_server(int port)``` and ```int main(int argc, char *argv[])```. 

**myclient.c**
1. ```int create_socket()```: This function create the socket using the socket() function.
2. ```void connect_to_server(int sock, struct sockaddr_in *server_addr, const char *ip, int port)```: This function connect to the server by initializing the server's address and then configure the socket with a timeout.
3. ```void construct_packet(char *packet, int seq_num, const char *data, size_t data_len) ```: This function creates the packet by combining the sequecne number and the data into single buffer for trnasmission.
4. ```void send_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, FILE *file, int mtu)```: The function sends a file over UPD socket by diving it into smaller packets, and then add the sequence number to each packet, and then transmitting them by sequence.
5. ```void receive_file(int sock, struct sockaddr_in *server_addr, socklen_t addr_len, FILE *output, int mtu)```: It receives the packets over a UDP socket, reconstructs the original file from these packets, and the write the datea to the output file.
6. ```void parse_input(int argc, char *argv[], char *ip, int *port, int *mtu, char *in_file, char *out_file)```: The function parses and validates the command line arguments, in order to assign values to variables used in main.
7. ```void create_output_path(const char *path)```: This function creates the path in case the output path is none existant.
8. ```int main(int argc, char *argv[])```: Run the program with the functions implemented above.


