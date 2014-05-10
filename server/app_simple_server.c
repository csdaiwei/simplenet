//ÎÄ¼þÃû: server/app_simple_server.c

//ÃèÊö: ÕâÊÇ¼òµ¥°æ±¾µÄ·þÎñÆ÷³ÌÐò´úÂë. ·þÎñÆ÷Ê×ÏÈÁ¬½Óµ½±¾µØSIP½ø³Ì. È»ºóËüµ÷ÓÃstcp_server_init()³õÊ¼»¯STCP·þÎñÆ÷. 
//ËüÍ¨¹ýÁ½´Îµ÷ÓÃstcp_server_sock()ºÍstcp_server_accept()´´½¨2¸öÌ×½Ó×Ö²¢µÈ´ýÀ´×Ô¿Í»§¶ËµÄÁ¬½Ó. ·þÎñÆ÷È»ºó½ÓÊÕÀ´×ÔÁ½¸öÁ¬½ÓµÄ¿Í»§¶Ë·¢ËÍµÄ¶Ì×Ö·û´®. 
//×îºó, ·þÎñÆ÷Í¨¹ýµ÷ÓÃstcp_server_close()¹Ø±ÕÌ×½Ó×Ö, ²¢¶Ï¿ªÓë±¾µØSIP½ø³ÌµÄÁ¬½Ó.

//´´½¨ÈÕÆÚ: 2013Äê1ÔÂ

//ÊäÈë: ÎÞ

//Êä³ö: STCP·þÎñÆ÷×´Ì¬

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

//´´½¨Á½¸öÁ¬½Ó, Ò»¸öÊ¹ÓÃ¿Í»§¶Ë¶Ë¿ÚºÅ87ºÍ·þÎñÆ÷¶Ë¿ÚºÅ88. ÁíÒ»¸öÊ¹ÓÃ¿Í»§¶Ë¶Ë¿ÚºÅ89ºÍ·þÎñÆ÷¶Ë¿ÚºÅ90.
#define CLIENTPORT1 87
#define SERVERPORT1 88
#define CLIENTPORT2 89
#define SERVERPORT2 90
//ÔÚ½ÓÊÕµ½×Ö·û´®ºó, µÈ´ý15Ãë, È»ºó¹Ø±ÕÁ¬½Ó.
#define WAITTIME 15

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
	//ÓÃÓÚ¶ª°üÂÊµÄËæ»úÊýÖÖ×Ó
	srand(time(NULL));

	//Á¬½Óµ½SIP½ø³Ì²¢»ñµÃTCPÌ×½Ó×ÖÃèÊö·û
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}

	//³õÊ¼»¯STCP·þÎñÆ÷
	stcp_server_init(sip_conn);

	//ÔÚ¶Ë¿ÚSERVERPORT1ÉÏ´´½¨STCP·þÎñÆ÷Ì×½Ó×Ö 
	int sockfd= stcp_server_sock(SERVERPORT1);
	if(sockfd<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//¼àÌý²¢½ÓÊÜÀ´×ÔSTCP¿Í»§¶ËµÄÁ¬½Ó 
	stcp_server_accept(sockfd);
	printf("server start to listening on sock %d\n", sockfd);

	//ÔÚ¶Ë¿ÚSERVERPORT2ÉÏ´´½¨ÁíÒ»¸öSTCP·þÎñÆ÷Ì×½Ó×Ö
	int sockfd2= stcp_server_sock(SERVERPORT2);
	if(sockfd2<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//¼àÌý²¢½ÓÊÜÀ´×ÔSTCP¿Í»§¶ËµÄÁ¬½Ó 

	stcp_server_accept(sockfd2);
	printf("server start to listening on sock %d\n", sockfd2);

	char buf1[6];
	char buf2[7];
	int i;
	//½ÓÊÕÀ´×ÔµÚÒ»¸öÁ¬½ÓµÄ×Ö·û´®
	for(i=0;i<5;i++) {
		stcp_server_recv(sockfd,buf1,6);
		printf("recv string: %s from connection 1\n",buf1);
	}
	//½ÓÊÕÀ´×ÔµÚ¶þ¸öÁ¬½ÓµÄ×Ö·û´®
	for(i=0;i<5;i++) {
		stcp_server_recv(sockfd2,buf2,7);
		printf("recv string: %s from connection 2\n",buf2);
	}

	sleep(WAITTIME);

	//¹Ø±ÕSTCP·þÎñÆ÷ 
	if(stcp_server_close(sockfd)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}				
	if(stcp_server_close(sockfd2)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}				

	//¶Ï¿ªÓëSIP½ø³ÌÖ®¼äµÄÁ¬½Ó
	disconnectToSIP(sip_conn);

	return 0;
}
