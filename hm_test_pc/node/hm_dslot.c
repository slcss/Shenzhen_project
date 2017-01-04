/****
date 16.04.28
by s_lich
****/

#include "../mr_common.h"
#include "hm_slot.h"
#include "hm_with_lowmac.h"    /* for lm_packet_t */
#include "hm_timer.h"          /* for timer */
#include "hm_queue_manage.h"
#include "hm_common.h"
#include "hm_dslot.h"

/* 链表队列 */
extern link_queue_t link_queue[];

/* 信号量 */
extern sem_t  empty[];

/* 队列的互斥量 */
extern pthread_mutex_t mutex_queue[];

/* 本地节点ID */
extern U8 localID;

/* 本地节点加入的网络 */
extern U8 *netp;

/* 建立邻节点时隙表 指针数组 数组中每一个元素都是 neighbor_map_t 类型的指针 */
extern neighbor_map_t *neighbor_map[];  /* 暂时设定网络数最大为32个 11.05 */

/* 本地节点所在网络中一跳邻节点的个数 */
extern U8 hop1_conut;

/* 定时器结构 */
extern tsche_t nl_tsch;

/* dslot.c使用的定时器 */
extern tdslot_t dslot_timer;

/* 定时器到达标志 */
extern U8 timer_flag1;

/* 下发的时隙表 */
extern LM_neighbor_map_t LM_neighbor_map;

/* 邻节点跳数记录表 */
extern U8 hop_table[];  //hop_table[i]=0，节点i不在两跳范围内；hop_table[i]=1，节点i为1跳；hop_table[i]=2，节点i为2跳；本地节点暂时不计；

/* 读写锁 */
extern pthread_rwlock_t rw_lock;

/* 节点在网的标志位 6.29 */
extern U8 in_net;

extern FILE* fp;

/* 统计队列长度的互斥量 16.12.08 */
extern pthread_mutex_t all_qlenth;









int data_come = 0;         //到达数据个数
pthread_mutex_t for_data_come = PTHREAD_MUTEX_INITIALIZER;  //为data_come增加互斥量 16.12.13
U8 dslot_order_flag = 0;  //动态时隙预约/释放标志，0表示不在预约/释放过程中，1表示正在预约，2表示正在释放
U8 dslot_order_num = 0;	//动态时隙每复帧已经!预约的时隙总个数
U8 dslot_drop_num = 0;    //动态时隙每复帧需释放的时隙总个数

/* dslot 和 lm2nl 两个线程交互数据的两个缓存 */
lm_packet_t dslot_cache1;
lm_packet_t dslot_cache2;

/* 两个缓存是否可以读取的标志 0表示不可读可写 1表示可读不可写 */
int dslot_cache1_flag = 0;
int dslot_cache2_flag = 0;

/* 待发送的REQ帧 */
REQ_t REQ_frame;
/* 待发送的REP帧 */
REP_t REP_frame;
/* 待发送的ACK帧 */
ACK_t ACK_frame;
/* 待发送的DROP帧 */
DROP_t DROP_frame;

/* 待发送的MAC帧 */
//mac_packet_t mac_packet;

/* 节点维护的动态时隙总表，仅供本地节点加入的网络使用，dslot_map1记录本地节点和一跳邻节点，dslot_map2记录两跳节点 */
U8 dslot_map1[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];  //纵向代表D1-D55共55个动态时隙，横向代表1-32个节点，dslot_map[0][i]表示i节点占用的时隙总数，D[i][0]表示预约/占用i时隙节点的总个数，值为0表示时隙未占用，1表示预占用，2表示已占用
U8 dslot_map2[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];

/* 准动态时隙预约占用表 */
U8 dslot_temp_map[MAX_DSLS_PCF+1];  //占用的时隙置一，下次使用要清空，dslot_temp_map[0]为空

/* 发送REQ的节点统计收到的REP帧的数量，对应到具体某个节点 */
U8 REP_count[MAX_NODE_CNT+1];  //REP_count[0]为允许节点的总数，1为允许，0为不允许

/* 发送REQ的节点统计收到的REP帧携带的动态时隙 */
U8 REP_dslot[MAX_DSLS_PCF+1];  //统计REP帧中包含的时隙个数

U8 ask_node = 0;  //记录时隙预约请求节点

/* 队列的各种参数 16.12.15 */
float W = 0.1;  //计算平均队长的权值
float M1 = 0;  //预约门限
float M2 = 0;  //释放门限



