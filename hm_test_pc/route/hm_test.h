#ifndef _MR_TEST_H
#define _MR_TEST_H

#define  LM_DATA_MAX  2044
#define  MAC_MAX_DATA 1588


/* struct for the data sharing among thread */
typedef struct _mt_tshare_t {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
//	int qr_run;
//	int gi_run;
} mt_tshare_t;


/* LowMAC���ݽṹ */
typedef struct _lm_packet_t{
	U16  len;
	U8	 type;
	U8   Lsn:4;
	U8   Hsn:4;
	char data[LM_DATA_MAX];
}lm_packet_t;


/* �յ��Ľڵ�ʱ϶ */
typedef struct _LM_neighbor_map_t{
	U8   localBS;
	U8   slotnum;
	U16  slotlen;
	U8   dynamic_slot[8];
	U8   fixed_slot[MAX_CFS_PSF];
}LM_neighbor_map_t;


/* MAC������֡��ʽ */
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
	char data[MAC_MAX_DATA];
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

/********************************************** �м̱��������ڹ���ṹ�� **********************************************/
typedef struct _mid_manage_t{
	pthread_t id;
	pthread_t timer;
	sem_t     sem;

	U8        node;    /* �ڵ�� */
	U8        timeup;  /* ��ʱ�������־ */
 
}mid_manage_t;

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



int   ht_tinit(char*);
int   ht_queues_init();
int   ht_queues_delete();
void* ht_qrv_thread();
int   ht_rmsg_proc(mmsg_t*, int);
int   ht_tmsg_node(MADR, int, mmsg_t*);
void  ht_show_top();
void  ht_show_rqs();
void* ht_timer_thread(void *);
void  ht_timer_proc(int );
void *ht_mid_manage_thread(void *);
void *ht_mid_life_thread(void *);


#endif
