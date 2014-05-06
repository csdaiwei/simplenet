#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

#include "seg.h"
#include "bool.h"

int readn(int socket_fd, char *buf, int len, int flag) {
	int count = len;
	int read_count;
	while (count > 0) {
		read_count = recv(socket_fd, buf, count, flag);
		if (read_count < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (read_count == 0)
			return 0;
		buf += read_count;
		count -= read_count;
	}
	return len;
}

//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回1,否则返回-1.
int sip_sendseg(int sip_conn, int dest_nodeID, seg_t* segPtr)
{
	/*
    int connection_state;
	connection_state = send(connection, "!&", 2, 0);
	if (connection_state <= 0) return -1;
	
	segPtr -> header.checksum = checksum(segPtr);

	connection_state = send(connection, segPtr, segPtr -> header.length + SEG_HEAD_LEN, 0);
	if (connection_state <= 0) return -1;
	
	connection_state = send(connection, "!#", 2, 0);
	if (connection_state <= 0) return -1;
	return 1;*/
}

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int sip_recvseg(int sip_conn, int* src_nodeID, seg_t* segPtr)
{
  	
  	/*int connection_state;
	char recv_begin[2];
	char recv_end[2];
	//receive the begin "!&"
	while (true) {
		connection_state = readn(connection, recv_begin, 2, 0);
		if (strncmp(recv_begin, "!&", 2) == 0)
			break;
		if (connection_state <= 0) return -1;
	}
	
	//receive the segment header
	connection_state = readn(connection, (char *)segPtr, SEG_HEAD_LEN, 0);
	if (connection_state <= 0) return -1;
	//receive the segment data	
	if (segPtr -> header.length != 0) {
		connection_state = readn(connection, (char *)segPtr + SEG_HEAD_LEN, segPtr -> header.length, 0);
		if (connection_state <= 0) return -1;
	}
	//receive the end "!#"
	connection_state = readn(connection, recv_end, 2, 0);
	if (connection_state <= 0) return -1;
	if (strncmp(recv_end, "!#", 0) != 0) return -1;


	int is_lost = seglost(segPtr);	//1 means lost

	if(is_lost == 0 && checkchecksum(segPtr) != 1){	
		is_lost = 1;//checksum failed, set lost.
		printf("checkchecksum failed\n");
	}

	return is_lost;*/
}

//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
  return 0;
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
  return 0;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr)
{
  	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		
		if(rand()%2==0) {
			printf("seg lost!!!\n");
      		return 1;
		}
		else {
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
			int errorbit = rand()%(len*8);
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			printf("error bit!!!\n");
			return 0;
		}
	}
	return 0;
}

//这个函数计算指定段的校验和.
//校验和计算覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
  
	segment -> header.checksum = 0;	
	int size = segment -> header.length + sizeof(stcp_hdr_t);
		
	unsigned short* p = (unsigned short*)segment;
	unsigned long checksum = 0;

	//sum
	while(size > 1){
		checksum += *p++;
		size -= 2;
	}
	if (size > 0) {
		checksum += *(unsigned char *)p;
	}
	while (checksum >> 16) 
		checksum = (checksum & 0xffff) + (checksum >> 16);

	return (unsigned short)~checksum;
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1.
int checkchecksum(seg_t* segment)
{
 	 /*almost the same with checksum(seg_t*)*/
	int size = segment -> header.length + sizeof(stcp_hdr_t);

	unsigned short* p = (unsigned short*)segment;
	unsigned int checksum = 0;

	//sum
	while(size > 1){
		checksum += *p++;
		size -= 2;
	}
	if (size > 0) {
		checksum += *(unsigned char *)p;
	}
	while (checksum >> 16) 
		checksum = (checksum & 0xffff) + (checksum >> 16);

	return ((unsigned short)~checksum == 0) ? 1 : -1;
}
