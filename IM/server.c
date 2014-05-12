#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include "stcp.h"
#include "server.h"
#include "common/queue.h"
#include "common/network.h"
#include "../common/bool.h"
#include "../topology/topology.h"

#define BUF_SIZE 256

#define SERVER_NODE_ID 128
#define SERVER_PORT		80
#define CLIENT_PORT		90

int sip_conn;

int *client_node_array;
int node_num = 0;

struct user_queue *online_user_queue = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char const *argv[]) {
	
	sip_conn = connectToSIP();
	
	online_user_queue = init_user_queue();
	server_init();
	
	while (true) {
		sleep(60);
	}

	return 0;
}

void server_init() {

	client_node_array = topology_getNodeArray();
	node_num = topology_getNodeNum();
	
	pthread_t tid;
	pthread_create(&tid, NULL, seghandler, NULL);
	stcp_server_init(sip_conn);
	stcp_client_init(sip_conn);

	int i;
	for (i = 0; i < node_num; i ++)
		if (client_node_array[i] != SERVER_NODE_ID) {
			pthread_t tid;
			pthread_create(&tid, NULL, server_accept_thread, &client_node_array[i]);
		}
}

void *server_accept_thread(void *arg) {
	int client_node_id = *((int *)arg);

	int recv_sock_fd = stcp_server_sock(client_node_id - SERVER_NODE_ID + SERVER_PORT);
	if (stcp_server_accept(recv_sock_fd) == -1)
		printf("recv error\n");
	else printf("recv one\n");
	printf("client node id:%d\n", client_node_id);
	usleep(50);
	int send_sock_fd = stcp_client_sock(client_node_id - SERVER_NODE_ID + CLIENT_PORT);
	if (stcp_client_connect(send_sock_fd, client_node_id, client_node_id - SERVER_NODE_ID + CLIENT_PORT) < 0)
		printf("connect error\n");
	else printf("connect one\n");
	struct socket_fd_t *socket_fd = (struct socket_fd_t *)malloc(sizeof(struct socket_fd_t));
	socket_fd -> send_sock_fd = send_sock_fd;
	socket_fd -> recv_sock_fd = recv_sock_fd;
	socket_fd -> client_node_id = client_node_id;
	pthread_t tid;
	pthread_create(&tid, NULL, client_handler, (void *)socket_fd);
	return NULL;
}

