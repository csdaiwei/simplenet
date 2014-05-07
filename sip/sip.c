//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/bool.h"
#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

#define LOACLHOST "127.0.0.1"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 60

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() { 
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in local_server;
	memset(&local_server, 0, sizeof(local_server));
	local_server.sin_family = AF_INET;
	local_server.sin_addr.s_addr = inet_addr(LOACLHOST);
	local_server.sin_port = htons(SON_PORT);

	int connd = connect(socket_fd, (struct sockaddr *)&local_server, sizeof(local_server));
	if (connd < 0)
		return -1;

	return socket_fd;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
	sip_pkt_t sip_packet;
	sip_packet.header.src_nodeID = topology_getMyNodeID();
	sip_packet.header.dest_nodeID = BROADCAST_NODEID;
	sip_packet.header.type = ROUTE_UPDATE;
	
	pkt_routeupdate_t packet_route_uptate;
	packet_route_uptate.entryNum = topology_getNbrNum();
	int host_node_id = topology_getMyNodeID();
	int *neighbor_node_id_array = topology_getNbrArray();
	int i;
	for (i = 0; i < packet_route_uptate.entryNum; i ++) {
		packet_route_uptate.entry[i].nodeID = neighbor_node_id_array[i];
		packet_route_uptate.entry[i].cost = topology_getCost(host_node_id, neighbor_node_id_array[i]);
	}
	sip_packet.header.length = sizeof(unsigned int) * (2 * packet_route_uptate.entryNum + 1);
	memcpy(sip_packet.data, &packet_route_uptate, sip_packet.header.length);
	

	int connd;
	while (true) {
		connd = son_sendpkt(BROADCAST_NODEID, &sip_packet, son_conn);
		if (connd == -1)
			break;

		printf("Routing: broadcast a route update packet to all neighbors\n");
		
		/*
		printf("====================\n");
		printf("entryNum:\t%d\n", packet_route_uptate.entryNum);
		for (i = 0; i < packet_route_uptate.entryNum; i++) {
			printf("nodeID:\t\t%d\n", neighbor_node_id_array[i]);
			printf("cost:\t\t%d\n", packet_route_uptate.entry[i].cost);
		}
		printf("====================\n");*/

		sleep(ROUTEUPDATE_INTERVAL);
	}
	return NULL;
}

//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {
	
	sip_pkt_t pkt;
	memset(&pkt, 0, sizeof(sip_pkt_t));

	while(son_recvpkt(&pkt, son_conn) > 0) {

		printf("get a packet from %d, to %d, ", pkt.header.src_nodeID, pkt.header.dest_nodeID);

		// route update packet
		if (pkt.header.type == ROUTE_UPDATE){
		
			pkt_routeupdate_t *packet_route_uptate = (pkt_routeupdate_t *)((char *)(&pkt) + SIP_HEADER_LEN);
			
			printf("it's a route update packet.\n");
			/*
			printf("Routing: received a route update packet from neighbor %d\n", pkt.header.src_nodeID);
			printf("====================\n");
			printf("entryNum:\t%d\n", packet_route_uptate -> entryNum);
			int i;
			for (i = 0; i < packet_route_uptate -> entryNum; i++) {
				printf("nodeID:\t\t%d\n", packet_route_uptate -> entry[i].nodeID);
				printf("cost:\t\t%d\n", packet_route_uptate -> entry[i].cost);
			}
			printf("====================\n");*/
		}

		if (pkt.header.type == SIP){
		
			printf("it's a sip packet.\n");
		}
	}

	close(son_conn);
	son_conn = -1;
	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {

	nbrcosttable_destroy(nct);
	
	close(son_conn);
	exit(0);
	return;
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接. // ?
void waitSTCP() {


	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr, client_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(SIP_PORT);

	bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	listen(listenfd, MAX_NODE_NUM);
	const int on = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	socklen_t client_len = sizeof(client_addr);
	stcp_conn = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
	close(listenfd);
	printf("STCP has connected to local SIP network\n");
	
	while (true) {

		//receive a packet from STCP
		//getsegToSend...
		sleep(1);
	}

}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, pls wait...\n");

	//初始化全局变量
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	//routingtable = routingtable_create();
	//routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	//pthread_mutex_init(routingtable_mutex,NULL);
	
	son_conn = -1;
	stcp_conn = -1;

	nbrcosttable_print(nct);
	//dvtable_print(dv);
	//routingtable_print(routingtable);

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程 
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);		
	}
	
	//启动线程处理来自SON进程的进入报文 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("SIP layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	//routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP(); 

}


