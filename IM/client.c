#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "stcp.h"
#include "client.h"
#include "../common/bool.h"
#include "common/queue.h"
#include "common/network.h"
#include "common/protocol.h"
#include "../topology/topology.h"

#define BUF_SIZE 256

#define SERVER_NODE_ID 128
#define SERVER_PORT		80
#define CLIENT_PORT		90
//SIGINT
int sip_conn;

int client_node_id;

int send_sock_fd;
int recv_sock_fd;

char sendbuf[BUF_SIZE];	/*global send buffer, flushed before each send preparations*/
char recvbuf[BUF_SIZE];	/*global receive buffer, flushed before each recieving packets*/
char username[20];		/*global username, never change after login*/

struct user_queue *online_friend_queue;
struct message recent_messages[5];
int message_num = 0;

void client_init() {

	client_node_id = topology_getMyNodeID();

	pthread_t tid;
	pthread_create(&tid, NULL, seghandler, NULL);
	stcp_client_init(sip_conn);
	send_sock_fd = stcp_client_sock(client_node_id - SERVER_NODE_ID + CLIENT_PORT);
	if (stcp_client_connect(send_sock_fd, SERVER_NODE_ID, client_node_id - SERVER_NODE_ID + SERVER_PORT) == -1)
		printf("connect error\n");
	else printf("connect one\n");
	stcp_server_init(sip_conn);
	if ((recv_sock_fd = stcp_server_sock(client_node_id - SERVER_NODE_ID + CLIENT_PORT)) < 0)
		printf("recv error\n");
	else printf("recv one\n");
	stcp_server_accept(recv_sock_fd);
}

void client_close() {
	if(stcp_client_disconnect(send_sock_fd)<0) {
		printf("fail to disconnect from stcp server\n");
		exit(1);
	}
	if(stcp_client_close(send_sock_fd)<0) {
		printf("fail to close stcp client\n");
		exit(1);
	}
	if(stcp_server_close_force(recv_sock_fd) < 0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}
}

int main(int argc, char const *argv[]) {
	
	sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("fail to connect to the local SIP process\n");
		exit(1);
	}
	client_init();
	signal(SIGINT, client_close);
	
	online_friend_queue = init_user_queue();
	login();

	pthread_t tid;
	int rc = pthread_create(&tid, NULL, recv_packet_thread, NULL);
	if(rc){
		printf("ERROR creating thread, tid %d, return code %d\n", (int)tid, rc);
		exit(-1);
	}
	/*main thread loads the next page to get user's command*/
	system("clear");
	print_prompt_words(username);
	while(true){
		printf("enter >> ");
		char command[20];
		get_keyboard_input(command, 20);
		if(command[0] != '-'){
			printf("input error, you can try again.\n");
			continue;
		}
		if(strcmp(&command[1], "list") == 0) {
			assert(online_friend_queue -> front != NULL);
			print_online_friends(online_friend_queue);
			continue;

		} else if(strcmp(&command[1], "clear") == 0){
			system("clear");
			print_prompt_words(username);
			continue;

		} else if(strcmp(&command[1], "logout") == 0){
			logout();
			break;//this lead to the end of the program.

		} else if(strcmp(&command[1], "recent") == 0){
			print_recent_messages(recent_messages, message_num);
			continue;
		}else if(strcmp(&command[1], "chat") == 0){
			/*bug:if someone's name is "all", bug happens*/
			char recipient[20];
			printf("Who do you want to talk to?\n" 
				"Type he/she's name or type \"all\":");
			get_keyboard_input(recipient, 20);
			if(strcmp(recipient, username) == 0){
				printf("it's funny to talk to yourself.\n");
			} 
			else if(strcmp(recipient, "all") == 0){
				char text[100];
				printf("Input your words to say to %s.\n"
					   "Within 100 characters, end up with the Enter key):\n", recipient);
				get_keyboard_input(text, 100);

				/*build and send the packet*/
				memset(sendbuf, 0, sizeof(sendbuf));
				unsigned short data_size = 20 + 100;
				construct_im_pkt_head((struct im_pkt_head *)sendbuf, TYPE_REQUEST, SERVICE_MULTI_MESSAGE, data_size);
				strncpy(&sendbuf[IM_PKT_HEAD_SIZE], username, 20);
				strncpy(&sendbuf[IM_PKT_HEAD_SIZE + 20], text, 100);
				stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + data_size);
			} else if(find_user_by_name(online_friend_queue, recipient) != NULL){

				char text[100];
				printf("Input your words to say to %s.\n"
					   "Within 100 characters, end up with the Enter key):\n", recipient);
				get_keyboard_input(text, 100);

				/*build and send the packet*/
				memset(sendbuf, 0, sizeof(sendbuf));
				unsigned short data_size = 20 + 20 + 100;
				construct_im_pkt_head((struct im_pkt_head *)sendbuf, TYPE_REQUEST, SERVICE_SINGLE_MESSAGE, data_size);
				strncpy(&sendbuf[IM_PKT_HEAD_SIZE], username, 20);
				strncpy(&sendbuf[IM_PKT_HEAD_SIZE + 20], recipient, 20);
				strncpy(&sendbuf[IM_PKT_HEAD_SIZE + 40], text, 100);
				stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + data_size);

			} else{
				printf("sorry, %s seems not online now\n", recipient);
				print_online_friends(online_friend_queue);
			}
			continue;
		}  else{
			printf("input error, you can try again.\n");
			continue;	
		}
	}
	/*close socket and stop another thread before exit*/
	pthread_cancel(tid);
	client_close();
	return 0;
}