#if 0
void *hm_dslot_thread(void *arg)
{
	U8 i,stop = 0;
	U8 count = 0;  //动态时隙占用计数
	U8 dcur_state = DINIT;
	U8 dnxt_state = DINIT;
	int rval,result = 0;
	void *p = NULL;	

	/* 待发送的MAC帧 */
	mac_packet_t mac_packet;
	
	while(0 == stop)
	{
		dcur_state = dnxt_state;
		switch(dcur_state)
		{
        	case DINIT:
				ask_node = 0;  //将记录的请求节点清0
				sem_wait(&(empty[3]));  //可能是主动触发也可能是被动触发，需进一步判断
				
				if(dslot_cache1_flag == 1)
				{
					EPT(stderr, "d0_1\n");
					
					dnxt_state = DREQ;
					break;
				}
				else if(dslot_order_flag == 1) //进入动态时隙预约过程 
				{
					EPT(stderr, "d0_2\n");

					/* 生成REQ帧 */
					memset(&REQ_frame, 0, sizeof(REQ_frame));
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][0] == 0)  //选取没有被占用的动态时隙
						{							
							dslot_temp_map[i] = 1;  //记录到准动态时隙预约占用表中，这个数据之后还会用到
							dslot_map[i][localID] = 1;  //将选用的时隙标记为预占用
							count++;
							if(count == dslot_order_num)
							{
								break;
								count = 0;
							}
						}
					}
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 2;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REQ_frame, sizeof(REQ_frame));					
					
					/* 下发REQ帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;
					
					dnxt_state = DREP;
					break;
				}
				else if(dslot_order_flag == 2) //进入动态时隙释放过程 
				{
					EPT(stderr, "d0_3\n");

					/* 生成DROP帧 */
					memset(&DROP_frame, 0, sizeof(DROP_frame));
					DROP_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][localID] == 2)  //选取被占用的动态时隙
						{	
							dslot_map[i][localID] = 0;
							dslot_map[i][0]--;
							dslot_map[0][localID]--;	 //清空操作						
							DROP_frame.slot_select[i] = 1;  //记录下被释放的动态时隙
							
							count++;
							if(count == dslot_drop_num)
							{
								break;
								count = 0;
							}
						}
					}
					count = 0;  //防止释放的时隙比占有的时隙还多
					
					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 6;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &DROP_frame, sizeof(DROP_frame));		
					
					/* 下发DROP帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(DROP_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，释放过程结束
					break;
				}
				else
				{
					EPT(stderr, "DINIT:dslot_order_flag = %d\n", dslot_order_flag);
					break;
				}
				
			case DREQ:		
				rval = hm_dslot_MAC_rcv_proc1(&dslot_cache1);  //判断收到的帧类型				
				
				//dslot_cache1_flag = 0;  //清空dslot_cache1

				if(rval = 1)
				{
					EPT(stderr, "d1_1\n");
					
					/* 生成REP帧 */
					p = (REQ_t *)((mac_packet_t *)dslot_cache1.data)->data;	
					ask_node = ((REQ_t *)p)->node;   //记录请求节点
					memset(&REP_frame, 0, sizeof(REP_frame));
					REP_frame.select_flag = 2;						
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REQ_t *)p)->slot_select[i] == 1 && dslot_map[i][((REQ_t *)p)->node] == 0)
						{
							dslot_map[i][((REQ_t *)p)->node] = 1;  //将选用的时隙标记为预占用
							REP_frame.slot_select[i] = 1;  //将允许占用的时隙记录到REP帧中
						}
						else
							REP_frame.select_flag = 1;	
					}					
					REP_frame.node_REQ = ((REQ_t *)p)->node;
					REP_frame.node = localID;								
					
					p = NULL;
					dslot_cache1_flag = 0;  //清空dslot_cache1

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 3;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REP_frame, sizeof(REP_frame));					
					
					/* 下发REP帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DACK;
					break;
				}

				if(rval = 2)
				{
					EPT(stderr, "d1_2\n");					
					
					/* 生成REP帧 */
					p = (REQ_t *)((mac_packet_t *)dslot_cache1.data)->data;					
					memset(&REP_frame, 0, sizeof(REP_frame));				
					REP_frame.node_REQ = ((REQ_t *)p)->node;
					REP_frame.node = localID;					
					REP_frame.select_flag = 0;
					
					p = NULL;
					dslot_cache1_flag = 0;  //清空dslot_cache1

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 3;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REP_frame, sizeof(REP_frame));					
					
					/* 下发REP帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}

				if(rval = 3)
				{
					EPT(stderr, "d1_3\n");
					dslot_cache1_flag = 0;  //清空dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}

				if(rval = 4)
				{
					EPT(stderr, "d1_4\n");					

					/* 收到1跳节点发来的允许占用REP，暂时还不能确定预约成功，需要等待时隙表的更新 */
					p = (REP_t *)((mac_packet_t *)dslot_cache1.data)->data;
					ask_node = ((REP_t *)p)->node_REQ;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REP_t *)p)->slot_select[i] == 1)
							dslot_map[i][((REP_t *)p)->node_REQ] = 1;  //将选用的时隙标记为预占用
					}					
					
					/* 开启定时器4，时间暂定两个超帧 */
					nl_tsch.tmask = nl_tsch.tmask^(1<3);
					while(1)
					{						
						sem_wait(&(empty[4]));  //等待缓冲区数据或者定时器到达
						if(timer_flag1 == 1)    //定时器到达则退出
						{
							timer_flag1 = 0;
							break;
						}
						
						if(dslot_cache1_flag == 1)
						{
							hm_dslot_MAC_rcv_proc1_1(&dslot_cache1);
							dslot_cache1_flag = 0;
						}
						if(dslot_cache2_flag == 1)
						{
							hm_dslot_MAC_rcv_proc1_1(&dslot_cache2);
							dslot_cache2_flag = 0;
						}
					}
					/* 将没有占用的预占用时隙清除 */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REP_t *)p)->slot_select[i] == 1 && dslot_map[i][((REP_t *)p)->node_REQ] == 1)
							dslot_map[i][((REP_t *)p)->node_REQ] = 0;  //将选用的时隙标记为预占用
					}
					p = NULL;
					dslot_cache1_flag = 0;  //清空dslot_cache1
					
					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}

				if(rval = 5)
				{
					EPT(stderr, "d1_5\n");
					dslot_cache1_flag = 0;  //清空dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}

				if(rval = 6)
				{
					EPT(stderr, "d1_6\n");
					
					p = (ACK_t *)((mac_packet_t *)dslot_cache1.data)->data;
					/* 更新动态时隙表 */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((ACK_t *)p)->slot_select[i] == 1)
						{	
							if(dslot_map[i][((ACK_t *)p)->node] == 0 || dslot_map[i][((ACK_t *)p)->node] == 1)
							{
								dslot_map[i][((ACK_t *)p)->node] = 2;
								dslot_map[0][((ACK_t *)p)->node]++;
								dslot_map[i][0]++;
							}
						}
					}					
					p = NULL;
					dslot_cache1_flag = 0;  //清空dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}

				if(rval = 7)
				{
					EPT(stderr, "d1_7\n");

					p = (DROP_t *)((mac_packet_t *)dslot_cache1.data)->data;
					/* 更新动态时隙表 */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((DROP_t *)p)->slot_select[i] == 1 && dslot_map[i][((DROP_t *)p)->node] == 2)
						{
							dslot_map[i][((DROP_t *)p)->node] = 0;
							dslot_map[i][0]--;
							dslot_map[0][((DROP_t *)p)->node]--;							
						}
					}					
					p = NULL;
					dslot_cache1_flag = 0;  //清空dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束
					break;
				}
				
			case DREP:
				memset(REP_count, 0, sizeof(REP_count));
				/* 开启定时器5，时间暂定两个超帧 */
				nl_tsch.tmask = nl_tsch.tmask^(1<4);
				while(1)
				{						
					sem_wait(&(empty[4]));  //等待缓冲区数据或者定时器到达
					if(timer_flag1 == 1)    //定时器到达则退出
					{
						timer_flag1 = 0;
						break;
					}
					
					if(dslot_cache1_flag == 1)
					{
						hm_dslot_MAC_rcv_proc2(&dslot_cache1);
						dslot_cache1_flag = 0;
					}
					if(dslot_cache2_flag == 1)
					{
						hm_dslot_MAC_rcv_proc2(&dslot_cache2);
						dslot_cache2_flag = 0;
					}					
				}
				
				if(REP_count[0] == hop1_conut)
				{
					EPT(stderr, "d2_1\n");

					/* 更新动态时隙表 */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_temp_map[i] == 1)
						{	
							if(dslot_map[i][localID] == 1)
							{
								dslot_map[i][localID] = 2;
								dslot_map[0][localID]++;
								dslot_map[i][0]++;
							}
						}
					}	

					/* 生成ACK帧 */
					memset(&ACK_frame, 0, sizeof(ACK_frame));
					ACK_frame.node = localID;
					memcpy(ACK_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 4;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REQ_frame, sizeof(REQ_frame));		

					/* 下发ACK帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束				
					break;
				}
				
				else
				{
					EPT(stderr, "d2_2\n");

					/* 将之前预占用的时隙清除 */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_temp_map[i] == 1 && dslot_map[i][localID] == 1)
							dslot_map[i][localID] = 0;
					}
					/* 生成REQ帧 */
					memset(&REQ_frame, 0, sizeof(REQ_frame));
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][0] == 0)  //选取没有被占用的动态时隙
						{							
							dslot_temp_map[i] = 1;  //记录到准动态时隙预约占用表中
							dslot_map[i][localID] = 1;  //将选用的时隙标记为预占用
							count++;
							if(count == dslot_order_num)
							{
								break;
								count = 0;
							}
						}
					}
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 2;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REQ_frame, sizeof(REQ_frame));					
					
					/* 下发REQ帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DREP;
					break;
				}

			case DACK:
				/* 开启定时器6，时间暂定两个超帧 */
				nl_tsch.tmask = nl_tsch.tmask^(1<5);
				while(1)
				{						
					sem_wait(&(empty[4]));  //等待缓冲区数据或者定时器到达
					if(timer_flag1 == 1)    //定时器到达则退出
					{
						timer_flag1 = 0;
						break;
					}
					
					if(dslot_cache1_flag == 1)
					{
						rval = hm_dslot_MAC_rcv_proc3(&dslot_cache1);
						if(rval == 1)
							result = 1;
						dslot_cache1_flag = 0;
					}
					if(dslot_cache2_flag == 1)
					{
						rval = hm_dslot_MAC_rcv_proc3(&dslot_cache2);
						if(rval == 1)
							result = 1;
						dslot_cache2_flag = 0;
					}					
				}

				if(result == 1)
				{
					EPT(stderr, "d3_1\n");
					result = 0;
					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束	
					break;
				}
				
				else
				{
					EPT(stderr, "d3_2\n");
					dnxt_state = DINIT;
					dslot_order_flag = 0;  //回到DINIT状态，预约过程结束	
					break;
				}
				
		}

	}
}
#endif

