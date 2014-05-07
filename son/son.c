//文件名: son/son.c
//
//描述: 这个文件实现SON进程
//SON进程首先连接到所有邻居, 然后启动listen_to_neighbor线程, 每个该线程持续接收来自一个邻居的进入报文, 并将该报文转发给SIP进程. 
//然后SON进程等待来自SIP进程的连接. 在与SIP进程建立连接之后, SON进程持续接收来自SIP进程的sendpkt_arg_t结构, 并将接收到的报文发送到重叠网络中.  
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/bool.h"
#include "../common/constants.h"
#include "../common/pkt.h"
#include "../topology/topology.h"
#include "neighbortable.h"
#include "son.h"


//你应该在这个时间段内启动所有重叠网络节点上的SON进程
#define SON_START_DELAY 10

/**************************************************************/
//声明全局变量
/**************************************************************/

int host_node_id;

//将邻居表声明为一个全局变量 
nbr_entry_t* nt; 
//将与SIP进程之间的TCP连接声明为一个全局变量
int sip_conn; 

/**************************************************************/
//实现重叠网络函数
/**************************************************************/

// 这个线程打开TCP端口CONNECTION_PORT, 等待节点ID比自己大的所有邻居的进入连接,
// 在所有进入连接都建立后, 这个线程终止.
void* waitNbrs(void* arg) {
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr, client_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(CONNECTION_PORT);

	bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	listen(listenfd, MAX_NODE_NUM);
	const int on = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	
	socklen_t client_len = sizeof(client_addr);

	int num = 0;
	int accept_num = get_accept_node_num(nt, host_node_id);
	int connection_fd;
	while (true) {
		
		if (num == accept_num)
			break;
		connection_fd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
		int client_node_id = topology_getNodeIDfromip(&client_addr.sin_addr);
		//the node_id need to accept.
		if (need_accept_node(nt, host_node_id, client_node_id) == 1) {
			printf("a neighbor has connected, nodeID :%d\n", client_node_id);
			nt_addconn(nt, client_node_id, connection_fd);
			num ++;
		}
	}
	close(listenfd);
	return NULL;
}

// 这个函数连接到节点ID比自己小的所有邻居.
// 在所有外出连接都建立后, 返回1, 否则返回-1.
int connectNbrs() {
	int socket_fd;
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(CONNECTION_PORT);

	nbr_entry_t *neighbor_entry = nt;
	int i;
	for (i = 0; i < nbr_entry_num; i ++) {
		socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (neighbor_entry -> nodeID < host_node_id) {
			//set server ip
			server_addr.sin_addr.s_addr = neighbor_entry -> nodeIP;
			int connd = connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
			if (connd < 0)
				return -1;
			printf("connected to a neighbor, nodeID: %d\n", neighbor_entry -> nodeID);
			neighbor_entry -> conn = socket_fd;
		}
		neighbor_entry ++;
	}
	return 1;
}

//每个listen_to_neighbor线程持续接收来自一个邻居的报文. 它将接收到的报文转发给SIP进程.
//所有的listen_to_neighbor线程都是在到邻居的TCP连接全部建立之后启动的.
void* listen_to_neighbor(void* arg) {
	nbr_entry_t *neighbor_entry = nt + *((int *)arg);
	while (true) {
		
		if (nt == NULL)
			return NULL;
		
		sip_pkt_t *recv_packet = (sip_pkt_t *)malloc(sizeof(sip_pkt_t));

		int connd = recvpkt(recv_packet, neighbor_entry -> conn);
		if (connd == -1)
			return NULL;
		forwardpktToSIP(recv_packet, sip_conn);
	}
}

//这个函数打开TCP端口SON_PORT, 等待来自本地SIP进程的进入连接. 
//在本地SIP进程连接之后, 这个函数持续接收来自SIP进程的sendpkt_arg_t结构, 并将报文发送到重叠网络中的下一跳. 
//如果下一跳的节点ID为BROADCAST_NODEID, 报文应发送到所有邻居节点.
void waitSIP() {
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr, client_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(SON_PORT);

	bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	listen(listenfd, MAX_NODE_NUM);
	const int on = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	socklen_t client_len = sizeof(client_addr);
	sip_conn = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
	close(listenfd);
	printf("SIP has connected to local SON network\n");
	
	while (true) {
		//receive a packet from SIP
		int next_node_id;
		sip_pkt_t *send_packet = (sip_pkt_t *)malloc(sizeof(sip_pkt_t));
		int connd = getpktToSend(send_packet, &next_node_id, sip_conn);
		if (connd == -1) {
			son_stop();
		}

		//send the packet to neighbors
		nbr_entry_t *neighbor_entry = nt;
		int i;
		for (i = 0; i < nbr_entry_num; i ++) {
			if (next_node_id == BROADCAST_NODEID || next_node_id == neighbor_entry -> nodeID)
				sendpkt(send_packet, neighbor_entry -> conn);
			neighbor_entry ++;
		}
	}
}

//这个函数停止重叠网络, 当接收到信号SIGINT时, 该函数被调用.
//它关闭所有的连接, 释放所有动态分配的内存.
void son_stop() {
	nt_destroy(nt);
	close(sip_conn);
	nt = NULL;
	printf("\nstop SON network\n");
	exit(0);
}

int main() {

	host_node_id = topology_getMyNodeID();
	nbr_entry_num = topology_getNbrNum();

	//启动重叠网络初始化工作
	printf("Overlay network: Node %d initializing...\n", host_node_id);	

	//创建一个邻居表
	nt = nt_create();
	//将sip_conn初始化为-1, 即还未与SIP进程连接.
	sip_conn = -1;
	
	//注册一个信号句柄, 用于终止进程
	signal(SIGINT, son_stop);

	//打印所有邻居
	int i;
	for(i=0;i<nbr_entry_num;i++) {
		printf("Overlay network: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//启动waitNbrs线程, 等待节点ID比自己大的所有邻居的进入连接
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//等待其他节点启动
	sleep(SON_START_DELAY);
	
	//连接到节点ID比自己小的所有邻居
	connectNbrs();

	//等待waitNbrs线程返回
	pthread_join(waitNbrs_thread,NULL);	

	//此时, 所有与邻居之间的连接都建立好了
	
	//创建线程监听所有邻居
	for(i=0;i<nbr_entry_num;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay network: node initialized...\n");
	printf("Overlay network: waiting for connection from SIP process...\n");

	//等待来自SIP进程的连接
	waitSIP();
	return 0;
}
