
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat. 
nbr_cost_entry_t* nbrcosttable_create(){

	int nbr_num = topology_getNbrNum();
	int* nbr_id_array = topology_getNbrArray();
	int local_id = topology_getMyNodeID();
	nbr_cost_entry_t* nct = (nbr_cost_entry_t*)malloc(sizeof(nbr_cost_entry_t) * nbr_num);
	
	assert(local_id != -1);

	int i;
	for (i = 0; i < nbr_num; ++i){
		nct[i].nodeID = nbr_id_array[i];
		nct[i].cost = topology_getCost(local_id, nct[i].nodeID);
	}

  	return nct;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct){
  	free(nct);
  	nct = NULL;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID){

	int nbr_num = topology_getNbrNum();	//make this var global ?
	int i;
	for (i = 0; i < nbr_num; ++i)
		if(nodeID == nct[i].nodeID)
			return nct[i].cost;
	return INFINITE_COST;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct){

	int i;
	int nbr_num = topology_getNbrNum();

	printf("==========nbr cost table (node %d)===========\n", topology_getMyNodeID());
	for (i = 0; i < nbr_num; ++i)
		printf("nbr id: %d\t\tcost: %d\n", nct[i].nodeID, nct[i].cost);

	printf("============================================\n");
}
