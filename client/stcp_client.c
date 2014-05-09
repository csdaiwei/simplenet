#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "stcp_client.h"
#include "../common/seg.h"
#include "../common/bool.h"

client_tcb_t* client_tcb_table[MAX_TRANSPORT_CONNECTIONS];
int sip_conn;


struct timeval prog_start_time;	//initialed in stcp_client_init()

/*get time from program start, in millisecond */
unsigned int get_time(){
  	struct timeval te; 
    gettimeofday(&te, NULL);
    unsigned int milliseconds = (te.tv_sec - prog_start_time.tv_sec)*1000 + (te.tv_usec - prog_start_time.tv_usec)/1000;
    return milliseconds;
}

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
void stcp_client_init(int conn) 
{
	gettimeofday(&prog_start_time, NULL);
	pthread_t tid;
	pthread_create(&tid, NULL, seghandler, NULL);
  	int i;
  	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
  		client_tcb_table[i] = NULL;
  	sip_conn = conn;
  	return;
}

// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_client_sock(unsigned int client_port) 
{
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {
		client_tcb_t *tcb = client_tcb_table[i];
		if (tcb == NULL) {
			tcb = (client_tcb_t *)malloc(sizeof(client_tcb_t));
			memset(tcb, 0, sizeof(client_tcb_t));
			tcb -> client_nodeID = topology_getMyNodeID();
			tcb -> client_portNum = client_port;
			tcb -> state = CLOSED;

			tcb -> bufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
			pthread_mutex_init(tcb -> bufMutex, NULL);	//todo destroy
			
			client_tcb_table[i] = tcb;
			return i;
		}
	}
	return -1;
}

// 这个函数用于连接服务器. 它以套接字ID, 服务器节点ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器节点ID和服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL)
		return -1;
	if (tcb -> state != CLOSED)
		return -1;
	tcb -> state = SYNSENT;
	tcb -> server_nodeID = nodeID;
	tcb -> server_portNum = server_port;
	//set send segment.
	seg_t send_segment;

	build_segment_head(&send_segment, tcb -> client_portNum, server_port, 0, SYN);

	int send_num = 0;
	while (true) {

		//send SYN to server
		sip_sendseg(sip_conn, nodeID, &send_segment);
		
		//wait SYN_TIMEOUT nano seconds
		usleep(SYN_TIMEOUT / 1000);

		if (tcb -> state == CONNECTED)
			return 1;

		//timeout
		send_num ++;
		if (send_num > SYN_MAX_RETRY) {
			tcb -> state = CLOSED;
			return -1;
		}
	}
}

