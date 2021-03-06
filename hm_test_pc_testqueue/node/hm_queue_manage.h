#ifndef _HM_QUEUE_MANAGE_H
#define _HM_QUEUE_MANAGE_H
#include "hm_with_lowmac.h"

#define  QUEUE_NUM   7
#define  MAX_DATA    2048
#define  HSN_MAX     16
#define  HSN_WINDOW   15  /* 收到HSN反馈前，最多下发15个数据 3.2 */

typedef struct _node_t {	
	U16     len;
	U8	    type;
	struct _node_t  *next;
	U8      HSN;  /* 数据第一次出队(未删除)后，此处记录出对数据的HSN编号 */
	char  	data[MAX_DATA];
}node_t;

typedef struct _link_queue_t{
	node_t  *front;
	node_t  *rear;
	node_t  *temp;
	int     real_l;   /* real length,from front to rear */
	int     uc_l;     /* unconfirm length,form front to temp */
}link_queue_t;


int hm_queue_init(link_queue_t *);
int hm_queue_destroy(link_queue_t *);
int hm_queue_enter(link_queue_t *, U8, U16, char *, U8);
int hm_queue_delete(link_queue_t *, U8, lm_packet_t *);
int hm_queue_delete_flow_ctrl(link_queue_t *, U8, lm_packet_t *);
int hm_queue_delete_HSN(link_queue_t *, U8, U8, U8);




#endif
