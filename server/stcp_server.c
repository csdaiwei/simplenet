//文件名: server/stcp_server.c
//
//描述: 这个文件包含STCP服务器接口实现. 
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"
#include "../common/bool.h"

//声明tcbtable为全局变量
server_tcb_t* server_tcb_table[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
void stcp_server_init(int conn) 
{
	pthread_t tid;
	pthread_create(&tid, NULL, seghandler, NULL);
	sip_conn = conn;
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
		server_tcb_table[i] = NULL;
	return;
}

// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_server_sock(unsigned int server_port) 
{
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {
		server_tcb_t *tcb = server_tcb_table[i];
		if (tcb == NULL) {
			tcb = (server_tcb_t *)malloc(sizeof(server_tcb_t));
			memset(tcb, 0, sizeof(server_tcb_t));
			tcb -> server_nodeID = topology_getMyNodeID();
			tcb -> server_portNum = server_port;
			tcb -> state = CLOSED;

			tcb -> bufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
			pthread_mutex_init(tcb -> bufMutex, NULL);

			tcb -> recvBuf = (char *)malloc(RECEIVE_BUF_SIZE);	//todo destroy
			memset(tcb -> recvBuf, 0, RECEIVE_BUF_SIZE);
			
			server_tcb_table[i] = tcb;
			return i;
		}
	}
	return -1;
}

// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后启动定时器进入忙等待直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
int stcp_server_accept(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	server_tcb_t *tcb = server_tcb_table[sockfd];
	if (tcb == NULL)
		return -1;
	if (tcb -> state != CLOSED)
		return -1;
	tcb -> state = LISTENING;
	while (true) {
		if (tcb -> state == CONNECTED)
			return 1;
		usleep(ACCEPT_POLLING_INTERVAL / 1000);
	}
}

// 接收来自STCP客户端的数据. 这个函数每隔RECVBUF_POLLING_INTERVAL时间
// 就查询接收缓冲区, 直到等待的数据到达, 它然后存储数据并返回1. 如果这个函数失败, 则返回-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int length) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	server_tcb_t *tcb = server_tcb_table[sockfd];
	if (tcb == NULL)
		return -1;
	if (tcb -> state != CONNECTED)
		return -1;

	while(true){
		
		if(tcb -> usedBufLen >= length){
			//if enough, take data out of recv buf
			pthread_mutex_lock(tcb -> bufMutex);
			memcpy(buf, tcb -> recvBuf, length);
			tcb -> usedBufLen -= length;
			int i;
			for (i = 0; i < tcb -> usedBufLen; ++i){
				tcb -> recvBuf[i] = tcb -> recvBuf[i + length];
			}
			memset(tcb -> recvBuf + tcb -> usedBufLen, 0, length);

			pthread_mutex_unlock(tcb -> bufMutex);
			return 1;
		}

		//not enough data in recv buf, wait for a moment
		sleep(RECVBUF_POLLING_INTERVAL);
	}
  	return 1;
}





// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_server_close(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS) {
		return -1;
	}
		
	server_tcb_t *tcb = server_tcb_table[sockfd];
	if (tcb == NULL) {
		return -1;
	}
	if (tcb -> state != CLOSED) {
		return -1;
	}
	
	free(tcb -> bufMutex);	//modify
	free(tcb -> recvBuf);
	free(tcb);
	server_tcb_table[sockfd] = NULL;
	
	return 1;
}

/*new functions for seghandler*/
void *wait_to_close_server(void *servertcb) {

	pthread_detach(pthread_self());	//modify
	
	server_tcb_t *tcb = (server_tcb_t *)servertcb;
	
	sleep(CLOSEWAIT_TIMEOUT);

	if (tcb -> state == CLOSEWAIT){
		printf("client disconnect to server, server port: %d, client port: %d\n", 
			tcb -> server_portNum, tcb -> client_portNum);
		tcb -> state = CLOSED;

		//clear buf
		memset(tcb -> recvBuf, 0, tcb -> usedBufLen);
		tcb -> usedBufLen = 0;

	}
	return NULL;
}

