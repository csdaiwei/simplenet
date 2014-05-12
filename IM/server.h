#ifndef __SERVER_H__
#define __SERVER_H__

#define ONLINE_STATUS 1 // to be improve

void server_init();
void *server_accept_thread(void *arg);
void *client_handler(void * connfd);
void on_off_line_notify(int SERVICE_ON_OFF, char *username, char *sendbuf);


#endif