#ifdef _HM1_TEST
void *hm_dslot_test_thread(void *arg)
{
	U8 i,rval,count = 0;
	sleep(30);
	dslot_order_num = 4;					
	dslot_order_flag = 1;  //标志已进入动态时隙预约过程
	pthread_t dslot1_tid = -1;
	mac_packet_t mac_packet;

	/* 开启dslot接收线程 */
	rval = pthread_create(&dslot1_tid, NULL, hm_dslot_thread, NULL);
	if (rval != 0) 	
	{
		EPT(stderr, "hm_dslot_test_thread: can not create hm_dslot_thread\n");
	}

	/* 写锁 */
	pthread_rwlock_wrlock(&rw_lock);

	/* 生成REQ帧 */					
	memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
	REQ_frame.node = localID;
	for(i = 1; i <= MAX_DSLS_PCF; i++)
	{
		if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //选取没有被占用的动态时隙
		{	
			EPT(stderr, "take slot %d\n", i);
			dslot_temp_map[i] = 1;  //记录到准动态时隙预约占用表中，这个数据之后还会用到
			dslot_map1[i][localID] = 1;  //将选用的时隙标记为预占用
			dslot_map1[i][0]++;
			//dslot_map1[0][localID]++;
			
			/* 开启对应定时器 */
			sprintf(dslot_timer.procs[i][localID].name, "dtimer_D%dN%d", i, localID);
			dslot_timer.procs[i][localID].period = (T1 * 1000)/NL_TINTVL_US;
			dslot_timer.procs[i][localID].wait = dslot_timer.procs[i][localID].period;
			dslot_timer.procs[i][localID].pf = ds_timer;
			dslot_timer.procs[i][0].wait++;
			dslot_timer.tmap[i] = dslot_timer.tmap[i] | 1<<(localID-1);
			dslot_timer.tmask[i] = dslot_timer.tmask[i]^(1<<(localID-1));

			count++;
			if(count == dslot_order_num)
			{
				count = 0;
				break;				
			}
		}
	}
	memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

	/* 解锁 */
	pthread_rwlock_unlock(&rw_lock);

	/* 填充MAC帧 */
	memset(&mac_packet, 0, sizeof(mac_packet));
	mac_packet.pr = 0;
	mac_packet.type = 1;
	mac_packet.subt = REQ;
	mac_packet.re_add = 0;
	mac_packet.st_add = 0;
	mac_packet.dest = 0xFF;
	mac_packet.src = localID;
	mac_packet.seq = 0;
	mac_packet.h = 1;
	mac_packet.sn = 0;
	mac_packet.ttl = 1;
	mac_packet.cos = 0;
	mac_packet.ack = 0;
	mac_packet.rev = 0;
	memcpy(mac_packet.data, &REQ_frame, sizeof(REQ_frame));					
	
	/* 下发REQ帧 */
	pthread_mutex_lock(&mutex_queue[0]);
	hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);  /* 送到 TQ_0 队列 */
	sem_post(&(empty[0]));
	EPT(stderr, "REQ in Q0\n");
	pthread_mutex_unlock(&mutex_queue[0]);
	data_come++;

	/* 开启REQ_timer */
	nl_tsch.tmask = nl_tsch.tmask^(1<<3);	
	pause();
	EPT(stderr, "pause end\n");
}

