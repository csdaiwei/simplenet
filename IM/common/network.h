#ifndef __NETWORK_H
#define __NETWORK_H

#include "protocol.h"

struct socket_fd_t {
	int send_sock_fd;
	int recv_sock_fd;
	int client_node_id;
};

int m_readn( int sock_fd, char *bp, size_t len);
int readvrec( int sock_fd, char *bp, size_t len);
//int sendn()

void construct_im_pkt_head(struct im_pkt_head *h, char type, char service, short data_size);
void concat_im_pkt_data(struct im_pkt_head *h, char *data);


#endif

