#ifndef _HM_SLOT_H
#define _HM_SLOT_H
#include"hm_with_lowmac.h"

#define MAC_MAX_DATA 1588

/* 节点状态机 */
#define INIT 0  /* 初始化状态 */
#define SCN  1  /* 入网扫描状态 */
#define WAN  2  /* 正在入网状态 */
#define NET  3  /* 在网状态(包括孤立节点建网) */

/* LowMAC 节点状态 */


/********************************************** MAC层数据帧格式 **********************************************/
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
	char data[MAC_MAX_DATA];   /* 从网络层接收到的数据中，在DATA的头部包含2字节的type和2字节的len */
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


/********************************************** 邻节点时隙表/二维修改 11.06 **********************************************/
typedef struct _nb_BS_map_t{	
	U8   BS_flag:1;       /* 记录此时隙是否被占用 */
	U8   BS_ID:7;
	U8   state:3;
	U8   clock_lv:5;
	U8   life:4;
	U8   hop:4;
}nb_BS_map_t;

typedef struct _neighbor_map_t{	
	U8   netID;
	U8   BS_flag;        /* 记录此时隙表有没有选定BS  0没选定 1选定 */
	U8   referenceID;
	U8   rfclock_lv;
	U8	 r_BS;
	U8   localID;
	U8   lcclock_lv;
	U8   l_BS;
	U8   BS_num;           /* 记录时隙被占用的个数 */
	nb_BS_map_t  BS[MAX_CFS_PSF][MAX_NODE_CNT+1];  /* 用 BS[i][0].BS_ID 来记录占用i时隙的节点数 */
}neighbor_map_t;


/********************************************** 时隙表管理结构体/二维修改 11.06 **********************************************/
typedef struct _neighbor_map_manage_t{
	pthread_t bs_tid[MAX_CFS_PSF][MAX_NODE_CNT+1];
	pthread_t bs_timer[MAX_CFS_PSF][MAX_NODE_CNT+1];
	sem_t     bs_sem[MAX_CFS_PSF][MAX_NODE_CNT+1];

	/* 时隙表生存期 */
	U32       bs_net_BS_node[MAX_CFS_PSF][MAX_NODE_CNT+1];    /* 8位网络号，8位时隙号，8位节点号 */
	U8        bs_timeup[MAX_CFS_PSF][MAX_NODE_CNT+1];

	/* SCN状态使用 */
	U8        sf_NET_num[MAX_CFS_PSF];          /* 记录收到 NET 节点的勤务帧的次数，0~31时隙 */
	U8		  sf_flag;                       /* 当sf_num中有勤务帧次数为2时，sf_flag置1 */

	/* WAN状态使用 */
	U8        sf_rf_get;                 /* 收到上级节点的勤务帧，1收到，0未收到 */
	U8        sf_lc_get;				 /* 在上级节点的勤务帧中包含自己的信息，1包含，0不包含 */
	U8        sf_lc_flag;                /* 判断标志位 */

	/* NET状态使用 */
	U8		  sf_samenet_get;            /* 收到本网络的勤务帧，1收到，0未收到 */
	U8        sf_diffnet_get;            /* 收到异网络的勤务帧，1收到，0未收到 */
	U8        sf_rf_num;                 /* 收到上级节点勤务帧的次数 */
}neighbor_map_manage_t;


/********************************************** 下发给LowMAC的时隙表 **********************************************/
typedef struct _LM_neighbor_map_t{	
	U8   localBS;
	U8   slotnum;
	U16  slotlen;
	U8   dynamic_slot[8];
	U8   fixed_slot[MAX_CFS_PSF];
}LM_neighbor_map_t;


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


/********************************************** 当前节点状态反馈帧结构 **********************************************/
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