#else
void *hm_dslot_test_thread(void *arg)
{
	U8 i,stop = 0,count = 0;
	float N1 = 0;  //平均业务速率
	float N2 = 0;  //平均服务速率
	float N1max = 0;  //满足600ms时延，在当前的服务速率下，支持的最大业务速率
	float N1max_1 = 0;
	float N2min = 0;  //满足600ms时延，在当前的业务速率下，支持的最小服务速率
	float sum = 0;  //10个data_come的和
	float avg_new = 0;	//新的平均队长
	float avg_old = 0;	//旧的平均队长
	int q_len = 0;		//瞬时队长
	mac_packet_t mac_packet;
	int rval;
	pthread_t dslot1_tid = -1;
	U8 temp = 0;   //记录每次预约时隙数目的最终值
	U8 order_temp = 0;   //记录每次预约时隙数目的临时值
	U8 order_temp_num = 0;   //记录相同预约时隙数目的次数
	U8 drop_temp = 0;   //记录每次释放时隙数目的临时值
	U8 drop_temp_num = 0;   //记录相同释放时隙数目的次数
	

	/* 开启dslot接收线程 */
	rval = pthread_create(&dslot1_tid, NULL, hm_dslot_thread, NULL);
	if (rval != 0) 	
	{
		EPT(stderr, "hm_dslot_test_thread: can not create hm_dslot_thread\n");
	}

	for(i = 1; i<= 10; i++)
	{
		usleep(64000);  //每复帧统计一次
		pthread_mutex_lock(&for_data_come);
		sum = sum + data_come;		
		hm_dslot_test_queue_enter(link_queue, 6, &data_come);  //将10次统计的data_come值存入队列，队列的长度最大为10
		data_come = 0;
		pthread_mutex_unlock(&for_data_come);
	}
	EPT(fp, "sum1 = %f\n", sum);
	
	while(0 == stop)
	{
		usleep(64000);
		pthread_mutex_lock(&for_data_come);
		sum = sum + data_come - hm_dslot_test_queue_delete(link_queue, 6);
		hm_dslot_test_queue_enter(link_queue, 6, &data_come);
		data_come = 0;
		pthread_mutex_unlock(&for_data_come);

		/* 计算各种参数 */
		N1 = sum/10;              EPT(fp, "\nN1 = %f\n", N1);
		N2 = hm_get_slot_num();  EPT(fp, "N2 = %f\n", N2);
		if(!N2)  //节点没入网就不开启动态时隙预约
			continue;
		
		pthread_mutex_lock(&mutex_queue[0]);
		q_len = link_queue[0].real_l;
		pthread_mutex_unlock(&mutex_queue[0]);
		pthread_mutex_lock(&mutex_queue[1]);
		q_len += link_queue[1].real_l;
		pthread_mutex_unlock(&mutex_queue[1]);
		pthread_mutex_lock(&mutex_queue[2]);
		q_len += link_queue[2].real_l;
		pthread_mutex_unlock(&mutex_queue[2]);
		pthread_mutex_lock(&mutex_queue[3]);
		q_len += link_queue[3].real_l;
		pthread_mutex_unlock(&mutex_queue[3]);
		pthread_mutex_lock(&mutex_queue[5]);
		q_len += link_queue[5].real_l;
		pthread_mutex_unlock(&mutex_queue[5]);
		EPT(fp, "q_len = %d\n", q_len);		
		
#if 0
		N1max = 2*N2*(10*N2-1)/(20*N2-1);  EPT(fp, "N1max = %f\n", N1max);
		N2min = (N1+sqrt(N1*N1-(N1-2*N2)/5))/2;   EPT(fp, "N2min = %f\n", N2min);
		
		if(N1 > N1max || N1 < (N2min))  //需要改变W，明显改变队长  
		{
			avg_new = (1-5*W)*avg_old + 5*W*q_len;  
			EPT(fp, "avg_new1 = %f\n", avg_new);
				
		}
		else
		{
			avg_new = (1-W)*avg_old + W*q_len;   
			EPT(fp, "avg_new2 = %f\n", avg_new);
		}
		avg_old = avg_new;
		
		M1 = 2*(10*N2-1)*(10*N2-1)/(20*N2-1);   EPT(fp, "M1 = %f\n", M1);
		M2 = (N1max-dslot_order_num)*(N1max-dslot_order_num)/(2*(N2-N1max+dslot_order_num)*N2);   EPT(fp, "M2 = %f\n", M2); 
#endif

		N1max = 2*N2*(4*N2-1)/(8*N2-1);  EPT(fp, "N1max = %f\n", N1max);
		//为已经预约到的动态时隙数目
		N1max_1 = 2*(N2-dslot_order_num)*(4*(N2-dslot_order_num)-1)/(8*(N2-dslot_order_num)-1);  EPT(fp, "N1max_1 = %f\n", N1max_1);
		N2min = ((N1+0.25)+sqrt((N1+0.25)*(N1+0.25)-N1/2))/2;   EPT(fp, "N2min = %f\n", N2min);
		
		if(N1 > N1max || N2 < N2min || (N2min < N1max && (int)N2min != (int)N1max))  //需要改变W，明显改变队长  
		{
			avg_new = (1-5*W)*avg_old + 5*W*q_len;  
			EPT(fp, "avg_new1 = %f\n", avg_new);
				
		}
		else
		{
			avg_new = (1-W)*avg_old + W*q_len;   
			EPT(fp, "avg_new2 = %f\n", avg_new);
		}
		avg_old = avg_new;

		M1 = 2*(4*N2-1)*(4*N2-1)/(8*N2-1);   EPT(fp, "M1 = %f\n", M1);
		M2 = N1max_1*N1max_1/(2*(N2-N1max_1)*N2);   EPT(fp, "M2 = %f\n", M2);

		/* 判断预约/释放条件 */
		if(avg_new > M1 && in_net == 2)  //孤立节点的时隙预约
		{
			EPT(fp, "111\n");
			if((N1+0) >= N1max || N2 <= N2min)
			{
				EPT(fp, "222\n");	
				/* 预约动态时隙 */
				if(N2min+0-N2 <= 0)
					continue;
				if((N2min+0-N2) == (int)(N2min+0-N2))
					temp = (int)(N2min+0-N2);
				else
					temp = (int)(N2min+0-N2+1);

				/* 防止多次预约，连续3次预约时隙数目相同才开始预约 16.12.23 */
				drop_temp_num = 0;
				drop_temp = 0;
				if(temp == order_temp)
				{
					order_temp_num++;
					if(order_temp_num == 3)
					{
						order_temp_num = 0;
						order_temp = 0;
					}
					else
						continue;
				}
				else
				{
					order_temp_num = 1;
					order_temp = temp;
					continue;
				}
				//dslot_order_num += temp;
				EPT(fp, "dslot_order_num = %d\n", temp);
				if(temp != 0)
				{	
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //选取没有被占用的动态时隙
						{						
							dslot_map1[i][localID] = 2;  //将选用的时隙标记为已占用
							dslot_map1[i][0]++;
							dslot_map1[0][localID]++;
							dslot_order_num++;
							count++;
							if(count == temp)
							{
								count = 0;
								break;							
							}
						}
					}
					count = 0; //防止预约的比可用的还多
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
				}
			}			
		}
		else if(avg_new > M1 && in_net == 1)  //时隙预约必须满足节点在网这个条件
		{
			EPT(fp, "333\n");
			if((N1+0) >= N1max || N2 <= N2min)
			{
				EPT(fp, "444\n");
				/* 预约动态时隙，个数为每复帧N1-N2 */
				//dslot_order_num = (int)(N1+0.5) - (int)(N2+0.5);
				/* 预约动态时隙 */
				if(N2min-N2 <= 0)
					continue;
				if((N2min-N2) == (int)(N2min-N2))
					temp = (int)(N2min-N2);
				else
					temp = (int)(N2min-N2+1);

				/* 防止多次预约，连续3次预约时隙数目相同才开始预约 16.12.23 */
				drop_temp_num = 0;
				drop_temp = 0;
				if(temp == order_temp)
				{
					order_temp_num++;
					if(order_temp_num == 3)
					{
						order_temp_num = 0;
						order_temp = 0;
					}
					else
						continue;
				}
				else
				{
					order_temp_num = 1;
					order_temp = temp;
					continue;
				}
				//dslot_order_num += temp;
				EPT(fp, "dslot_order_num = %d\n", temp);
				if(dslot_order_flag == 0 && temp != 0)
				{							 			
					dslot_order_flag = 1;  //标志已进入动态时隙预约过程

					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/* 生成REQ帧 */					
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //选取没有被占用的动态时隙
						{							
							dslot_temp_map[i] = 1;  //记录到准动态时隙预约占用表中，这个数据之后还会用到
							dslot_map1[i][localID] = 1;  //将选用的时隙标记为预占用
							dslot_map1[i][0]++;
							//dslot_map1[0][localID]++;
							
							/* 开启对应定时器 */
							sprintf(dslot_timer.procs[i][localID].name, "dtimer_D%dN%d", i, localID);
							dslot_timer.procs[i][localID].period = (T1 * 1000)/NL_TINTVL_US;
							dslot_timer.procs[i][localID].wait = dslot_timer.procs[i][localID].period;
							dslot_timer.procs[i][localID].pf = ds_timer;
							dslot_timer.procs[i][0].wait++;
							dslot_timer.tmap[i] = dslot_timer.tmap[i] | 1<<(localID-1);
							dslot_timer.tmask[i] = dslot_timer.tmask[i]^(1<<(localID-1));

							count++;
							if(count == temp)
							{
								count = 0;
								break;								
							}
						}
					}
					count = 0; //防止预约的比可用的还多
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = REQ;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &REQ_frame, sizeof(REQ_frame));						
					
					/* 下发REQ帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					pthread_mutex_lock(&for_data_come);
					data_come++;
					pthread_mutex_unlock(&for_data_come);

					/* 开启REQ_timer */
					nl_tsch.tmask = nl_tsch.tmask^(1<<3);
				}
			}
		}
		else if(avg_new < M2 || avg_new < 0.2)
		{
			EPT(fp, "555\n");
			if(N1 < N2)
			{
				EPT(fp, "666\n");
				/* 释放动态时隙 */
				if(N2min-N2 >= 0)
					continue;
				if((N2-N2min) == (int)(N2-N2min))
					dslot_drop_num = (int)(N2-N2min);
				else
					dslot_drop_num = (int)(N2-N2min);	

				/* 防止多次预约，连续4次释放时隙数目相同才开始预约 16.12.23 */
				order_temp_num = 0;
				order_temp = 0;
				if(dslot_drop_num == drop_temp)
				{
					drop_temp_num++;
					if(drop_temp_num == 4)
					{
						drop_temp_num = 0;
						drop_temp = 0;
					}
					else
						continue;
				}
				else
				{
					drop_temp_num = 1;
					drop_temp = dslot_drop_num;
					continue;
				}
				
				EPT(fp, "dslot_drop_num = %d\n", dslot_drop_num);
				//dslot_order_num = (dslot_order_num > dslot_drop_num)?(dslot_order_num-dslot_drop_num):0;
				
				if(dslot_order_flag == 0 && dslot_drop_num != 0 && N2 > 1)
				{
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/* 生成DROP帧 */
					memset(&DROP_frame, 0, sizeof(DROP_frame));
					DROP_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][localID] == 2)  //选取被占用的动态时隙
						{	
							dslot_map1[i][localID] = 0;
							dslot_map1[i][0]--;
							dslot_map1[0][localID]--;	 //清空操作		
							dslot_order_num--;
							DROP_frame.slot_select[i] = 1;  //记录下被释放的动态时隙

							/* 关闭对应定时器 */							
							dslot_timer.procs[i][0].wait--;							
							dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(localID-1));
							
							count++;
							if(count == dslot_drop_num)
							{
								count = 0;
								break;								
							}
						}
					}
					count = 0;  //防止释放的时隙比占有的时隙还多

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					
					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = DROP;
					mac_packet.re_add = 0;
					mac_packet.st_add = 0;
					mac_packet.dest = 0xFF;
					mac_packet.src = localID;
					mac_packet.seq = 0;
					mac_packet.h = 1;
					mac_packet.sn = 0;
					mac_packet.ttl = 1;
					mac_packet.cos = 0;
					mac_packet.ack = 0;
					mac_packet.rev = 0;
					memcpy(mac_packet.data, &DROP_frame, sizeof(DROP_frame));					
					
					/* 下发DROP帧 */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(DROP_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					pthread_mutex_lock(&for_data_come);
					data_come++;
					pthread_mutex_unlock(&for_data_come);
				}				
			}
		}
	}
}
#endif

