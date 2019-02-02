#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#define BUFFER_SIZE 1024                       //maximum size of data in one socket

int main(int argc, char* argv[])
{
	int res, port, state;
	int client_socket;
	struct sockaddr_in server_addr;
	char buf[BUFFER_SIZE+1];
	char p[] = "-p";
	char h[] = "-h";
	char ready[] = "ready";                    //server's answer
	char *host;

	if (argc != 5) {                           //invalid arguments
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	if (strcmp(argv[1], p) && strcmp(argv[1], h)) {
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	if (!strcmp(argv[1], p)) {
		port = atoi(argv[2]);
		if (strcmp(argv[3], h)) {
			printf("ERROR: invalid arguments\n");
			exit(0);
		}
		host = argv[4];
	}
	else if (!strcmp(argv[1], h)) {
		host = argv[2];
		if (strcmp(argv[3], p)) {
			printf("ERROR: invalid arguments\n");
			exit(0);
		}
		port = atoi(argv[4]);
	}

	memset(&server_addr, 0, sizeof(server_addr));   //set socket
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(host);

	state = 1;                        //state change when data is connected with last data
	while(fgets(buf, BUFFER_SIZE, stdin)) {
		if (buf[0] == '\n') {                     //first character is enter
			if (state == -1) {                   //be connected with last data
				client_socket = socket(PF_INET, SOCK_STREAM, 0); //make socket
				if (client_socket == -1) {
					printf("socket 생성 실패\n");
					exit(0);
				}                                 //connect socket
				res = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
				if (res == -1) {
					printf("connect 실패\n");
					exit(0);
				}
				write(client_socket, buf, strlen(buf));    //send enter
				while (1) {
					read(client_socket, buf, BUFFER_SIZE);
					if (!strncmp(buf, ready, 5)) break;   //wait until recieve server's answer
				}
				close(client_socket);              //close socket
		 		state = 0;
			}
			else if (state == 0) break;       //just input enter with no string so finish
			else state = 0;                   //first input is enter
		}
		else {                              //first character is no enter
			client_socket = socket(PF_INET, SOCK_STREAM, 0);    //make socket
			if (client_socket == -1) {
				printf("socket 생성 실패\n");
				exit(0);
			}                                         //connect socket
			res = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
			if (res == -1) {
		    	printf("connect 실패\n");
				exit(0);
			}
			write(client_socket, buf, strlen(buf));    //send data what recieve from stdin
			state = 0;
			if (buf[strlen(buf)-1] != '\n') {       
				//if last character is not enter, then next data will be connected with this data
				state = -1;
			}
			while (1) {
				read(client_socket, buf, BUFFER_SIZE);
				if (!strncmp(buf, ready, 5)) break;   //wait until recieve server's answer
			}
			close(client_socket);
		}
	}
	//when there is no enter last and finish fgets mean there are EOF so send enter to server
	if (state == -1) {
		buf[0] = '\n';
		buf[1] = '\0';
		client_socket = socket(PF_INET, SOCK_STREAM, 0);
		if (client_socket == -1) {
			printf("socket 생성 실패\n");
			exit(0);
		}
		res = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
		if (res == -1) {
			printf("connect 실패\n");
			exit(0);
		}
		write(client_socket, buf, strlen(buf));              //send enter
		close(client_socket);
	}
	return 0;
}
