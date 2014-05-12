#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <strings.h>
#include <pthread.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "stcp.h"
#include "../common/seg.h"
#include "../common/bool.h"
#include "../topology/topology.h"

server_tcb_t* server_tcb_table[MAX_TRANSPORT_CONNECTIONS];
client_tcb_t* client_tcb_table[MAX_TRANSPORT_CONNECTIONS];

struct timeval prog_start_time;

int sip_conn;

int connectToSIP() {

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

void disconnectToSIP(int sip_conn) {

	//你需要编写这里的代码.
	close(sip_conn);
	exit(0);
}

void stcp_server_init(int conn) 
{
	sip_conn = conn;
	int i;
	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
		server_tcb_table[i] = NULL;
	return;
}

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

int stcp_server_accept(int sockfd) 
{
	printf("server socket accepting%d\n", sockfd);

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
	printf("server socket accepted %d\n", sockfd);
}

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
		if (tcb -> state == CLOSED)
			return -1;
		//not enough data in recv buf, wait for a moment
		sleep(RECVBUF_POLLING_INTERVAL);
	}
  	return 1;
}

int stcp_server_close(int sockfd) 
{
	printf("server socket closing %d\n", sockfd);

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
	
	free(tcb -> bufMutex);
	free(tcb -> recvBuf);
	free(tcb);
	server_tcb_table[sockfd] = NULL;
	printf("server socket closed %d\n", sockfd);
	return 1;
}

int stcp_server_close_force(int sockfd) {
	
	printf("server socket closing %d\n", sockfd);

	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS) {
		return -1;
	}
		
	server_tcb_t *tcb = server_tcb_table[sockfd];
	if (tcb == NULL) {
		return -1;
	}

	free(tcb -> bufMutex);
	free(tcb -> recvBuf);
	free(tcb);
	server_tcb_table[sockfd] = NULL;
	printf("server socket closed %d\n", sockfd);
	return 1;
}

void *wait_to_close_server(void *servertcb) {
	
	pthread_detach(pthread_self());

	server_tcb_t *tcb = (server_tcb_t *)servertcb;
	
	sleep(CLOSEWAIT_TIMEOUT);

	if (tcb -> state == CLOSEWAIT){
		//printf("client disconnect to server, server port: %d, client port: %d\n", 
		//	tcb -> server_portNum, tcb -> client_portNum);
		tcb -> state = CLOSED;

		//clear buf
		memset(tcb -> recvBuf, 0, tcb -> usedBufLen);
		tcb -> usedBufLen = 0;
	}
	return NULL;
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

int server_get_sockfd(seg_t *segment) {
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



unsigned int get_time(){
  	struct timeval te; 
    gettimeofday(&te, NULL);
    unsigned int milliseconds = (te.tv_sec - prog_start_time.tv_sec)*1000 + (te.tv_usec - prog_start_time.tv_usec)/1000;
    return milliseconds;
}

void stcp_client_init(int conn) 
{
	gettimeofday(&prog_start_time, NULL);
  	int i;
  	for (i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++)
  		client_tcb_table[i] = NULL;
  	sip_conn = conn;
  	return;
}

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

int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	printf("client socket connecting %d\n", sockfd);
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
	printf("client socket connected %d\n", sockfd);
}

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

int stcp_client_disconnect(int sockfd) 
{
	printf("client socket disconnecting %d\n", sockfd);
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
	printf("client socket disconnected %d\n", sockfd);
}

int stcp_client_close(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL) 
		return -1;
	if (tcb -> state != CLOSED)
		return -1;
	
	free(tcb -> bufMutex);
	free(tcb);
	
	client_tcb_table[sockfd] = NULL;
	return 1;
}

int stcp_client_close_force(int sockfd) 
{
	if (sockfd < 0 || sockfd >= MAX_TRANSPORT_CONNECTIONS)
		return -1;
	client_tcb_t *tcb = client_tcb_table[sockfd];
	if (tcb == NULL) 
		return -1;
	free(tcb -> bufMutex);
	free(tcb);
	client_tcb_table[sockfd] = NULL;
	return 1;
}

int client_get_sockfd(seg_t *segment) {
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


void* seghandler(void* arg)
{
  	pthread_detach(pthread_self());

  	int sockfd;
	int connection_state;
	
	seg_t send_segment;
	seg_t recv_segment;
	while (true) {
		
		int src_nodeID;
		connection_state = sip_recvseg(sip_conn, &src_nodeID, &recv_segment);
		
		if (connection_state == -1) {
			continue;	//seg lost
		}
		
		int type = recv_segment.header.type;
		if (type == SYN || type == FIN || type == DATA) {
			sockfd = server_get_sockfd(&recv_segment);
			if (sockfd == -1) {
				continue;	//socket not found
			}
			server_tcb_t *tcb = server_tcb_table[sockfd];
			//SYN
			if (recv_segment.header.type == SYN) {

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
		if (type == SYNACK || type == FINACK || type == DATAACK) {
			sockfd = client_get_sockfd(&recv_segment);
			if (sockfd == -1) {
				printf("socket error\n");
				continue;
			}
			client_tcb_t *tcb = client_tcb_table[sockfd];
		
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
	
						tcb -> sendBufunSent -> sentTime = get_time();
						tcb -> unAck_segNum ++;
						tcb -> sendBufunSent = tcb -> sendBufunSent -> next;
					}
					pthread_mutex_unlock(tcb -> bufMutex);
				}
			}
		}
	}
}



void* sendBuf_timer(void* clienttcb) 
{

	pthread_detach(pthread_self());

	client_tcb_t* tcb = (client_tcb_t*)clienttcb;
	if (tcb == NULL)
		return NULL;
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
				
				if (resent_segBuf == NULL) {
					return NULL;
				}
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

	memset(segment, 0, sizeof(seg_t));
	
	segment -> header.src_port = src_port;
	segment -> header.dest_port = dest_port;
	segment -> header.length = length;
	segment -> header.type = type;
	segment -> header.checksum = 0;	
}

