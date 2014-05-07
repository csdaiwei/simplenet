
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create(){


	int i, j;
	int nbr_num = topology_getNbrNum();
	int* nbr_id_array = topology_getNbrArray();
	int node_num = topology_getNodeNum();
	int* node_id_array = topology_getNodeArray();
	dv_t* dv = (dv_t*)malloc(sizeof(dv_t) * (nbr_num + 1));

	//dv[0] contains the distance vector of local node
	dv[0].nodeID = topology_getMyNodeID();
	dv[0].dvEntry = (dv_entry_t*)malloc(sizeof(dv_entry_t) * node_num);
	for (i = 0; i < node_num; ++i){
		dv[0].dvEntry[i].nodeID = node_id_array[i];
		dv[0].dvEntry[i].cost = topology_getCost(dv[0].nodeID, node_id_array[i]);
	}

	//neighbour distance vectors
	for (i = 0; i < nbr_num; ++i){
		dv[i + 1].nodeID = nbr_id_array[i];
		dv[i + 1].dvEntry = (dv_entry_t*)malloc(sizeof(dv_entry_t) * node_num);

		for (j = 0; j < node_num; ++j){
			dv[i + 1].dvEntry[j].nodeID = node_id_array[j];
			dv[i + 1].dvEntry[j].cost = INFINITE_COST;
		}
	}

  	return dv;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable){

	int i;
	int dv_num = topology_getNbrNum() + 1;
	for (i = 0; i < dv_num; ++i)
		free(dvtable[i].dvEntry);
	free(dvtable);
	dvtable = NULL;
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost){
  
	int i, j;
	int dv_num = topology_getNbrNum() + 1;
	int node_num = topology_getNodeNum();

	for (i = 0; i < dv_num; ++i)
		if(dvtable[i].nodeID == fromNodeID)
			break;
	if (i == dv_num)
		return -1;

	for (j = 0; j < node_num; ++j)
		if (dvtable[i].dvEntry[j].nodeID == toNodeID)
			break;
	if (j == node_num)
		return -1;

	dvtable[i].dvEntry[j].cost = cost;
  	return 1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
  	int i, j;
	int dv_num = topology_getNbrNum() + 1;
	int node_num = topology_getNodeNum();

	for (i = 0; i < dv_num; ++i)
		if(dvtable[i].nodeID == fromNodeID)
			break;
	if (i == dv_num)
		return INFINITE_COST;

	for (j = 0; j < node_num; ++j)
		if (dvtable[i].dvEntry[j].nodeID == toNodeID)
			break;
	if (j == node_num)
		return INFINITE_COST;

	return dvtable[i].dvEntry[j].cost;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable){

	int i, j;
	int dv_num = topology_getNbrNum() + 1;	//lines
	int node_num = topology_getNodeNum();	//rows
	
	printf("==========dvtable (node %d)==========\n", topology_getMyNodeID());

	printf("  nodeID");
	for (i = 0; i < node_num; ++i)
		printf("%8d", dvtable[0].dvEntry[i].nodeID);
	printf("\n");

	for (i = 0; i < dv_num; ++i){
		printf("%8d", dvtable[i].nodeID);
		
		for (j = 0; j < node_num; ++j)
			printf("%8d", dvtable[i].dvEntry[j].cost);
		printf("\n");
	}

	printf("====================================\n");

}

/*
int main(){
	dvtable_print(dvtable_create());
	return 0;
}
*/