void *hm_dslot_thread(void *arg)
{
	U8 i,rval,stop = 0;	

	while(0 == stop)
	{						
		sem_wait(&(empty[3]));  //等待缓冲区数据或者定时器到达
		
		if(dslot_cache1_flag == 1)
		{
			hm_dslot_MAC_rcv_proc(&dslot_cache1);
			dslot_cache1_flag = 0;
		}
		if(dslot_cache2_flag == 1)
		{
			hm_dslot_MAC_rcv_proc(&dslot_cache2);
			dslot_cache2_flag = 0;
		}					
	}
}

int hm_dslot_MAC_rcv_proc(lm_packet_t *cache_p)
{
	U8 i;
	REQ_t *REQ_p = NULL;
	REP_t *REP_p = NULL;
	ACK_t *ACK_p = NULL;
	DROP_t *DROP_p = NULL;
	mac_packet_t mac_packet;

	U8 count1=0,count2=0,count3=0;
	U8 hop;	
	
	switch(cache_p->type)
	{
		case REQ:
			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);
			
			EPT(stderr, "rcv REQ\n");
			REQ_p = (REQ_t *)((mac_packet_t *)cache_p->data)->data;
			memset(&REP_frame, 0, sizeof(REP_frame));
			REP_frame.node_REQ = REQ_p->node;
			REP_frame.node = localID;			
			for(i = 1; i <= MAX_DSLS_PCF; i++)
			{
				if(REQ_p->slot_select[i] == 1)
				{
					EPT(stderr, "rcv REQ slot1 %d\n", i);
					count1++;
					if(dslot_map1[i][0] == 0)
					{
						//EPT(stderr, "rcv REQ slot2 %d\n", i);
						count2++;
						dslot_map1[i][REQ_p->node] = 1;  //将选用的时隙标记为预占用
						EPT(stderr, "dslot_map1[%d][%d] = %d\n", i, REQ_p->node, dslot_map1[i][REQ_p->node]);
						dslot_map1[i][0]++;
						//dslot_map1[0][REQ_p->node]++;
						REP_frame.slot_select[i] = 1;   //将允许占用的时隙记录到REP帧中

						/* 开启对应定时器 */
						sprintf(dslot_timer.procs[i][REQ_p->node].name, "dtimer_D%dN%d", i, REQ_p->node);
						dslot_timer.procs[i][REQ_p->node].period = (T2 * 1000)/NL_TINTVL_US;
						dslot_timer.procs[i][REQ_p->node].wait = dslot_timer.procs[i][REQ_p->node].period;
						dslot_timer.procs[i][REQ_p->node].pf = ds_timer;
						dslot_timer.procs[i][0].wait++;
						dslot_timer.tmap[i] = dslot_timer.tmap[i] | (1<<(REQ_p->node-1));
						dslot_timer.tmask[i] = dslot_timer.tmask[i]^(1<<(REQ_p->node-1));						
					}					
				}
			}

			if(count1 == count2)
				REP_frame.select_flag = 2;
			else if(count2 > 0)
				REP_frame.select_flag = 1;
			else
				REP_frame.select_flag = 0;
			EPT(stderr, "REP_frame.select_flag = %d\n", REP_frame.select_flag);

			/* 解锁 */
			pthread_rwlock_unlock(&rw_lock);
			
			/* 填充MAC帧 */
			memset(&mac_packet, 0, sizeof(mac_packet));
			mac_packet.pr = 0;
			mac_packet.type = 1;
			mac_packet.subt = REP;
			mac_packet.re_add = 0;
			mac_packet.st_add = 0;
			mac_packet.dest = 0xFF;
			mac_packet.src = localID;
			mac_packet.seq = 0;
			mac_packet.h = 1;
			mac_packet.sn = 0;
			mac_packet.ttl = 1;
			mac_packet.cos = 0;
			mac_packet.ack = 0;
			mac_packet.rev = 0;
			memcpy(mac_packet.data, &REP_frame, sizeof(REP_frame));					
			
			/* 下发REP帧 */
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
			sem_post(&(empty[0]));
			pthread_mutex_unlock(&mutex_queue[0]);
			EPT(stderr, "REP in Q0\n");
			pthread_mutex_lock(&for_data_come);
			data_come++;
			pthread_mutex_unlock(&for_data_come);
			break;
			
		case REP:
			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);
			
			EPT(stderr, "rcv REP\n");
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;			
			
			if(REP_p->node_REQ == localID)
			{
				if(dslot_order_flag == 0)  //如果本节点已不在预约过程中，则无需接收发给本节点的REP帧
				{
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
				if(REP_p->select_flag == 0)
				{
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_temp_map[i] == 1)
						{
							dslot_map1[i][localID] = 0;
							dslot_map1[i][0]--;
							//dslot_map1[0][localID]--;

							/* 关闭对应定时器 */							
							dslot_timer.procs[i][0].wait--;							
							dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(localID-1));													
						}
					}
					dslot_order_flag = 0;  //关闭本地节点预约过程标志	
				}
				else
				{
					if(REP_count[REP_p->node] == 0)
					{
						REP_count[REP_p->node] = 1;
						REP_count[0]++;

						for(i = 1; i <= MAX_DSLS_PCF; i++)
						{
							if(REP_p->slot_select[i] == 1)
							{
								EPT(stderr, "i1 = %d\n", i);
								/* 如果定时器未到时，考虑dslot_map1[i][localID]=0也是条件 */
								EPT(stderr, "dslot_timer.tmask[%d] = %u\n", i, dslot_timer.tmask[i]);
								if(!((dslot_timer.tmask[i]>>(localID-1)) & 1))
								{	
									REP_dslot[i]++; 
									EPT(stderr, "i2 = %d\n", i);
								}
							}
						}
					
						if(REP_count[0] == hop1_conut)
						{
							EPT(stderr, "hop1_conut = %d\n", hop1_conut);
							memset(&ACK_frame, 0, sizeof(ACK_frame));
							count3 = 0;
							for(i = 1; i <= MAX_DSLS_PCF; i++)
							{
								if(dslot_temp_map[i] == 1 && REP_dslot[i] == hop1_conut && !((dslot_timer.tmask[i]>>(localID-1)) & 1))
								{
									count3 = 1;
									dslot_map1[i][localID] = 2;
									dslot_map1[0][localID]++;
									dslot_order_num++;
									ACK_frame.slot_select[i] = 1;
									EPT(stderr, "ACK select = %d\n", i);
								}
							}
							EPT(stderr, "count3 = %d\n", count3);
							if(count3 != 0)
							{
								ACK_frame.node = localID;
								/* 填充MAC帧 */
								memset(&mac_packet, 0, sizeof(mac_packet));
								mac_packet.pr = 0;
								mac_packet.type = 1;
								mac_packet.subt = ACK;
								mac_packet.re_add = 0;
								mac_packet.st_add = 0;
								mac_packet.dest = 0xFF;
								mac_packet.src = localID;
								mac_packet.seq = 0;
								mac_packet.h = 1;
								mac_packet.sn = 0;
								mac_packet.ttl = 1;
								mac_packet.cos = 0;
								mac_packet.ack = 0;
								mac_packet.rev = 0;
								memcpy(mac_packet.data, &ACK_frame, sizeof(ACK_frame));		

								/* 下发ACK帧 */
								pthread_mutex_lock(&mutex_queue[0]);
								hm_queue_enter(link_queue, 0, 8+sizeof(ACK_frame), (char *)&mac_packet, HL_MP_DATA);     /* 送到 TQ_0 队列 */
								sem_post(&(empty[0]));
								pthread_mutex_unlock(&mutex_queue[0]);
								EPT(stderr, "ACK in Q0\n");
								pthread_mutex_lock(&for_data_come);
								data_come++;
								pthread_mutex_unlock(&for_data_come);
							}

							/* 停止REQ_timer */
							nl_tsch.tmask = nl_tsch.tmask | (1<<3);
							nl_tsch.procs[3].wait = nl_tsch.procs[3].period; 
							EPT(stderr, "REQ_timer is shutdown\n");
							/* 清空统计数据 */
							memset(REP_count, 0, sizeof(REP_count));
							memset(REP_dslot, 0, sizeof(REP_dslot));
							dslot_order_flag = 0;  //关闭本地节点预约过程标志	
						}
					}
				}	
				/* 解锁 */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}	
			/* 判断REP_p->node_REQ是本地节点的几跳邻节点 */
			if(hop_table[REP_p->node_REQ] == 1)
			{	
				/* 解锁 */
				pthread_rwlock_unlock(&rw_lock);
				break;		
			}
			if(hop_table[REP_p->node_REQ] == 2)
			{
				if(REP_p->select_flag != 0)
				{
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(REP_p->slot_select[i] == 1)
						{
							if(dslot_map2[i][REP_p->node_REQ] == 0)
							{
								dslot_map2[i][REP_p->node_REQ] = 1;
								EPT(stderr, "dslot_map2[%d][%d] = %d\n", i, REP_p->node_REQ, dslot_map2[i][REP_p->node_REQ]);
								dslot_map2[i][0]++;
								//dslot_map2[0][REP_p->node_REQ]++;

								/* 开启对应定时器 */
								sprintf(dslot_timer.procs[i][REP_p->node_REQ].name, "dtimer_D%dN%d", i, REP_p->node_REQ);
								dslot_timer.procs[i][REP_p->node_REQ].period = (T3 * 1000)/NL_TINTVL_US;
								dslot_timer.procs[i][REP_p->node_REQ].wait = dslot_timer.procs[i][REP_p->node_REQ].period;
								dslot_timer.procs[i][REP_p->node_REQ].pf = ds_timer;
								dslot_timer.procs[i][0].wait++;
								dslot_timer.tmap[i] = dslot_timer.tmap[i] | (1<<(REP_p->node_REQ-1));
								dslot_timer.tmask[i] = dslot_timer.tmask[i]^(1<<(REP_p->node_REQ-1));	
							}
							else if(dslot_map2[i][REP_p->node_REQ] == 1)
							{
								EPT(stderr, "hm_dslot_MAC_rcv_proc: dslot %d is reserved\n", i);
							}
							else if(dslot_map2[i][REP_p->node_REQ] == 2)
							{
								EPT(stderr, "hm_dslot_MAC_rcv_proc: dslot %d is taken\n", i);
							}
						}
					}
				}
				/* 解锁 */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}			
			
		case ACK:
			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);
			
			EPT(stderr, "rcv ACK\n");
			ACK_p = (ACK_t *)((mac_packet_t *)cache_p->data)->data;
			//EPT(stderr, "ACK_p->slot_select[1] = %d  dslot_map1[1][%d] = %d\n", ACK_p->slot_select[i], ACK_p->node, dslot_map1[i][ACK_p->node]);
			
			for(i = 1; i <= MAX_DSLS_PCF; i++)
			{
				if(ACK_p->slot_select[i] == 1 && dslot_map1[i][ACK_p->node] == 1)
				{					
					EPT(stderr, "ACK_p->slot_select[%d] = %d  dslot_map1[%d][%d] = %d\n", i, ACK_p->slot_select[i], i, ACK_p->node, dslot_map1[i][ACK_p->node]);
					EPT(stderr, "rcv ACK = %d\n", i);
					dslot_map1[i][ACK_p->node] = 2;
					dslot_map1[0][ACK_p->node]++;
					
					/* 关闭对应定时器 */							
					dslot_timer.procs[i][0].wait--;							
					dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(ACK_p->node-1));
				}
			}
			/* 解锁 */
			pthread_rwlock_unlock(&rw_lock);
			break;

		case DROP:
			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);
			
			EPT(stderr, "rcv DROP\n");
			DROP_p = (DROP_t *)((mac_packet_t *)cache_p->data)->data;
			for(i = 1; i <= MAX_DSLS_PCF; i++)
			{
				if(DROP_p->slot_select[i] == 1 && dslot_map1[i][DROP_p->node] == 2)
				{
					dslot_map1[i][DROP_p->node] = 0;
					dslot_map1[i][0]--;
					dslot_map1[0][DROP_p->node]--;	

					/* 关闭对应定时器 */							
					dslot_timer.procs[i][0].wait--;							
					dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(DROP_p->node-1));
				}
			}
			/* 解锁 */
			pthread_rwlock_unlock(&rw_lock);
			break;
			
		default:			
			EPT(stderr, "hm_dslot_MAC_rcv_proc: unknown MAC frame, no = %d\n", cache_p->type);
			break;
	}
}

