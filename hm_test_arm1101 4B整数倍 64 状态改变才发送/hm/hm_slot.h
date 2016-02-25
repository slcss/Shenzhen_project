#ifndef _HM_SLOT_H
#define _HM_SLOT_H
#include"hm_with_lowmac.h"

#define MAC_MAX_DATA 1588

/* �ڵ�״̬�� */
#define INIT 0  /* ��ʼ��״̬ */
#define SCN  1  /* ����ɨ��״̬ */
#define WAN  2  /* ��������״̬ */
#define NET  3  /* ����״̬(���������ڵ㽨��) */

/* LowMAC �ڵ�״̬ */


/********************************************** MAC������֡��ʽ **********************************************/
typedef struct _mac_packet_t{
	U8   subt:4;
	U8	 type:2;
	U8   pr:2;
	U8	 re_add;
	U8   st_add;
	U8   dest;
	U8   src;
	U8   h:1;
	U8   seq:7;
	U8   ttl:4;
	U8   sn:4;
	U8   rev:4;
	U8   ack:1;
	U8   cos:3;
	char data[MAC_MAX_DATA];   /* ���������յ��������У���DATA��ͷ������2�ֽڵ�type��2�ֽڵ�len */
}mac_packet_t;


/********************************************** ����֡��ʽ **********************************************/
typedef struct _sf_BS_map_t{	
	U8   flag:1;     /* �п�����ʱ϶���У���ʱ϶��Ӧ�ڵ��Ǹ������ڵ㣬������ֻ֡��һ���ڵ㣬����Ч������0Ϊ��Ч��1Ϊ��Ч */
	U8   BS_ID:7;
	U8   state:3;
	U8   clock_lv:5;
}sf_BS_map_t;

typedef struct _service_frame_t{	
	U8   netID;
	U8   referenceID;
	U8   rfclock_lv;
	U8   r_BS;
	U8   localID;
	U8   lcclock_lv;
	U8   l_BS;
	U8   num;   /* ��¼����֡����Ч���ݵ������� */
	sf_BS_map_t  BS[MAX_CFS_PSF];
}service_frame_t;


/********************************************** �ڽڵ�ʱ϶��/��ά�޸� 11.06 **********************************************/
typedef struct _nb_BS_map_t{	
	U8   BS_flag:1;       /* ��¼��ʱ϶�Ƿ�ռ�� */
	U8   BS_ID:7;
	U8   state:3;
	U8   clock_lv:5;
	U8   life:4;
	U8   hop:4;
}nb_BS_map_t;

typedef struct _neighbor_map_t{	
	U8   netID;
	U8   BS_flag;        /* ��¼��ʱ϶����û��ѡ��BS  0ûѡ�� 1ѡ�� */
	U8   referenceID;
	U8   rfclock_lv;
	U8	 r_BS;
	U8   localID;
	U8   lcclock_lv;
	U8   l_BS;
	U8   BS_num;           /* ��¼ʱ϶��ռ�õĸ��� */
	nb_BS_map_t  BS[MAX_CFS_PSF][MAX_NODE_CNT+1];  /* �� BS[i][0].BS_ID ����¼ռ��iʱ϶�Ľڵ��� */
}neighbor_map_t;


/********************************************** ʱ϶�����ṹ��/��ά�޸� 11.06 **********************************************/
typedef struct _neighbor_map_manage_t{
	pthread_t bs_tid[MAX_CFS_PSF][MAX_NODE_CNT+1];
	pthread_t bs_timer[MAX_CFS_PSF][MAX_NODE_CNT+1];
	sem_t     bs_sem[MAX_CFS_PSF][MAX_NODE_CNT+1];

	/* ʱ϶�������� */
	U32       bs_net_BS_node[MAX_CFS_PSF][MAX_NODE_CNT+1];    /* 8λ����ţ�8λʱ϶�ţ�8λ�ڵ�� */
	U8        bs_timeup[MAX_CFS_PSF][MAX_NODE_CNT+1];

	/* SCN״̬ʹ�� */
	U8        sf_NET_num[MAX_CFS_PSF];          /* ��¼�յ� NET �ڵ������֡�Ĵ�����0~31ʱ϶ */
	U8		  sf_flag;                       /* ��sf_num��������֡����Ϊ2ʱ��sf_flag��1 */

	/* WAN״̬ʹ�� */
	U8        sf_rf_get;                 /* �յ��ϼ��ڵ������֡��1�յ���0δ�յ� */
	U8        sf_lc_get;				 /* ���ϼ��ڵ������֡�а����Լ�����Ϣ��1������0������ */
	U8        sf_lc_flag;                /* �жϱ�־λ */

	/* NET״̬ʹ�� */
	U8		  sf_samenet_get;            /* �յ������������֡��1�յ���0δ�յ� */
	U8        sf_diffnet_get;            /* �յ������������֡��1�յ���0δ�յ� */
	U8        sf_rf_num;                 /* �յ��ϼ��ڵ�����֡�Ĵ��� */
}neighbor_map_manage_t;


/********************************************** �·���LowMAC��ʱ϶�� **********************************************/
typedef struct _LM_neighbor_map_t{	
	U8   localBS;
	U8   slotnum;
	U16  slotlen;
	U8   dynamic_slot[8];
	U8   fixed_slot[MAX_CFS_PSF];
}LM_neighbor_map_t;


/********************************************** ��ǰ�ڵ�״̬�ı�֡�ṹ **********************************************/
typedef struct _H2L_MAC_frame_t{	
	U8   referenceID;
	U8   rfclock_lv;
	U8   localID;
	U8   lcclock_lv;
	U8   res:5;
	U8   state:3;
	U8   slotnum;
	U16  slotlen;
}H2L_MAC_frame_t;


/********************************************** ��ǰ�ڵ�״̬����֡�ṹ **********************************************/
typedef struct _L2H_MAC_frame_t{	
	U8   localID;
	U8   lcclock_lv;
	U8   referenceID;
	U8   rfclock_lv;
	U8   build:4;
	U8   state:4;
	U8   res1;
	U8   res2;
	U8   res3;
}L2H_MAC_frame_t;


void *hm_slot_thread(void *);
void hm_MAC_frame_rcv_proc1(lm_packet_t *, U8*);
U8 hm_MAC_frame_rcv_proc2(lm_packet_t *, U8, U8*);
U8 hm_MAC_frame_rcv_proc3(lm_packet_t *, U8, U8*);
void *hm_neighbor_map_thread(void *);
void *hm_bs_life_thread(void *);
void hm_LowMAC_slot_proc(U8);
void *hm_sf_ls_send_thread(void *);

#if 1
void *hm_pf_slot_thread(void *);
#endif

#endif