void *client_handler(void * connfd){

	int n;	/*number received*/
	char username[20];	//the user's name connecting to this thread. will be filled when login
	int status = -1;	//the user's status
	int send_sock_fd = ((struct socket_fd_t *)connfd) -> send_sock_fd;
	int recv_sock_fd = ((struct socket_fd_t *)connfd) -> recv_sock_fd;
	int client_node_id = ((struct socket_fd_t *)connfd) -> client_node_id;
	free(connfd);

	assert(online_user_queue != NULL);
	/*each thread has its own buf*/
	char sendbuf[BUF_SIZE];
	char recvbuf[BUF_SIZE];
	
	/*Receive the request im packet by two steps.
	 *Firstly receive the head field, secondly receive the data field.
	 *Then handle the request packet (usually by a response packet)*/
	while( (n = readvrec(recv_sock_fd, recvbuf, BUF_SIZE)) > 0){
		struct im_pkt_head *request_head = (struct im_pkt_head *)recvbuf;
		struct im_pkt_head *response_head = (struct im_pkt_head *)sendbuf;
		char *request_data = (char *)(request_head + 1);
		int response_data_size = 0;

		memset(sendbuf, 0, sizeof(sendbuf));
		if(request_head -> type != TYPE_REQUEST){
			printf("Received a error packet, drop it.\n");
			break;
		}
		
		switch(request_head -> service){
			case SERVICE_LOGIN: ;
				/*response a login response packet, 
				 *it contains only 1 byte data to indicate that login succeeded or failed*/
				response_data_size = 1;
				bool login_result;
				strncpy(username, (char *)(request_head + 1), 20);
				
				//check repeat
				pthread_mutex_lock(&mutex);
				if(find_user_by_name( online_user_queue, username) == NULL){
					struct user_node *pnode = init_user_node(username, send_sock_fd, recv_sock_fd);
					enqueue(online_user_queue, pnode);
					status = ONLINE_STATUS;
					printf("\tuser %s login.\n", username);
					login_result = true;
					/*notify all othre users*/
					on_off_line_notify(SERVICE_ONLINE_NOTIFY, username, sendbuf);
				}
				else{
					login_result = false;
				}
				pthread_mutex_unlock(&mutex);
				construct_im_pkt_head(response_head, TYPE_RESPONSE, SERVICE_LOGIN, response_data_size);
				concat_im_pkt_data(response_head, (char *)&login_result);
				stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + response_data_size);
				break;
			case SERVICE_LOGOUT: ;
				/*nothing need to do here.*/
				break;
			case SERVICE_QUERY_ONLINE: ;
				/*copy all usernames into the data field of the response packet*/
				pthread_mutex_lock(&mutex);
				response_data_size = 20 * copy_all_user_name((char *)(response_head + 1), online_user_queue);
				pthread_mutex_unlock(&mutex);
				construct_im_pkt_head(response_head, TYPE_RESPONSE, SERVICE_QUERY_ONLINE, response_data_size);
				/*send the packet*/
				stcp_client_send(send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + response_data_size);
				break;
			case SERVICE_SINGLE_MESSAGE: ;
				/*simply resend the packet to the recipient*/
				char *recipient = request_data + 20;
				struct user_node *recipient_node = find_user_by_name(online_user_queue, recipient); 
				if(recipient_node != NULL){
					response_data_size = request_head -> data_size;
					construct_im_pkt_head(response_head, TYPE_RESPONSE, SERVICE_SINGLE_MESSAGE, response_data_size);
					concat_im_pkt_data(response_head, request_data);
					stcp_client_send(recipient_node -> send_sock_fd, sendbuf,IM_PKT_HEAD_SIZE + response_data_size);
					printf("\tuser %s send a message to %s, text %s\n", username, recipient, request_data + 40);
				}else
					printf("Error, no recipient %s. drop the packet\n", recipient);
				break;
			case SERVICE_MULTI_MESSAGE: ;
				response_data_size = request_head -> data_size;
				construct_im_pkt_head(response_head, TYPE_RESPONSE, SERVICE_MULTI_MESSAGE, response_data_size);
				concat_im_pkt_data(response_head, request_data);
				char *sender = request_data;
				/*send to all online users except this message's sender*/
				for(recipient_node = online_user_queue -> front; recipient_node != NULL; recipient_node = recipient_node -> next)
					if(strcmp(recipient_node -> username, sender) != 0)
						stcp_client_send(recipient_node -> send_sock_fd, sendbuf,IM_PKT_HEAD_SIZE + response_data_size);
				printf("\tuser %s send a multi-message, text %s\n", username, request_data + 20);
				break;
			default:
				printf("Received a error packet, drop it.\n");break;
		}
		memset(recvbuf, 0, sizeof(recvbuf));
	}
	if( n < 0)
		printf("Read error\n");
	if(status == ONLINE_STATUS){
		/*remove logout or disconnected  users from the queue*/
		on_off_line_notify(SERVICE_OFFLINE_NOTIFY, username, sendbuf);
		pthread_mutex_lock(&mutex);
		delete_user_by_name(online_user_queue, username);
		pthread_mutex_unlock(&mutex);
		status = -1;
		printf("\tuser %s logout\n", username);
	}

	if(stcp_server_close_force(recv_sock_fd) < 0) {
		printf("can't destroy stcp server\n");
	}
	if(stcp_client_close_force(send_sock_fd) < 0) {
		printf("fail to close stcp client\n");
	}
	int i;
	for (i = 0; i < node_num; i++)
		if (client_node_id == client_node_array[i]) {
			pthread_t tid;
			pthread_create(&tid, NULL, server_accept_thread, &client_node_array[i]);
			break;
		}
	return NULL;
}

/*notify all others when a user login/logout*/
void 
on_off_line_notify(int SERVICE_ON_OFF, char *username, char *sendbuf){

	assert((SERVICE_ON_OFF == SERVICE_ONLINE_NOTIFY) ||
		(SERVICE_ON_OFF == SERVICE_OFFLINE_NOTIFY));
	/*build a notif packet*/
	int response_data_size = 20;
	struct im_pkt_head *response_head = (struct im_pkt_head *)sendbuf;
	memset(sendbuf, 0, BUF_SIZE);
	construct_im_pkt_head(response_head, TYPE_RESPONSE, SERVICE_ON_OFF, response_data_size);
	concat_im_pkt_data(response_head, username);
	//send(client_socket, sendbuf, IM_PKT_HEAD_SIZE + response_data_size, 0);

	/*send to all other user*/
	struct user_node *n;
	for(n = online_user_queue -> front; n != NULL; n = n -> next){
		if(strcmp(n -> username, username) != 0) {
			printf("username :%s\n", username);
			stcp_client_send(n -> send_sock_fd, sendbuf, IM_PKT_HEAD_SIZE + response_data_size);
		}
	}
}