/*send a login request to server*/
void 
login(){

	system("clear");//unused warnning

	printf(	"Hello, this is an IM program.\n" 
			"You may pick a nickname and login.\n"
			"==========================================\n");
	while(true){
		while(true){
			printf("Your name (no more than 15 characters):");
			if(get_keyboard_input(username, 20) >= 4)
				break;
			else
				printf("It's too short.\n");
		}
		/*construct a login request packet and send it*/
		memset(sendbuf, 0, sizeof(sendbuf));
		unsigned short data_size = 20;	//request im packet data size
		construct_im_pkt_head((struct im_pkt_head *)sendbuf, TYPE_REQUEST, SERVICE_LOGIN, data_size);
		concat_im_pkt_data((struct im_pkt_head *)sendbuf, username);
		stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + data_size);

		/*get and parse the response packet*/
		memset(recvbuf, 0 ,sizeof(recvbuf));
		int n = readvrec(recv_sock_fd, recvbuf, BUF_SIZE);
		struct im_pkt_head *response_head = (struct im_pkt_head *)recvbuf;
		if( (n > 0) && (response_head -> service == SERVICE_LOGIN)){	//the response data size is 1
			char *response_data = (char *)(response_head + 1);//right after the head
			bool login_success = (bool) response_data[0];
			if(login_success){
				printf("log success\n");
				query_online_all();
				break;
			}
			else{	//login falied. name repeat
				printf(	"\nSorry, that name seems to have been taken, pick another one!\n");
				continue;
			}
		}
		printf("Sorry ,the server seems overloaded, wait a moment and try again.\n");
		exit(-1);
	}
}


void
logout(){

	/*build a logout request packet and send it*/
	memset(sendbuf, 0, sizeof(sendbuf));
	int data_size = 0;
	construct_im_pkt_head((struct im_pkt_head *)sendbuf, TYPE_REQUEST, SERVICE_LOGOUT, data_size);
	stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + data_size);
	
}

/*query for all online friends' names*/
void
query_online_all(){
	/*build a query packet*/
	memset(sendbuf, 0, sizeof(sendbuf));
	int data_size = 0;
	construct_im_pkt_head((struct im_pkt_head *)sendbuf, TYPE_REQUEST, SERVICE_QUERY_ONLINE, data_size);
	stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + data_size);

	/*get and parse the response packet*/
	memset(recvbuf, 0 ,sizeof(recvbuf));
	readvrec(recv_sock_fd, recvbuf, BUF_SIZE);
	struct im_pkt_head *response_head = (struct im_pkt_head *)recvbuf;
	assert((response_head -> type == TYPE_RESPONSE) && 
		(response_head -> service == SERVICE_QUERY_ONLINE));

	/*add all user into online_friend_queue*/
	int i;
	for (i = 0; i < response_head -> data_size / 20; ++i){
		struct user_node *pnode = init_user_node((char *)(response_head + 1) + 20 * i, -1, -1);
		enqueue(online_friend_queue, pnode);
	}
}

/*a thread created after login, to receive all packets from server
 *the main thread will no more receive packet after this thread created*/
void *
recv_packet_thread(void *this_is_no_use){

	int n = 0;	//number received 
	while( (n = readvrec(recv_sock_fd, recvbuf, BUF_SIZE)) > 0){
		struct im_pkt_head *response_head = (struct im_pkt_head *)recvbuf;
		char *response_data = (char *)(response_head + 1);
		memset(sendbuf, 0, sizeof(sendbuf));
		if(response_head -> type != TYPE_RESPONSE){
			printf("\nReceived a error packet, drop it.\n enter >> ");
			break;
		}
		switch(response_head -> service){
			
			case SERVICE_SINGLE_MESSAGE: ;
				char *sender = response_data;
				char *text = response_data + 40;/*two names' length*/
				printf("\nMessage comes from %s:\n \t%s\nenter >> ", sender,text);
				fflush(stdout);
				save_recent_messages(sender, text);
				break;
			case SERVICE_MULTI_MESSAGE: ;
				sender = response_data;
				text = response_data + 20;/*one name's length*/
				printf("\nMessage comes from %s:\n \t%s\nenter >> ", sender,text);
				fflush(stdout);
				save_recent_messages(sender, text);
				break;
			case SERVICE_ONLINE_NOTIFY: ;
				char *that_online_username = response_data;
				if(find_user_by_name(online_friend_queue, that_online_username) == NULL){
					struct user_node *n = init_user_node(that_online_username, -1, -1);
					enqueue(online_friend_queue, n);
					printf("\n User %s get online!\nenter >> ", that_online_username);
					fflush(stdout);
				}
				break;
			case SERVICE_OFFLINE_NOTIFY: ;
				char *that_offline_username = response_data;
				delete_user_by_name(online_friend_queue, that_offline_username);
				printf("\n User %s get offline!\nenter >> ", response_data);
				fflush(stdout);
				break;
			default:
				printf("\nReceived a error packet, drop it.\n enter >> ");break;
		}
		memset(recvbuf, 0, sizeof(recvbuf));
	}
	if( n < 0)
		printf("Read error\n");
	pthread_exit(NULL);
}

void save_recent_messages(char *sender, char *text){
	
	int subscript = message_num % 5;
	strcpy(recent_messages[subscript].sender, sender);
	strcpy(recent_messages[subscript].text, text);
	message_num ++;
}



