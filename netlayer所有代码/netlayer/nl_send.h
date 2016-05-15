#ifndef NL_SEND_H
#define NL_SEND_H

#include "nl_common.h"
#include "nl_package.h"

typedef struct nl_buff
{
	U8 	number;
	U8 	count;
	int len[(MAX_DATA_LENGTH-1)/sizeof(nl_package_t) + 1];
	char package[(MAX_DATA_LENGTH-1)/sizeof(nl_package_t) + 1][sizeof(nl_package_t)];
}nl_buff_t;

typedef struct nl_buff_pool
{
	int flag;
	U8 seq;
	U8 src;
	time_t time;
	nl_buff_t nl_buf;
}nl_buff_pool_t;


int combine_send_pkt(nl_package_t * pkt, int length);
int manage_nl_buf(int key, U8 src, U8 seq);
int nl_send_to_himac(mmsg_t *msg,int len);
int nl_send_to_others(mmsg_t *snd_msg, U16 length);
void set_nl2other_mtype(mmsg_t *snd_buf, nl_package_t *pkt);

#endif // NL_SEND_H
