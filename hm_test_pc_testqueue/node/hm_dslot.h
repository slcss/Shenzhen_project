#ifndef _HM_DSLOT_H
#define _HM_DSLOT_H
#include "hm_with_lowmac.h"
#include "hm_queue_manage.h"

#define W  0.1	//Ȩֵ
#define M1 2	//ԤԼ����
#define M2 1	//�ͷ�����
#define RED(old, q) (1-W)*old+W*q  //RED�㷨������ƽ���ӳ���ʽ

#define REQ  0x02
#define REP  0x03
#define ACK  0x04
#define DROP 0x06


#ifdef _HM_TEST
#define T1  3000 /*ms*/
#define T2  3000 /*ms*/
#define T3  4200 /*ms*/
#define T4  4200 /*ms*/
#else
#define T1  1100 /*ms*/
#define T2  1100 /*ms*/
#define T3  4200 /*ms*/
#define T4  4200 /*ms*/
#endif

/********************************************** ʱ϶ԤԼ����֡REQ **********************************************/
typedef struct _REQ_t{
	U8	node;
	U8	slot_select[MAX_DSLS_PCF+1];
}REQ_t;


/********************************************** ʱ϶ԤԼ��Ӧ֡REP **********************************************/
typedef struct _REP_t{
	U8	node_REQ;  //REPӦ��ķ���REQ�Ľڵ�
	U8	node;
	U8	slot_select[MAX_DSLS_PCF+1];
	U8	select_flag;  //0Ϊ������ռ�ã�1Ϊ��������ռ�ã�2Ϊȫ������ռ��
}REP_t;


/********************************************** ʱ϶ԤԼȷ��֡ACK **********************************************/
typedef struct _ACK_t{
	U8	node;
	U8	slot_select[MAX_DSLS_PCF+1];
}ACK_t;


/********************************************** ʱ϶�ͷ�֪ͨ֡DROP **********************************************/
typedef struct _DROP_t{
	U8	node;
	U8	slot_select[MAX_DSLS_PCF+1];
}DROP_t;


void *hm_dslot_test_thread(void *);
void *hm_dslot_thread(void *);
int hm_dslot_MAC_rcv_proc(lm_packet_t *);
U8 hm_get_slot_num();
U8 bit_count(U8);
int hm_dslot_test_queue_enter(link_queue_t *, U8, int *);
int hm_dslot_test_queue_delete(link_queue_t *, U8);




#endif