U8 hm_get_slot_num()
{
	U8 i,j;
	U16 result = 0;
	for(i = 0; i < MAX_CFS_PSF; i++)
	{
		result += bit_count(LM_neighbor_map.fixed_slot[i]);
	}
	/* 返回值是每个节点平均每复帧占用的时隙 */
	return (result/MAX_CFS_PSF + dslot_map1[0][localID]);	
}

U8 bit_count(U8 v)
{
	U8 c; //置位总数累计

	for (c = 0; v; c++)
	{
		v &= v - 1; //去掉最低的置位
	}
	return c;
}

/* 数据入队 */
int hm_dslot_test_queue_enter(link_queue_t *Q, U8 id, int *data)
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
	memcpy(p->data, data, sizeof(int));
	p->next = NULL;
	
	Q[id].rear->next = p;	
	Q[id].real_l++;  /* 队列真实长度加1 */
	Q[id].rear = p;
	return rval;
}

/* 数据出队 */
int hm_dslot_test_queue_delete(link_queue_t *Q, U8 id)  /* 数据被读出 */
{
	int rval = 0;
	int outcome = 0;
	node_t *p;
	if(Q[id].front == Q[id].rear)
	{
		EPT(stderr, "11 queue %d is empty\n", id);
		rval = 1;
		return rval;
	}
	p = Q[id].front->next;
	
	memcpy(&outcome, p->data, sizeof(int));
	
	Q[id].front->next = p->next;
	if(Q[id].rear == p)
		Q[id].rear = Q[id].front;
	free(p);

	Q[id].real_l--;
	//EPT(stderr, "11 queue %d real_l %d\n", id, Q[id].real_l);
	return outcome;
}


