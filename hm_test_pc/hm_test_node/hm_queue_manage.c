#include "../mr_common.h"
#include "hm_queue_manage.h"
#include "hm_with_lowmac.h"

/* 6�����еĻ����� */
pthread_mutex_t mutex_queue[QUEUE_NUM] = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_MUTEX_INITIALIZER
};        


/* ���� link_queue ���� */
link_queue_t link_queue[QUEUE_NUM] = {       
	{NULL, NULL, NULL},
	{NULL, NULL, NULL},
	{NULL, NULL, NULL},
	{NULL, NULL, NULL},
	{NULL, NULL, NULL},
	{NULL, NULL, NULL}	
};

extern HSN[];


/* ����6���ն��� */
int hm_queue_init(link_queue_t *Q)
{
	int rval = 0;
	int id=0;
	for(id=0;id<QUEUE_NUM;id++)
	{
		Q[id].front = Q[id].rear = (node_t *)malloc(sizeof(node_t));
		Q[id].temp = Q[id].front;
		if(!Q[id].front)
		{
			EPT(stderr, "queue %d cannot initial\n",id);
			exit(1);
		}
		Q[id].front->next = NULL;
	}
	return(rval);
}

/* ����6������ */
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
	return(rval);
}

/* ������� */
int hm_queue_enter(link_queue_t *Q, U8 id, U16 len, char *data, U8 type)
{
	int rval = 0;
	node_t *p;
	p = (node_t *)malloc(sizeof(node_t));
	memset(p,0,sizeof(node_t));
	if(!p)
	{
		EPT(stderr, "queue %d cannot enter queue\n",id);
		exit(1);
	}
	memcpy(p->data, data, len);
	p->len = len;
	p->type = type;
	p->next = NULL;
	Q[id].rear->next = p;
	Q[id].rear = p;
	return rval;
}

/* ���ݳ��� */
int hm_queue_delete(link_queue_t *Q, U8 id, lm_packet_t *packet)  /* ���ݱ����� */
{
	int rval = 0;
	node_t *p;
	if(Q[id].front == Q[id].rear)
	{
		EPT(stderr, "queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].front->next;
	
	memcpy(packet->data, p->data, p->len);
	packet->len = p->len+4;
	packet->type = p->type;
	hm_get_HLsn(packet, id);

	//EPT(stderr, "hm_queue_delete: queue %d packet->Hsn = %d\n", id, packet->Hsn);
	
	Q[id].front->next = p->next;
	if(Q[id].rear == p)
		Q[id].rear = Q[id].front;
	free(p);
	return rval;
}


/* �������ݳ��� */
int hm_queue_delete_flow_ctrl(link_queue_t *Q, U8 id, lm_packet_t *packet)  /* ���ݱ����� */
{
	int rval = 0;
	node_t *p;
	if(Q[id].temp == Q[id].rear)
	{
		EPT(stderr, "queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].temp->next;
	
	memcpy(packet->data, p->data, p->len);
	packet->len = p->len+4;
	packet->type = p->type;

	if(HSN[id] == 0xff)
	{
		HSN[id] = p->HSN+1;
		packet->Hsn = p->HSN;
	}
	else
	{
		hm_get_HLsn(packet, id);
		p->HSN = packet->Hsn;    /* �����·��󣬼�¼HSN��� */
	}
	
	Q[id].temp->next = p->next;
	if(Q[id].rear == p)
		Q[id].rear = Q[id].temp;
	//free(p);
	return rval;
}


/* ɾ��HSNȷ�ϵ����� */
int hm_queue_delete_HSN(link_queue_t *Q, U8 id, U8 hsn, U8 i)  /* i��¼�յ�����������һ���յ���HSN����ֻ������ǰHSN(��front)���ڶ����յ�������µ�ǰHSN(��rear) */
{
	int rval = 0;
	node_t *p;
	U8  stop = 0;
	
	if(Q[id].front == Q[id].rear)
	{
		EPT(stderr, "queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].front->next;

	while(0 == stop)
	{
		if(p->HSN != hsn)
		{
			Q[id].front->next = p->next;
			if(Q[id].rear == p)
				Q[id].rear = Q[id].front;
			free(p);
			p = Q[id].front->next;
			continue;
		}
		stop = 1;
	}

	if(i == 2)
	{
		Q[id].temp->next = p;   /* 9/1�޸�->temp */

		HSN[id] = 0xff;  /* ��ʾHSNֵ���¼��� */
	}
	return rval;
}


