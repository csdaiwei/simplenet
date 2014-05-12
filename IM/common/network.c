#include <stdio.h>
#include <stdlib.h> 	//size_t
#include <string.h>		//memcpy
#include <errno.h>	//readn errno
#include <assert.h>
#include <sys/socket.h>    
#include <sys/types.h>

#include "network.h"
#include "protocol.h"
#include "../stcp.h"

/* readn - read exactly n bytes */
int m_readn( int sock_fd, char *bp, size_t len)
{
	if (stcp_server_recv(sock_fd, bp, len) < 0)
		return 0;
	return len;
}

/* readvrec - read a variable record */
int readvrec(int sock_fd, char *bp, size_t len )
{

	if (m_readn(sock_fd, bp, IM_PKT_HEAD_SIZE ) <= 0)
		return 0;

	struct im_pkt_head * h = (struct im_pkt_head *)bp;
	int data_size = (u_int32_t) h -> data_size;
	if (data_size > 0) {
		if (m_readn(sock_fd, &bp[IM_PKT_HEAD_SIZE], data_size) <= 0)
			return 0;
		return data_size + IM_PKT_HEAD_SIZE;
	}
	return IM_PKT_HEAD_SIZE;
}

/*construct an im packet head to "h"(first parameter)*/
void 
construct_im_pkt_head(struct im_pkt_head *h, char type, char service, short data_size){
	h -> type = type;
	h -> service = service;
	h -> data_size = data_size;
}

/*concatenate the im pkt data after the im pkt head*/
void
concat_im_pkt_data(struct im_pkt_head *h, char *data){

	//data_size and data cannot be 0 at the same time 
	assert(h -> data_size == 0 || data != 0);

	char *p = (char *)(h + 1);// p points right after the im_pkt_head
	memcpy(p, data, h -> data_size);
}