#if 0
int hm_dslot_MAC_rcv_proc1(lm_packet_t *cache_p)
{
	U8 i,rval = 0;
	U8 hop = 0;
	REQ_t *REQ_p;
	REP_t *REP_p;
	ACK_t *ACK_p;
	
	switch(cache_p->type)
	{
		case 0x02:    /* 收到的是时隙预约请求帧REQ */
			REQ_p = (REQ_t *)((mac_packet_t *)cache_p->data)->data;
			for(i = 1; i <= MAX_DSLS_PCF; i++)
			{
				if(REQ_p->slot_select[i] == 1 && dslot_map[i][REQ_p->node] == 0)
				{	
					rval = 1;
					return rval;
				}
			}
			rval = 2;			
			break;

		case 0x03:    /* 收到的是时隙预约响应帧REP */
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;
			hop = 0;

			/* 判断REP_p->node是本地节点的几跳邻节点 */
			for(i = 0; i < MAX_CFS_PSF; i++)
			{
				if(neighbor_map[*netp]->BS[i][REP_p->node_REQ].BS_flag == 1)
				{	
					hop = neighbor_map[*netp]->BS[i][REP_p->node_REQ].hop;
					break;
				}
			}
			if(hop == 1)
			{	
				rval = 3;				
			}
			if(hop == 2)
			{
				if(REP_p->select_flag != 0)
				{
					rval = 4;					
				}
				else
				{
					rval = 5;					
				}
			}
			break;

		case 0x04:    /* 收到的是时隙预约确认帧ACK */
			rval = 6;			
			break;

		case 0x05:    /* 收到的是时隙释放通知帧DROP */
			rval = 7;			
			break;

		default:
			EPT(stderr, "hm_dslot_MAC_rcv_proc1: unknown MAC frame, no = %d\n", cache_p->type);
			break;
	}
	return rval;
}

