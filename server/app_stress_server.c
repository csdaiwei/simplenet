#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "../common/constants.h"
#include "stcp_server.h"

#define CLIENTPORT1 87
#define SERVERPORT1 88

#define WAITTIME 40

//这个函数连接到本地SIP进程的端口SIP_PORT. 如果TCP连接失败, 返回-1. 连接成功, 返回TCP套接字描述符, STCP将使用该描述符发送段.
int connectToSIP() {

	//你需要编写这里的代码.
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in local_server;
	memset(&local_server, 0, sizeof(local_server));
	local_server.sin_family = AF_INET;
	local_server.sin_addr.s_addr = inet_addr("127.0.0.1");
	local_server.sin_port = htons(SIP_PORT);

	int connd = connect(socket_fd, (struct sockaddr *)&local_server, sizeof(local_server));
	if (connd < 0)
		return -1;

	return socket_fd;
}

//这个函数断开到本地SIP进程的TCP连接. 
void disconnectToSIP(int sip_conn) {

	//你需要编写这里的代码.
	close(sip_conn);
	exit(0);
}

int main() {

	srand(time(NULL));


	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}

	stcp_server_init(sip_conn);

	int sockfd= stcp_server_sock(SERVERPORT1);
	if(sockfd<0) {
		printf("can't create stcp server\n");
		exit(1);
	}

	stcp_server_accept(sockfd);

	int fileLen;
	stcp_server_recv(sockfd,&fileLen,sizeof(int));
	char* buf = (char*) malloc(fileLen);
	stcp_server_recv(sockfd,buf,fileLen);


	FILE* f;
	f = fopen("receivedtext.txt","a");
	fwrite(buf,fileLen,1,f);
	fclose(f);
	free(buf);

	sleep(WAITTIME);

	if(stcp_server_close(sockfd)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}				

	disconnectToSIP(sip_conn);

	return 0;
}
