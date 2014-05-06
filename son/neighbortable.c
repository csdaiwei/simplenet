//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2013年1月
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "neighbortable.h"
#include "../topology/topology.h"

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create()
{
  	int entry_num = topology_getNbrNum();
	nbr_entry_t *neighbor_table = (nbr_entry_t *)malloc(sizeof(nbr_entry_t) * nbr_entry_num);
	nbr_entry_t *neighbor_entry = neighbor_table;
	
	int *node_id_array = topology_getNbrArray();
	
	int i;
	for (i = 0; i < entry_num; i++) {
		neighbor_entry -> nodeID = node_id_array[i];
		neighbor_entry -> nodeIP = htonl(topology_getLocalIP() | node_id_array[i]);
		neighbor_entry -> conn = -1;
		neighbor_entry ++;
	}
	neighbor_entry = NULL;
	free(node_id_array);
	return neighbor_table;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
  	if (nt == NULL)
		return;
	nbr_entry_t *neighbor_entry = nt;
	
	int i;
	for (i = 0; i < nbr_entry_num; i ++) {
		if (nt -> conn != -1) {
			close(nt -> conn);
			nt -> conn = -1;
		}
		nt ++;
	}
	free(neighbor_entry);
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
  	int i;
	for (i = 0; i < nbr_entry_num; i ++) {
		if (nt -> nodeID == nodeID) {
			nt -> conn = conn;
			return 1;
		}
		nt ++;
	}
	return -1;

}



/** get the num of node whose nodeID is larger than host's node ID
 */
int get_accept_node_num(nbr_entry_t *nt, int nodeID) {
	int num = 0;
	
	int i;
	for (i = 0; i < nbr_entry_num; i ++) {
		if (nt -> nodeID > nodeID)
			num ++;
		nt ++;
	}
	return num;
}

/** accept the node or not
	return 1  accept
	return -1 not
 */
int need_accept_node(nbr_entry_t *nt, int nodeID, int neighborID) {
	
	int i;
	for (i = 0; i < nbr_entry_num; i ++) {
		if (neighborID <= nodeID)
			return -1;
		if (nt -> nodeID == neighborID)
			return 1;
		nt ++;
	}
	return -1;
}