int hm_dslot_MAC_rcv_proc1_1(lm_packet_t *cache_p)
{
	U8 i,rval = 0;
	ACK_t *ACK_p;	
	
	switch(cache_p->type)
	{
		case 0x02:    /* 收到的是时隙预约请求帧REQ */
		case 0x03:    /* 收到的是时隙预约响应帧REP */
			break;
			
		case 0x04:    /* 收到的是时隙预约确认帧ACK */
			ACK_p = (ACK_t *)((mac_packet_t *)cache_p->data)->data;
			if(ACK_p->node != ask_node)
				break;
			for(i = 1; i < MAX_DSLS_PCF; i++)
			{
				/* 查看ACK帧中哪个时隙被占用 */
				if(ACK_p->slot_select[i] == 1)
				{
					/* 查看此时隙是否已被记录过 */
					if(dslot_map[i][ACK_p->node] == 1)
					{
						dslot_map[i][ACK_p->node] = 2;
						dslot_map[0][ACK_p->node]++;
						dslot_map[i][0]++;
					}
				}
			}
			break;
			
		default:
			EPT(stderr, "hm_dslot_MAC_rcv_proc2: unknown MAC frame, no = %d\n", cache_p->type);
			break;
			
	}
	return rval;
}

int hm_dslot_MAC_rcv_proc2(lm_packet_t *cache_p)
{
	U8 i,rval = 0;
	REP_t *REP_p;

	switch(cache_p->type)
	{
		case 0x02:    /* 收到的是时隙预约请求帧REQ */
		case 0x04:    /* 收到的是时隙预约确认帧ACK */
			break;

		case 0x03:    /* 收到的是时隙预约响应帧REP */
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;

			/* 判断REP回应的发送REQ的节点是否为本地节点 */
			if(REP_p->node_REQ != localID)
				break;
			if(REP_p->select_flag == 2)  //全部允许占用
			{
				if(REP_count[REP_p->node] == 0)
				{
					REP_count[REP_p->node] = 1;
					REP_count[0]++;					
				}
			}
			else if(REP_p->select_flag == 1)  //部分允许占用
			{
				if(REP_count[REP_p->node] == 0)
				{
					REP_count[REP_p->node] = 1;
					REP_count[0]++;					
				}
				for(i = 1; i <= MAX_DSLS_PCF; i++)
					dslot_temp_map[i] = dslot_temp_map[i] && REP_p->slot_select[i];  //修正预约的时隙

			}
			break;
	}
	return rval;
}

int hm_dslot_MAC_rcv_proc3(lm_packet_t *cache_p)
{
	U8 i,rval = 0;
	ACK_t *ACK_p;

	switch(cache_p->type)
	{
		case 0x02:    /* 收到的是时隙预约请求帧REQ */
		case 0x03:    /* 收到的是时隙预约响应帧REP */
			break;

		case 0X04:	   /* 收到的是时隙预约确认帧ACK */ 
			ACK_p = (ACK_t *)((mac_packet_t *)cache_p->data)->data;
			if(ACK_p->node != ask_node)
				break;
			for(i = 1; i < MAX_DSLS_PCF; i++)
			{
				/* 查看ACK帧中哪个时隙被占用 */
				if(ACK_p->slot_select[i] == 1)
				{
					/* 查看此时隙是否已被记录过 */
					if(dslot_map[i][ACK_p->node] == 1)
					{
						dslot_map[i][ACK_p->node] = 2;
						dslot_map[0][ACK_p->node]++;
						dslot_map[i][0]++;
					}
				}
			}
			rval = 1;
			break;
	}
	return rval;	
}
#endif
