#include "../mr_common.h"
#include "hm_queue_manage.h"
#include "hm_with_lowmac.h"

/* 7个队列的互斥量 */
pthread_mutex_t mutex_queue[QUEUE_NUM] = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};        


/* 传递 link_queue 即可 */
link_queue_t link_queue[QUEUE_NUM] = {       
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0},
	{NULL, NULL, NULL, 0, 0}	
};

extern U8 HSN[];


/* 构造7个空队列 */
int hm_queue_init(link_queue_t *Q)
{
	int rval = 0;
	int id=0;
	for(id=0;id<QUEUE_NUM;id++)
	{
		Q[id].front = Q[id].rear = (node_t *)malloc(sizeof(node_t));
		Q[id].temp = (node_t *)malloc(sizeof(node_t));
		if(!Q[id].front || !Q[id].temp)
		{
			EPT(stderr, "queue %d cannot initial\n",id);
			exit(1);
		}
		Q[id].front->next = NULL;
		Q[id].temp->next = NULL;
		Q[id].real_l = 0;
		Q[id].uc_l = 0;
	}
	return rval;
}

/* 销毁7个队列 */
int hm_queue_destroy(link_queue_t *Q)
{
	int rval = 0;
	int id = 0;
	for(id=0;id<QUEUE_NUM;id++)
	{
		while(Q[id].front)
		{
			Q[id].rear = Q[id].front->next;
			free(Q[id].front);
			Q[id].front = Q[id].rear;
		}		
	}
	return rval;
}

/* 数据入队 */
int hm_queue_enter(link_queue_t *Q, U8 id, U16 len, char *data, U8 type)
{
	//EPT(stderr, "queue %d  enter\n", id);
	int rval = 0;
	node_t *p;
	p = (node_t *)malloc(sizeof(node_t));
	memset(p,0,sizeof(node_t));
	if(!p)
	{
		EPT(stderr, "queue %d cannot enter queue\n", id);
		exit(1);
	}
	memcpy(p->data, data, len);
	p->len = len;
	p->type = type;
	p->next = NULL;
	Q[id].rear->next = p;	

	Q[id].real_l++;  /* 队列真实长度加1 */
	if(id == 0)
		EPT(stderr, "queue %d enter real_l %d\n", id, Q[id].real_l);

	/* 入队判断 3.2 */
	if(Q[id].temp->next == NULL)
	{	
		Q[id].temp->next = Q[id].rear->next;		
	}

	Q[id].rear = p;
	return rval;
}

/* 数据出队 */
int hm_queue_delete(link_queue_t *Q, U8 id, lm_packet_t *packet)  /* 数据被读出 */
{
	int rval = 0;
	node_t *p;
	if(Q[id].front == Q[id].rear)
	{
		EPT(stderr, "11 queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].front->next;
	
	memcpy(packet->data, p->data, p->len);
	packet->len = p->len+4;  //printf("packet->len = %d\n", packet->len);	
	packet->type = p->type;  //printf("packet->type = %d\n", packet->type);
	hm_get_HLsn(packet, id);
	//EPT(stderr, "11 queue %d\n", packet->Hsn);

	//EPT(stderr, "hm_queue_delete: queue %d packet->Hsn = %d\n", id, packet->Hsn);
	
	Q[id].front->next = p->next;
	if(Q[id].rear == p)
		Q[id].rear = Q[id].front;
	free(p);

	Q[id].real_l--;
	if(id == 0)
		EPT(stderr, "queue %d real_l %d\n", id, Q[id].real_l);
	return rval;
}


#if 1
/* 流控数据出队 */
int hm_queue_delete_flow_ctrl(link_queue_t *Q, U8 id, lm_packet_t *packet)  /* 数据被读出 */
{
	int rval = 0;
	node_t *p = NULL;
	if(Q[id].temp->next == NULL)
	{
		EPT(stderr, "22 queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	
	/* 加窗口的限制 3.2 */
	if(Q[id].uc_l >= HSN_WINDOW)
	{
		EPT(stderr, "queue %d is winmax\n", id);
		/* 达到最大窗后从队列中未确认的第一个开始发送 */
		Q[id].temp->next = Q[id].front->next;
		Q[id].uc_l = 0;
		HSN[id] = Q[id].temp->next->HSN;
			
		//rval = 1;
		//return rval;
	}
	p = Q[id].temp->next;
	
	memcpy(packet->data, p->data, p->len);
	packet->len = p->len+4;	
	packet->type = p->type;

	if(HSN[id] == 0xff)
	{
		HSN[id] = p->HSN+1;
		if(HSN[id] == 16)
			HSN[id] = 0;
		packet->Hsn = p->HSN;
		//EPT(stderr, "22 queue %d send1 %d\n", id, p->HSN);
	}
	else
	{		
		p->HSN = hm_get_HLsn(packet, id);  /* 数据下发后，记录HSN编号 */
		//EPT(stderr, "22 queue %d send2 %d\n", id, p->HSN);
	} 
	
	/* 出队修改 2.29 */
	Q[id].temp->next = p->next;
	Q[id].uc_l++;
	EPT(stderr, "queue %d uc_l %d hm %d\n", id, Q[id].uc_l, p->HSN);
	
	return rval;
}


/* 删除HSN确认的数据 */
int hm_queue_delete_HSN(link_queue_t *Q, U8 id, U8 hsn, U8 hsnRcvCount)  /* j记录收到次数，若第一次收到此HSN，则只更新最前HSN，第二次收到则需更新当前HSN */
{
	int rval = 0;
	int i,count = 0;
	node_t *p = NULL;
	
	if(Q[id].front == Q[id].rear)
	{
		EPT(stderr, "33 queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].front->next;
	EPT(stderr, "33 queue %d front %d	lm %d\n", id, p->HSN, hsn);

	/* HSN之前的都已被正确接收 2.29 */
	if(p->HSN != hsn)
	{
		if(p->HSN < hsn)
			count = hsn - p->HSN;
		else
			count = hsn + HSN_MAX - p->HSN;
		for(i = 1; i<=count; i++)
		{
			Q[id].front->next = p->next;
			/* 检查Q[id].temp->next的位置 11.21 */
			if(Q[id].temp->next == p)
				Q[id].temp->next = p->next;
			Q[id].real_l--;
			/* 防止Q[id].uc_l为负 11.17 */
			if(Q[id].uc_l-1 >= 0)
				Q[id].uc_l--;
			if(Q[id].rear == p)
			{	
				Q[id].rear = Q[id].front;
				free(p);
				p = NULL;
				//EPT(stderr, "33 queue %d is empty1\n", id);
				break;
			}
			free(p);
			p = Q[id].front->next;	
		}
	}	
	else
	{		
		Q[id].temp->next = p;   /* 9/1修改->temp */
		Q[id].uc_l = 0;
		HSN[id] = 0xff;  /* 表示HSN值重新计数 */
	}	
	EPT(stderr, "queue %d real_l %d\n", id, Q[id].real_l);	
	EPT(stderr, "queue %d uc_l %d\n", id, Q[id].uc_l);
	return rval;
}
#endif