int stcp_get_sockfd(seg_t *segment) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {
		server_tcb_t *tcb = server_tcb_table[i];
		if (tcb != NULL) {
			if (tcb -> state == LISTENING) {
				if (tcb -> server_portNum == segment -> header.dest_port)
					return i;
			}
			if (tcb -> state == CONNECTED || tcb -> state == CLOSEWAIT)
				if ((tcb -> server_portNum == segment -> header.dest_port)
					&&(tcb -> client_portNum == segment -> header.src_port))
					return i;
		}
	}
	return -1;
}

int get_client_port(int sockfd) {
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	server_tcb_t *tcb = server_tcb_table[sockfd];
	if (tcb == NULL)
		return -1;
	if (tcb -> state == CLOSED || tcb -> state == LISTENING)
		return -1;
	return tcb -> client_portNum;
}

void* seghandler(void* arg)
{
  	
  	pthread_detach(pthread_self());	//modify

  	int sockfd;
	int connection_state;
	
	seg_t send_segment;
	seg_t recv_segment;
	while (true) {
		
		int src_nodeID;
		connection_state = sip_recvseg(sip_conn, &src_nodeID, &recv_segment);
		
		//printf("stcp server recv a segment from node %d, segment type %d\n", src_nodeID, recv_segment.header.type);

		if (connection_state == -1) {
			continue;	//seg lost
		}
		
		sockfd = stcp_get_sockfd(&recv_segment);
		server_tcb_t *tcb = server_tcb_table[sockfd];
		if (sockfd == -1) {
			continue;	//socket not found
		}

		//SYN
		if (recv_segment.header.type == SYN) {

			//printf("tcb -> state %d\n", tcb -> state);

			if (tcb -> state == LISTENING || tcb -> state == CONNECTED) {
				tcb -> state = CONNECTED;
				tcb -> client_nodeID = src_nodeID;
				tcb -> client_portNum = recv_segment.header.src_port;
				
				//send back a SYNACK
				memset(&send_segment, 0, sizeof(send_segment));
				build_segment_head(&send_segment, tcb -> server_portNum, recv_segment.header.src_port, 0, SYNACK);
				sip_sendseg(sip_conn, tcb -> client_nodeID, &send_segment);
			}
		}
		
		//FIN
		if (recv_segment.header.type == FIN) {
			if (tcb -> state == CONNECTED || tcb -> state == CLOSEWAIT) {
				tcb -> state = CLOSEWAIT;

				//send back a FINACK
				memset(&send_segment, 0, sizeof(send_segment));
				build_segment_head(&send_segment, tcb -> server_portNum, recv_segment.header.src_port, 0, FINACK);
				sip_sendseg(sip_conn, tcb -> client_nodeID, &send_segment);
				
				//create thread to set the state to CLOSED after seconds
				pthread_t tid;
				pthread_create(&tid, NULL, wait_to_close_server, tcb);
			}
		}

		//DATA
		if(recv_segment.header.type == DATA){
			if(tcb -> state == CONNECTED){
					
				if(recv_segment.header.seq_num == tcb -> expect_seqNum){
					
					printf("receive a proper data segment, seq %d\n",  recv_segment.header.seq_num);
			
					//store data into recv buf
					pthread_mutex_lock(tcb -> bufMutex);		
					memcpy(tcb -> recvBuf + tcb -> usedBufLen , recv_segment.data, recv_segment.header.length);
					

					//update sequence
					tcb -> usedBufLen += recv_segment.header.length;
					tcb -> expect_seqNum += recv_segment.header.length;	
					pthread_mutex_unlock(tcb -> bufMutex);

					//send data ack
					memset(&send_segment, 0, sizeof(send_segment));
					build_segment_head(&send_segment, tcb -> server_portNum, recv_segment.header.src_port, 0, DATAACK);
					send_segment.header.ack_num = tcb -> expect_seqNum;
					sip_sendseg(sip_conn, tcb -> client_nodeID, &send_segment);
				}
			}

		}

	}
}

void
build_segment_head(seg_t* segment, int src_port, int dest_port, int length, int type){

	segment -> header.src_port = src_port;
	segment -> header.dest_port = dest_port;
	segment -> header.length = length;
	segment -> header.type = type;
	segment -> header.checksum = 0;	
}
