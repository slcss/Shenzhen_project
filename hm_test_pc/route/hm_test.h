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


/* LowMAC数据结构 */
typedef struct _lm_packet_t{
	U16  len;
	U8	 type;
	U8   Lsn:4;
	U8   Hsn:4;
	char data[LM_DATA_MAX];
}lm_packet_t;


/* 收到的节点时隙 */
typedef struct _LM_neighbor_map_t{
	U8   localBS;
	U8   slotnum;
	U16  slotlen;
	U8   dynamic_slot[8];
	U8   fixed_slot[MAX_CFS_PSF];
}LM_neighbor_map_t;


/* MAC层数据帧格式 */
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


/********************************************** 勤务帧格式 **********************************************/
typedef struct _sf_BS_map_t{	
	U8   flag:1;     /* 有可能在时隙表中，此时隙对应节点是个两跳节点，而勤务帧只发一跳节点，故无效跳过，0为无效，1为有效 */
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
	U8   num;   /* 记录勤务帧中有效数据的最大个数 */
	sf_BS_map_t  BS[MAX_CFS_PSF];
}service_frame_t;

/********************************************** 中继表项生存期管理结构体 **********************************************/
typedef struct _mid_manage_t{
	pthread_t id;
	pthread_t timer;
	sem_t     sem;

	U8        node;    /* 节点号 */
	U8        timeup;  /* 定时器到达标志 */
 
}mid_manage_t;

/********************************************** 当前节点状态改变帧结构 **********************************************/
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
