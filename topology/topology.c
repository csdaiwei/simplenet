//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2013年1月

#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#include "topology.h"
#include "../common/constants.h"
#include "../common/bool.h"

#define MAX_HOST_NAME 100
#define MAX_LINE_NUM  100

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char *hostname) {
  
  struct hostent *host = gethostbyname(hostname);
  if (host == NULL)
    return -1;
  return host -> h_addr[3] & 0x000000ff;
}


//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr) {

  if (addr == NULL)
    return -1;
  return ntohl(addr -> s_addr) & 0x000000ff;  
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID() {

  char host_name[MAX_HOST_NAME];
  gethostname(host_name, MAX_HOST_NAME);
  return topology_getNodeIDfromname(host_name);
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum() {

  char host_name[MAX_HOST_NAME];
  gethostname(host_name, MAX_HOST_NAME);

  FILE *fp = fopen("topology/topology.dat", "r");
  char file_line[MAX_LINE_NUM];

  int neighbor_num = 0;
  while (fgets(file_line, MAX_LINE_NUM, fp) != NULL) {
    char *host_name_1 = strtok(file_line, " ");
    char *host_name_2 = strtok(NULL, " ");
    if (strcmp(host_name, host_name_1) == 0 || strcmp(host_name, host_name_2) == 0)
      neighbor_num ++;
  }
  fclose(fp);
  return neighbor_num;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum()
{ 
  int i;
  int num = 0;
  char hostname_array[MAX_NODE_NUM][MAX_HOST_NAME];

  FILE *fp = fopen("topology/topology.dat", "r");
  char file_line[MAX_LINE_NUM];

  while (fgets(file_line, MAX_LINE_NUM, fp) != NULL) {
    char *host_name_1 = strtok(file_line, " ");
    char *host_name_2 = strtok(NULL, " ");
    
    bool is_new = true;
    for (i = 0; i < num; ++i){
      if(strcmp(hostname_array[i], host_name_1) == 0)
        is_new = false;
    }
    if(is_new)
      strcpy(hostname_array[num++], host_name_1);

    is_new = true;
    for (i = 0; i < num; ++i){
      if(strcmp(hostname_array[i], host_name_2) == 0)
        is_new = false;
    }
    if(is_new)
      strcpy(hostname_array[num++], host_name_2);
  }
  fclose(fp);
  return num;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
int* topology_getNodeArray()
{
  int* node_array = (int *)malloc(sizeof(int) * topology_getNodeNum());

  int i;
  int num = 0;
  char hostname_array[MAX_NODE_NUM][MAX_HOST_NAME];

  FILE *fp = fopen("topology/topology.dat", "r");
  char file_line[MAX_LINE_NUM];

  while (fgets(file_line, MAX_LINE_NUM, fp) != NULL) {
    char *host_name_1 = strtok(file_line, " ");
    char *host_name_2 = strtok(NULL, " ");
    
    bool is_new = true;
    for (i = 0; i < num; ++i){
      if(strcmp(hostname_array[i], host_name_1) == 0)
        is_new = false;
    }
    if(is_new){
      strcpy(hostname_array[num], host_name_1);
      node_array[num] = topology_getNodeIDfromname(host_name_1);
      num ++;
    }

    is_new = true;
    for (i = 0; i < num; ++i){
      if(strcmp(hostname_array[i], host_name_2) == 0)
        is_new = false;
    }
    if(is_new){
      strcpy(hostname_array[num], host_name_2);
      node_array[num] = topology_getNodeIDfromname(host_name_2);
      num ++;
    }
  }
  fclose(fp);
  return node_array;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.
int *topology_getNbrArray() {
  
  int *neighbor_id_array = (int *)malloc(sizeof(int) * topology_getNbrNum());

  char host_name[MAX_HOST_NAME];
  gethostname(host_name, MAX_HOST_NAME);

  FILE *fp = fopen("topology/topology.dat", "r");
  char file_line[MAX_LINE_NUM];

  int num = 0;
  while (fgets(file_line, MAX_LINE_NUM, fp) != NULL) {
    char *host_name_1 = strtok(file_line, " ");
    char *host_name_2 = strtok(NULL, " ");
    if (strcmp(host_name, host_name_1) == 0) {
      neighbor_id_array[num] = topology_getNodeIDfromname(host_name_2);
      num ++;
    }
    if (strcmp(host_name, host_name_2) == 0) {
      neighbor_id_array[num] = topology_getNodeIDfromname(host_name_1);
      num ++;
    }   
  }
  fclose(fp);
  return neighbor_id_array;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID) {


  if(fromNodeID == toNodeID)
    return 0;

  FILE *fp = fopen("topology/topology.dat", "r");
  char file_line[MAX_LINE_NUM];
  
  while (fgets(file_line, MAX_LINE_NUM, fp) != NULL) {
    char *host_name_1 = strtok(file_line, " ");
    char *host_name_2 = strtok(NULL, " ");
    
    if ((topology_getNodeIDfromname(host_name_1) == fromNodeID && 
      topology_getNodeIDfromname(host_name_2) == toNodeID) ||
      (topology_getNodeIDfromname(host_name_2) == fromNodeID && 
      topology_getNodeIDfromname(host_name_1) == toNodeID)) {
      char *cost = strtok(NULL, " ");
      return (unsigned int)atoi(cost);
      }
  }
  fclose(fp);
  return INFINITE_COST;
}

unsigned int topology_getLocalIP() {
	char host_name[MAX_HOST_NAME];
	gethostname(host_name, MAX_HOST_NAME);
	struct hostent *host = gethostbyname(host_name);
	unsigned int local_ip = ((host -> h_addr[0] + 1 ) << 24) + (host -> h_addr[1] << 16) + (host -> h_addr[2] << 8);
	return local_ip;
}

/*
int main(){
  topology_getNodeNum();
}*/