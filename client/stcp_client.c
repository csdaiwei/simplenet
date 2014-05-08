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
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
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

// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
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

// ��������������ӷ�����. �����׽���ID, �������ڵ�ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������ڵ�ID�ͷ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
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

// �������ݸ�STCP������. �������ʹ���׽���ID�ҵ�TCB���е���Ŀ.
// Ȼ����ʹ���ṩ�����ݴ���segBuf, �������ӵ����ͻ�����������.
// ������ͻ������ڲ�������֮ǰΪ��, һ����Ϊsendbuf_timer���߳̾ͻ�����.
// ÿ��SENDBUF_ROLLING_INTERVALʱ���ѯ���ͻ������Լ���Ƿ��г�ʱ�¼�����. 
// ��������ڳɹ�ʱ����1�����򷵻�-1. 
// stcp_client_send��һ����������������.
// ��Ϊ�û����ݱ���ƬΪ�̶���С��STCP��, ����һ��stcp_client_send���ÿ��ܻ�������segBuf
// ����ӵ����ͻ�����������. ������óɹ�, ���ݾͱ�����TCB���ͻ�����������, ���ݻ������ڵ����,
// ���ݿ��ܱ����䵽������, ���ڶ����еȴ�����.
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

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
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

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
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

// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
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
					
					printf("free segment in buf, seq %d\n", bufhead -> seg.header.seq_num);
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


//����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
//���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �ͷ���һ�γ�ʱ�¼�.
//����ʱ�¼�����ʱ, ���·��������ѷ��͵�δ��ȷ�϶�. �����ͻ�����Ϊ��ʱ, ����߳̽���ֹ.
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

			printf("data segment timeout, seq %d\n", (tcb -> sendBufHead -> seg).header.seq_num);
			
			pthread_mutex_lock(tcb -> bufMutex);
			segBuf_t* resent_segBuf = tcb -> sendBufHead;
			int unAck_segNum = tcb -> unAck_segNum;
			int i;
			for ( i = 0; i < unAck_segNum; ++i){
				
				assert(resent_segBuf != NULL);
				sip_sendseg(sip_conn, tcb -> server_nodeID, &(resent_segBuf -> seg));
				printf("resend the data segment, seq %d\n", (resent_segBuf -> seg). header.seq_num);

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