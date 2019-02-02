#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#define BUFFER_SIZE 2048

int main(int argc, char* argv[])
{
	int i, id, port, res, *index;
	int proxy_socket;
	struct sockaddr_in proxy_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;
	double timeout;                          //time for expiration
	int Cache_URL_id, index_id, time_id;     //id for shared memory
	char **Cache_URL;                        //save url info for cache
	clock_t *Cache_time, current_time;
	char expiration[] = "expiration.txt";    

	Cache_URL_id = shmget((key_t)1234, 1024, IPC_CREAT|0660);     //make shared memory fot cache
	if (Cache_URL_id == -1) {
		printf("ERROR: fail to make shared memory\n");
		exit(0);
	}
	index_id = shmget((key_t)3456, 4, IPC_CREAT|0660);     //shared memory for Cache
	if (index_id == -1) {
		printf("ERROR: fail to make shared memory\n");
		exit(0);
	}   
	time_id = shmget((key_t)5678, 512, IPC_CREAT|0600);    
	if (time_id == -1) {
		printf("ERROR: fail to make shared memory\n");
		exit(0);
	}
	Cache_URL = (char **)shmat(Cache_URL_id, NULL, 0);       //attach for using
	index = (int *)shmat(index_id, NULL, 0);
	Cache_time = (clock_t *)shmat(time_id, NULL, 0);
	
	*index = 0;                                   //initialize shared memory    

	if (!(argc == 3 || (argc == 2 && !strcmp(argv[0], "./proxy")))) { //argc == 2 for python test
		printf("ERROR: invalid argument number\n");
		exit(0);
	}
	port = atoi(argv[1]);
	timeout = atoi(argv[2]);           //timeout value for expire
	
	proxy_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (proxy_socket == -1) {
		printf("ERROR: socket 생성 실패\n");
		exit(0);
	}

	memset(&proxy_addr, 0, sizeof(proxy_addr));     //set for proxy server
	proxy_addr.sin_family = PF_INET;
	proxy_addr.sin_port = htons(port);
	proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	res = bind(proxy_socket, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr));
	if (res == -1) {
		printf("ERROR: bind 실패\n");
		exit(0);
	}
	
	if (!fork()) {                             //another process for Cache time expire
		while(1) {
			if (*index != 0 && Cache_time[*index-1] == (time_t)9999) {  //need to check time
				Cache_time[*index-1] = clock();
			}
			for (i = 0; i < *index; i++) {
				current_time = clock();
				if(Cache_time[i] != (time_t)9999 && (double)(current_time - Cache_time[i])/(CLOCKS_PER_SEC) > timeout) {	
					char buf[15];          //expiration so can't access this URL cache
					char file_name[11] = "Cache";
					sprintf(buf, "%d", i);
					strcat(buf, ".txt");
					strcat(file_name, buf);
					rename(file_name, expiration);  //rename file to expiration
				}
			}	
		}
	}	

	while(1) {                                   //open proxy server for client
		res = listen(proxy_socket, 5);
		if (res == -1) {
			printf("ERROR: listen 실패\n");
			exit(0);
		}

		addr_size = sizeof(client_addr);        
		client_socket = accept(proxy_socket, (struct sockaddr *)&client_addr, &addr_size);
		if (client_socket == -1) {
			printf("ERROR: client accept 실패\n");
			exit(0);
		}

		id = shmget((key_t)(*index+2000), 1024, IPC_CREAT|0660);
		if (id == -1) {
		 	printf("ERROR: fail to make shared memory\n");
		    exit(0);
		}
		Cache_URL[*index] = (char *)shmat(id, NULL, 0);    //make shared memory for new input URL

		if (!fork()) {                            //another process for new client
			char message[BUFFER_SIZE+1] = {'\0'};
			char response[BUFFER_SIZE+1] = {'\0'};
			char *str, *ptr, *temp;
			char *host, *URL, *path, *method;
			int port = 80;
			int server_socket;
			long int *host_ip;
			struct sockaddr_in server_addr;
			struct hostent *server_host;
			FILE *fp;
			char out_file_name[13] = "./Cache";
			
			close(proxy_socket);
			read(client_socket, message, BUFFER_SIZE);   //read request message
 
			str = strdup(message);            //allocate because strtok function change message
			ptr = strtok(str, " ");         //ptr is method
			if (ptr == NULL) {
				printf("ERROR: invalid request message\n");
				free(str);
				close(client_socket);
				exit(0);
			}
			method = ptr;

			ptr = strtok(NULL, " ");        //ptr is URL, parse this later
			if (ptr == NULL) {
				printf("ERROR: invalid request message\n");
				free(str);
				close(client_socket);
				exit(0);
			}
			URL = ptr;                   //save URL string pointer in variable URL
 
			ptr = strtok(NULL, "\n");     //ptr is http version
			if (strcmp(ptr, "HTTP/1.0") && strcmp(ptr, "HTTP/1.0\r")) {
				printf("ERROR: invalid request message's http version\n");
				free(str);
				close(client_socket);
				exit(0);
			}

			ptr = strtok(NULL, " ");          //ptr is maybe "Host:"
			if (strcmp(ptr, "Host:")) {
				printf("ERROR: 400 Bad Request\n");
			 	free(str);
 	 			close(client_socket);
   				exit(0);
			}

			ptr = strtok(NULL, "\r\n");           //ptr is host value
			if (ptr == NULL) {
				printf("ERROR: 400 Bad Request\n");
			  	free(str);
			   	close(client_socket);
			    exit(0);
			}
			host = ptr;

			for (i = *index-1; i > -1; i--) {        //find URL in cache memory from the latest
				if (!strcmp(URL, Cache_URL[i])) {   //if There is Cache for this URL
					char buf[15];
					char in_file_name[11] = "Cache";
					sprintf(buf, "%d", i);
					strcat(buf, ".txt");
					strcat(in_file_name, buf);
					fp = fopen(in_file_name, "r");    //send response message file     
					if (fp == NULL) break;             //if that file become expiration
					while(res = fread(response, 1, BUFFER_SIZE, fp) > 0) {
						write(client_socket, response, strlen(response));
					}
					fclose(fp);
					free(str);
					close(client_socket);
					exit(0);
				}
			}
			strcpy(Cache_URL[*index], URL);        //save this URL for new Cache

			ptr = strtok(URL, "/");       //ptr is maybe "http:"
			ptr = strtok(NULL, ":");        //ptr is host
			ptr = strtok(NULL, "/");        //ptr is maybe port number
			if (ptr != NULL && atoi(ptr) != 0) port = atoi(ptr);  //set port number
			ptr = strtok(NULL, " ");
			if (ptr != NULL) path = ptr;                         //set path

			server_host = gethostbyname(host);     //get ip of this URL
			if (server_host == NULL) {
				printf("ERROR: can't get host by name\n");
				free(str);
				close(client_socket);
				exit(0);
			}

			memset(&server_addr, 0, sizeof(server_addr));   //set server information
			server_addr.sin_family = AF_INET;
			server_addr.sin_port = htons(port);            //set port
			bcopy(server_host->h_addr, &(server_addr.sin_addr.s_addr), server_host->h_length);
			free(str);                                     //set ip address

			server_socket = socket(PF_INET, SOCK_STREAM, 0);   //make socket
			if (server_socket == -1) {
				printf("socket 생성 실패\n");
				close(client_socket);
				exit(0);
			}
			res = connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
			if (res == -1) {
				printf("connect 실패\n");
				close(client_socket);
				exit(0);
			}

			res = write(server_socket, message, strlen(message)+1);  //write to server from proxy
			if (res == -1) {
				printf("send request message to server 실패\n");
				close(client_socket);
				close(server_socket);
				exit(0);
			}

			char buf[15];
			sprintf(buf, "%d", *index);
		    strcat(buf, ".txt");
		    strcat(out_file_name, buf);
			fp = fopen(out_file_name, "w");      //save response message for Cache

			Cache_time[*index] = (time_t)9999;      //talk to another process for check time
						
			while(res = read(server_socket, response, BUFFER_SIZE) > 0) {   //read from server
				write(client_socket, response, strlen(response)+1);
				fprintf(fp, "%s", response);                       //save to file 
				bzero((char *)response, BUFFER_SIZE);
			}
			*index = *index + 1;     //increase Cache index

			fclose(fp);
			close(server_socket);
			close(client_socket);
			exit(0);
		}
		close(client_socket);
	}
}
