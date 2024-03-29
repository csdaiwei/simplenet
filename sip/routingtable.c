#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/bool.h"
#include "../common/constants.h"
#include "../topology/topology.h"
#include "routingtable.h"

//makehash()是由路由表使用的哈希函数.
//它将输入的目的节点ID作为哈希键,并返回针对这个目的节点ID的槽号作为哈希值.
int makehash(int node){

  	return node % MAX_ROUTINGTABLE_SLOTS;
}

//这个函数动态创建路由表.表中的所有条目都被初始化为NULL指针.
//然后对有直接链路的邻居,使用邻居本身作为下一跳节点创建路由条目,并插入到路由表中.
//该函数返回动态创建的路由表结构.
routingtable_t* routingtable_create(){

	routingtable_t* rt = (routingtable_t*)malloc(sizeof(routingtable_t));
	int i;
	for (i = 0; i < MAX_ROUTINGTABLE_SLOTS; ++i)
		rt -> hash[i] = NULL;

	//add route info of direct neighbours
	int nbr_num = topology_getNbrNum();
	int* nbr_array = topology_getNbrArray();
	for (i = 0; i < nbr_num; ++i)
		routingtable_setnextnode(rt, nbr_array[i], nbr_array[i]);

	free(nbr_array);
  	return rt;
}

/*destroy a routing list*/
void routinglist_destroy(routingtable_entry_t *head){
	if(head == NULL)
		return;
	routinglist_destroy(head -> next);
	free(head);
	head = NULL;
}

//这个函数删除路由表.
//所有为路由表动态分配的数据结构将被释放.
void routingtable_destroy(routingtable_t* routingtable){

	int i;
	for ( i = 0; i < MAX_ROUTINGTABLE_SLOTS; ++i)
		routinglist_destroy(routingtable -> hash[i]);
	free(routingtable);
	routingtable = NULL;
}



//这个函数使用给定的目的节点ID和下一跳节点ID更新路由表.
//如果给定目的节点的路由条目已经存在, 就更新已存在的路由条目.如果不存在, 就添加一条.
//路由表中的每个槽包含一个路由条目链表, 这是因为可能有冲突的哈希值存在(不同的哈希键, 即目的节点ID不同, 可能有相同的哈希值, 即槽号相同).
//为在哈希表中添加一个路由条目:
//首先使用哈希函数makehash()获得这个路由条目应被保存的槽号.
//然后将路由条目附加到该槽的链表中.
void routingtable_setnextnode(routingtable_t* routingtable, int destNodeID, int nextNodeID)
{
	int slot = makehash(destNodeID);
	bool is_updated = false;

	routingtable_entry_t* listhead = routingtable -> hash[slot];
	routingtable_entry_t* listtail = NULL;

	if(listhead == NULL){
		//no route info in this slot
		routingtable -> hash[slot] = (routingtable_entry_t*)malloc(sizeof(routingtable_entry_t));
		routingtable -> hash[slot] -> destNodeID = destNodeID;
		routingtable -> hash[slot] -> nextNodeID = nextNodeID;
		routingtable -> hash[slot] -> next = NULL;
	}
	else{
		//find and update
		routingtable_entry_t* p = listhead;
		for (; p != NULL; p = p -> next){
			if(p -> destNodeID == destNodeID){
				p -> nextNodeID = nextNodeID;
				is_updated = true;
				break;
			}
			if( p -> next == NULL)
				listtail = p;
		}

		//not found, add to tail
		if(!is_updated){
			p = (routingtable_entry_t*)malloc(sizeof(routingtable_entry_t));
			p -> destNodeID = destNodeID;
			p -> nextNodeID = nextNodeID;
			p -> next = NULL;
			assert(listtail != NULL);
			listtail -> next = p;
		}
	}
}

//这个函数在路由表中查找指定的目标节点ID.
//为找到一个目的节点的路由条目, 你应该首先使用哈希函数makehash()获得槽号,
//然后遍历该槽中的链表以搜索路由条目.如果发现destNodeID, 就返回针对这个目的节点的下一跳节点ID, 否则返回-1.
int routingtable_getnextnode(routingtable_t* routingtable, int destNodeID){

	int slot = makehash(destNodeID);
	routingtable_entry_t* listhead = routingtable -> hash[slot];
	if(listhead != NULL){
		routingtable_entry_t* p = listhead;
		for (; p != NULL; p = p -> next)
			if(p -> destNodeID == destNodeID)
				return p -> nextNodeID;
	}
	return -1;
}

void routinglist_print(routingtable_entry_t* head){
	if(head == NULL)
		return;
	printf("%11d%14d\n", head -> destNodeID, head -> nextNodeID);	
	routinglist_print(head -> next);
}

//这个函数打印路由表的内容
void routingtable_print(routingtable_t* routingtable){

	printf("==========routingtable (node %d)==========\n", topology_getMyNodeID());
	printf(" destNodeID    nextNodeID\n");
	int i;
	for ( i = 0; i < MAX_ROUTINGTABLE_SLOTS; ++i)
		routinglist_print(routingtable -> hash[i]);
	printf("==========================================\n");
}

/*
int main(int argc, char const *argv[])
{
	printf("test main\n");
	routingtable_print(routingtable_create());
	return 0;
}
*/