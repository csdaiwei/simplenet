#ifndef _STCP_H
#define _STCP_H

#include <pthread.h>
#include "../common/seg.h"
#include "../common/constants.h"

#define	CLOSED 		1
#define	SYNSENT 	2
#define	LISTENING 	3
#define	CONNECTED 	4
#define	CLOSEWAIT 	5
#define	FINWAIT 	6

typedef struct segBuf {
        seg_t seg;
        unsigned int sentTime;
        struct segBuf* next;
} segBuf_t;

//客户端传输控制块. 一个STCP连接的客户端使用这个数据结构记录连接信息.   
typedef struct client_tcb {
	unsigned int server_nodeID;        //服务器节点ID, 类似IP地址
	unsigned int server_portNum;       //服务器端口号
	unsigned int client_nodeID;     //客户端节点ID, 类似IP地址
	unsigned int client_portNum;    //客户端端口号
	unsigned int state;     	//客户端状态
	unsigned int next_seqNum;       //新段准备使用的下一个序号 
	pthread_mutex_t* bufMutex;      //发送缓冲区互斥量
	segBuf_t* sendBufHead;          //发送缓冲区头
	segBuf_t* sendBufunSent;        //发送缓冲区中的第一个未发送段
	segBuf_t* sendBufTail;          //发送缓冲区尾
	unsigned int unAck_segNum;      //已发送但未收到确认段的数量
} client_tcb_t;

//服务器传输控制块. 一个STCP连接的服务器端使用这个数据结构记录连接信息.
typedef struct server_tcb {
	unsigned int server_nodeID;        //服务器节点ID, 类似IP地址
	unsigned int server_portNum;       //服务器端口号
	unsigned int client_nodeID;     //客户端节点ID, 类似IP地址
	unsigned int client_portNum;    //客户端端口号
	unsigned int state;         	//服务器状态
	unsigned int expect_seqNum;     //服务器期待的数据序号	
	char* recvBuf;                  //指向接收缓冲区的指针
	unsigned int  usedBufLen;       //接收缓冲区中已接收数据的大小
	pthread_mutex_t* bufMutex;      //指向一个互斥量的指针, 该互斥量用于对接收缓冲区的访问
} server_tcb_t;

int connectToSIP();
void disconnectToSIP(int sip_conn);

void stcp_server_init(int conn);
int stcp_server_sock(unsigned int server_port);
int stcp_server_accept(int sockfd);
int stcp_server_recv(int sockfd, void* buf, unsigned int length);
int stcp_server_close(int sockfd);
int stcp_server_close_force(int sockfd);
void *wait_to_close_server(void *servertcb);
int get_client_port(int sockfd);
void* seghandler(void* arg);

unsigned int get_time();
void stcp_client_init(int conn);
int stcp_client_sock(unsigned int client_port);
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port);
int stcp_client_send(int sockfd, void* data, unsigned int length);
int stcp_client_disconnect(int sockfd);
int stcp_client_close(int sockfd);
int stcp_client_close_force(int sockfd);
void* sendBuf_timer(void* clienttcb);
void build_segment_head(seg_t* segment, int src_port, int dest_port, int length, int type);


#endif