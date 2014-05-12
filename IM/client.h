#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "common/queue.h"
#include "common/protocol.h"


struct message{
	char sender[20];
	char text[100];
};

void client_close();

void login();

void logout();

void query_online_all();

void *recv_packet_thread(void *);

void save_recent_messages(char *, char *);

/*the buf[size-1] will always be \0*/
int get_keyboard_input(char *buf, int size);

void print_prompt_words(char *username);

void print_online_friends(struct user_queue *q);

void print_recent_messages(struct message *recent_messages, int msg_num);


#endif
