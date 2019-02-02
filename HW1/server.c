#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#define BUFFER_SIZE 1024  //maximum size of each socket's data

int main(int argc, char* argv[])
{
	int res, port;
	int server_socket;
	struct sockaddr_in server_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;
	char p[] = "-p";
	char ready[] = "ready";                        //server's answer to client

	if (argc != 3) {                              //invalid argument
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	if (strcmp(argv[1], p)) {
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	port = atoi(argv[2]);

	server_socket = socket(PF_INET, SOCK_STREAM, 0);  //make socket
	if (server_socket == -1) {
		printf("ERROR: socket 생성 실패\n");
		exit(0);
	}

	memset(&server_addr, 0, sizeof(server_addr));     //set socket
	server_addr.sin_family = PF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//bind socket
	res = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (res == -1) {
		printf("ERROR: bind 실패\n");
		exit(0);
	}

	//recieve data from client and print continue
	while(1) {
		char buf[BUFFER_SIZE+1] = {'\0'};      //reser buffer

		res = listen(server_socket, 5);       //listen socket
		if (res == -1) {
			printf("ERROR: listen 실패\n");
			exit(0);
		}

		addr_size = sizeof(client_addr);       //accept socket
		client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
		if (client_socket == -1) {
			printf("ERROR: client accept 실패\n");
			exit(0);
		}

		if (!fork()) {                       //print data in child process
			close(server_socket);            //child process don't need to listen
			read(client_socket, buf, BUFFER_SIZE);
			printf("%s", buf);
			write(client_socket, ready, strlen(ready)+1);  //answer to client
			close(client_socket);
			exit(0);
		}
		//parent process close client socket and ready for new data from another socket
		close(client_socket);
	}
}
