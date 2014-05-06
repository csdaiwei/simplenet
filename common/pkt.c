#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "pkt.h"
#include "bool.h"

/** API for SIP, send a packet to SON network
	return 1  on success
	return -1 on failure
 */
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn) {
	
	sendpkt_arg_t *sendpkt_arg = (sendpkt_arg_t *)malloc(sizeof(sendpkt_arg_t));
	sendpkt_arg -> nextNodeID = nextNodeID;
	memcpy((char *)sendpkt_arg + sizeof(int), pkt, sizeof(sip_pkt_t));
	
	int connd;
	connd = send(son_conn, "!&", 2, 0);
	connd = send(son_conn, sendpkt_arg, pkt -> header.length + SIP_HEADER_LEN + sizeof(int), 0);
	connd = send(son_conn, "!#", 2, 0);
	
	if (connd <= 0) 
		return -1;
	
	return 1;
}

/** API for SIP, receive a packet from SON network
	return 1  on success
	return -1 on failure
 */
int son_recvpkt(sip_pkt_t* pkt, int son_conn) {

	char sign[3];
	while (true) {
		if (recv(son_conn, sign, 2, 0) <= 0) 
			return -1;
		if (strncmp(sign, "!&", 2) == 0) 
			break;
	}

	int connd;
	connd = recv(son_conn, pkt, SIP_HEADER_LEN, 0);
	connd = recv(son_conn, (char *)pkt + SIP_HEADER_LEN, pkt -> header.length, 0);
	
	connd = recv(son_conn, sign, 2, 0);
	if (connd <= 0)
		return -1;
	if (strncmp(sign, "!#", 2) != 0)
		return -1;

	return 1;
}

/** API for SON, receive a packet from SIP
	return 1  on success
	return -1 on failure
 */
int getpktToSend(sip_pkt_t* pkt, int* nextNode, int sip_conn) {
	
	char sign[3];
	while (true) {
		if (recv(sip_conn, sign, 2, 0) <= 0) 
			return -1;
		if (strncmp(sign, "!&", 2) == 0) 
			break;
	}

	int connd;
	connd = recv(sip_conn, nextNode, sizeof(int), 0);
	connd = recv(sip_conn, pkt, SIP_HEADER_LEN, 0);
	connd = recv(sip_conn, (char *)pkt + SIP_HEADER_LEN, pkt -> header.length, 0);

	connd = recv(sip_conn, sign, 2, 0);
	if (connd <= 0)
		return -1;
	if (strncmp(sign, "!#", 2) != 0)
		return -1;

	return 1;
}

/** API for SON, send a packet to SIP
	return 1  on success
	return -1 on failure
 */
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn) {
	
	int connd;
	connd = send(sip_conn, "!&", 2, 0);
	connd = send(sip_conn, pkt, pkt -> header.length + SIP_HEADER_LEN, 0);
	connd = send(sip_conn, "!#", 2, 0);
	
	if (connd <= 0)
		return -1;

	return 1;
}

/** API for SON, send a packet to SON network
	return 1  on success
	return -1 on failure
 */
int sendpkt(sip_pkt_t* pkt, int conn) {

	int connd;
	connd = send(conn, "!&", 2, 0);
	connd = send(conn, pkt, pkt -> header.length + SIP_HEADER_LEN, 0);
	connd = send(conn, "!#", 2, 0);
	
	if (connd <= 0)
		return -1;

	return 1;
}

/** API for SON, receive a packet from SON network
	return 1  on success
	return -1 on failure
 */
int recvpkt(sip_pkt_t* pkt, int conn) {

	char sign[3];
	while (true) {
		if (recv(conn, sign, 2, 0) <= 0) 
			return -1;
		if (strncmp(sign, "!&", 2) == 0) 
			break;
	}

	int connd;
	connd = recv(conn, pkt, SIP_HEADER_LEN, 0);
	connd = recv(conn, (char *)pkt + SIP_HEADER_LEN, pkt -> header.length, 0);
	
	connd = recv(conn, sign, 2, 0);
	if (connd <= 0)
		return -1;
	if (strncmp(sign, "!#", 2) != 0)
		return -1;

	return 1;
}