// 发送数据给STCP服务器. 这个函数使用套接字ID找到TCB表中的条目.
// 然后它使用提供的数据创建segBuf, 将它附加到发送缓冲区链表中.
// 如果发送缓冲区在插入数据之前为空, 一个名为sendbuf_timer的线程就会启动.
// 每隔SENDBUF_ROLLING_INTERVAL时间查询发送缓冲区以检查是否有超时事件发生. 
// 这个函数在成功时返回1，否则返回-1. 
// stcp_client_send是一个非阻塞函数调用.
// 因为用户数据被分片为固定大小的STCP段, 所以一次stcp_client_send调用可能会产生多个segBuf
// 被添加到发送缓冲区链表中. 如果调用成功, 数据就被放入TCB发送缓冲区链表中, 根据滑动窗口的情况,
// 数据可能被传输到网络中, 或在队列中等待传输.
int stcp_client_send(int sockfd, void* data, unsigned int length) 
{
    if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL)
		return -1;
	if (tcb -> state != CONNECTED)
		return -1;
	
	// if length > MAX_SEG_LEN, data will be cut and send twice or more	
	int real_length = (length > MAX_SEG_LEN) ? MAX_SEG_LEN : length;

	/*build a segbuf to contain new segment*/
	segBuf_t* new_segbuf = (segBuf_t*)malloc(sizeof(segBuf_t));
	new_segbuf -> sentTime = -1;
	new_segbuf -> next = NULL;

	seg_t *new_seg = &(new_segbuf -> seg);
	build_segment_head(new_seg, tcb -> client_portNum, tcb -> server_portNum, real_length, DATA);
	new_seg -> header.seq_num = tcb -> next_seqNum;
	memcpy(new_seg -> data, data, real_length);

	tcb -> next_seqNum += real_length;

	/*add to send buf list*/
	pthread_mutex_lock(tcb -> bufMutex);

	if(tcb -> sendBufHead == NULL){
		assert(tcb -> sendBufunSent == NULL && tcb -> sendBufTail == NULL);
		tcb -> sendBufHead = new_segbuf;
		tcb -> sendBufunSent = new_segbuf;
		tcb -> sendBufTail = new_segbuf;

		/*start timer*/
		pthread_t tid;
		pthread_create(&tid, NULL, sendBuf_timer, tcb);
	}
	else{
		assert(tcb -> sendBufTail != NULL);
		tcb -> sendBufTail -> next = new_segbuf;
		tcb -> sendBufTail = new_segbuf;
		tcb -> sendBufunSent = (tcb -> sendBufunSent == NULL) ? new_segbuf : tcb -> sendBufunSent;
	}

	/*send the first unsent segment*/
	if(tcb -> unAck_segNum < GBN_WINDOW){

		//the first of the unsent seg must be the new seg
		assert(&(tcb -> sendBufunSent -> seg) == new_seg);

		sip_sendseg(sip_conn, tcb -> server_nodeID, new_seg);
		printf("send a data segment, seq %d\n", new_seg -> header.seq_num);
		
		new_segbuf -> sentTime = get_time();
		tcb -> unAck_segNum ++;
		tcb -> sendBufunSent = tcb -> sendBufunSent -> next;

	}

	pthread_mutex_unlock(tcb -> bufMutex);


	if(real_length == length)
  		return 1;	//1 on success, -1 on fail
  	else
  		return stcp_client_send(sockfd, (char *)data + MAX_SEG_LEN, length - MAX_SEG_LEN);
}

// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN段给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
int stcp_client_disconnect(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL) 
		return -1;
	if (tcb -> state != CONNECTED)
		return -1;
	tcb -> state = FINWAIT;

	seg_t send_segment;
	build_segment_head(&send_segment, tcb -> client_portNum, tcb -> server_portNum, 0, FIN);
	
	int send_num = 0;
	while (true) { 

		//send FIN to server
		sip_sendseg(sip_conn, tcb -> server_nodeID, &send_segment);
		
		//wait FIN_TIMEOUT nano seconds
		usleep(FIN_TIMEOUT / 1000);

		if (tcb -> state == CLOSED){
			
			//clear buf
			if(tcb -> sendBufHead != NULL){
				segBuf_t* p = tcb -> sendBufHead;
				while(p != NULL){
					tcb -> sendBufHead = p -> next;
					free(p);
					p = tcb -> sendBufHead;
				}
				tcb -> sendBufunSent = tcb -> sendBufTail = NULL;
			}

			return 1;
		}
		
		//timeout
		send_num ++;
		if (send_num > FIN_MAX_RETRY) {
			tcb -> state = CLOSED;
			return -1;
		}
	}
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_client_close(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL) 
		return -1;
	if (tcb -> state != CLOSED)
		return -1;
	
	pthread_mutex_destroy(tcb -> bufMutex);
	free(tcb);
	
	tcb = NULL;
	return 1;
}



/*new func*/
int stcp_get_sockfd(seg_t *segment) {
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++) {
		client_tcb_t *tcb = client_tcb_table[i];
		if (tcb != NULL && tcb -> state != CLOSED) {
			if ((tcb -> client_portNum == segment -> header.dest_port) 
				&& (tcb -> server_portNum == segment -> header.src_port))
				return i;
		}
	}
	return -1;
}

// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
void* seghandler(void* arg) 
{
  	seg_t recv_segment;
	int sockfd;
	int connection_state;
	int src_nodeID;
	while (true) {
		connection_state = sip_recvseg(sip_conn, &src_nodeID, &recv_segment);
		
		//the segment is lost
		if (connection_state == -1) {
			continue;
		}
		
		sockfd = stcp_get_sockfd(&recv_segment);
		client_tcb_t *tcb = client_tcb_table[sockfd];
		
		assert(src_nodeID == tcb -> server_nodeID);

		//sockfd error
		if (sockfd == -1) {
			continue;
		}
		
		//SYNACK
		if (recv_segment.header.type == SYNACK) {
			if (tcb -> state == SYNSENT) {
				tcb -> state = CONNECTED;
			}
		}
		
		//FINACK
		if (recv_segment.header.type == FINACK) {
			if (tcb -> state == FINWAIT) {
				tcb -> state = CLOSED;
			}
		}

		//DATAACK
		if (recv_segment.header.type == DATAACK) {
			if(tcb -> state == CONNECTED){
				
				printf("receive a dataack segment, ack %d\n", recv_segment.header.ack_num);

				int ack = recv_segment.header.ack_num;

				pthread_mutex_lock(tcb -> bufMutex);

				segBuf_t* bufhead = tcb -> sendBufHead;

				//ack segs those seq<ack
				while(bufhead -> seg.header.seq_num < ack){
					
					tcb -> sendBufHead = tcb -> sendBufHead -> next;
					
				//	printf("free segment in buf, seq %d\n", bufhead -> seg.header.seq_num);
					free(bufhead);

					bufhead = tcb -> sendBufHead;
					tcb -> unAck_segNum --;
					
					if(bufhead == NULL){
						assert(tcb -> sendBufunSent == NULL);
						tcb -> sendBufTail = NULL;
						break;
					}
				}

				//send blocked segments(if exist)
				while(tcb -> sendBufunSent != NULL && tcb -> unAck_segNum < GBN_WINDOW){
				
					sip_sendseg(sip_conn, tcb -> server_nodeID, &(tcb -> sendBufunSent -> seg));
					printf("send a data segment, seq %d\n", (tcb -> sendBufunSent -> seg). header.seq_num);

					tcb -> sendBufunSent -> sentTime = get_time();
					tcb -> unAck_segNum ++;
					tcb -> sendBufunSent = tcb -> sendBufunSent -> next;
				}
				pthread_mutex_unlock(tcb -> bufMutex);
			}
		}
	}
}


//这个线程持续轮询发送缓冲区以触发超时事件. 如果发送缓冲区非空, 它应一直运行.
//如果(当前时间 - 第一个已发送但未被确认段的发送时间) > DATA_TIMEOUT, 就发生一次超时事件.
//当超时事件发生时, 重新发送所有已发送但未被确认段. 当发送缓冲区为空时, 这个线程将终止.
void* sendBuf_timer(void* clienttcb) 
{
	client_tcb_t* tcb = (client_tcb_t*)clienttcb;

	while(true){

		usleep(SENDBUF_POLLING_INTERVAL / 1000);

		if(tcb -> sendBufHead == NULL)	//send buf empty
			return NULL;
		
		int sentTime = tcb -> sendBufHead -> sentTime;
		int currTime = get_time();

		if((currTime - sentTime) * 100000 > DATA_TIMEOUT){

			//printf("data segment timeout, seq %d\n", (tcb -> sendBufHead -> seg).header.seq_num);
			
			pthread_mutex_lock(tcb -> bufMutex);
			segBuf_t* resent_segBuf = tcb -> sendBufHead;
			int unAck_segNum = tcb -> unAck_segNum;
			int i;
			for ( i = 0; i < unAck_segNum; ++i){
				
				assert(resent_segBuf != NULL);
				sip_sendseg(sip_conn, tcb -> server_nodeID, &(resent_segBuf -> seg));
		//		printf("resend the data segment, seq %d\n", (resent_segBuf -> seg). header.seq_num);

				resent_segBuf -> sentTime = get_time();
				resent_segBuf = resent_segBuf -> next;
				
			}

			assert(resent_segBuf == tcb -> sendBufunSent);
			
			pthread_mutex_unlock(tcb -> bufMutex);

			
		}
	}
	return NULL;
}

void
build_segment_head(seg_t* segment, int src_port, int dest_port, int length, int type){

	segment -> header.src_port = src_port;
	segment -> header.dest_port = dest_port;
	segment -> header.length = length;
	segment -> header.type = type;	
}