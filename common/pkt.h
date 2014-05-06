#ifndef PKT_H
#define PKT_H

#include "constants.h"

//SIP packet type
#define ROUTE_UPDATE 1
#define SIP 2 

#define SIP_HEADER_LEN 12
//SIP packet struct
typedef struct sipheader {
  int src_nodeID;
  int dest_nodeID;
  unsigned short int length;  //date length
  unsigned short int type;  //packet type
} sip_hdr_t;

typedef struct packet {
  sip_hdr_t header;
  char data[MAX_PKT_LEN];
} sip_pkt_t;

//route update entry struct
typedef struct routeupdate_entry {
  unsigned int nodeID;  //dest node ID
  unsigned int cost;      //route path cost
} routeupdate_entry_t;

//packet for route update
typedef struct pktrt{
    unsigned int entryNum;    //num of entry
    routeupdate_entry_t entry[MAX_NODE_NUM];
} pkt_routeupdate_t;


typedef struct sendpktargument {
  int nextNodeID;     //next node ID
  sip_pkt_t pkt;      //packet
} sendpkt_arg_t;

/** API for SIP, send a packet to SON network
  return 1  on success
  return -1 on failure
 */
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn);


/** API for SIP, receive a packet from SON network
  return 1  on success
  return -1 on failure
 */
int son_recvpkt(sip_pkt_t* pkt, int son_conn);


/** API for SON, receive a packet from SIP
  return 1  on success
  return -1 on failure
 */
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn);


/** API for SON, send a packet to SIP
  return 1  on success
  return -1 on failure
 */
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn);


/** API for SON, send a packet to SON network
  return 1  on success
  return -1 on failure
 */
int sendpkt(sip_pkt_t* pkt, int conn);

/** API for SON, receive a packet from SON network
  return 1  on success
  return -1 on failure
 */
int recvpkt(sip_pkt_t* pkt, int conn);

#endif

