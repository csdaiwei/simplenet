//�ļ���: client/stcp_client.c
//
//����: ����ļ�����STCP�ͻ��˽ӿ�ʵ�� 
//
//��������: 2013��1��

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

//����tcbtableΪȫ�ֱ���
client_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//������SIP���̵�TCP����Ϊȫ�ֱ���
int sip_conn;

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
    return;
}

// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
int stcp_client_sock(unsigned int client_port) 
{
	return 0;
}

// ��������������ӷ�����. �����׽���ID, �������ڵ�ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������ڵ�ID�ͷ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	return 0;
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
    return 0;
}

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
int stcp_client_disconnect(int sockfd) 
{
	return 0;
}

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
int stcp_client_close(int sockfd) 
{
	return 0;
}

// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
void* seghandler(void* arg) 
{
	return;
}


//����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
//���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �ͷ���һ�γ�ʱ�¼�.
//����ʱ�¼�����ʱ, ���·��������ѷ��͵�δ��ȷ�϶�. �����ͻ�����Ϊ��ʱ, ����߳̽���ֹ.
void* sendBuf_timer(void* clienttcb) 
{
	return;
}

