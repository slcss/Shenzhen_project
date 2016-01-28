#include "../mr_common.h"
#include "hm_slot.h"
#include "hm_with_lowmac.h"    /* for lm_packet_t */
#include "hm_timer.h"          /* for timer */
#include "hm_queue_manage.h"
#include "hm_common.h"

extern FILE* fp;

/* 本地节点ID */
extern U8 localID;

/* 消息队列管理信息 */
extern qinfo_t qinfs[];
extern int re_qin;
extern hm_tshare_t  share;


/* slot 和 lm2nl 两个线程交互数据的两个缓存 */
lm_packet_t slot_cache1;
lm_packet_t slot_cache2;

/* 两个缓存是否可以读取的标志 0表示不可读可写 1表示可读不可写 */
int slot_cache1_flag = 0;
int slot_cache2_flag = 0;

/* 当前状态和下一个状态 */
U8 cur_state = INIT;
U8 nxt_state = INIT;

/* 6个链表队列 */
extern link_queue_t link_queue[];

/* 建立邻节点时隙表 指针数组 数组中每一个元素都是 neighbor_map_t 类型的指针 */
neighbor_map_t *neighbor_map[32];  /* 暂时设定网络数最大为32个 11.05 */
U8  netID[32];      /* 记录现存网络号对应的网络零级节点，暂时设定网络数最大为32个 11.05 */
U8  netnum = 0;     /* 记录现存网络个数 */

/* 时隙表管理表结构体指针数组 */
neighbor_map_manage_t *neighbor_map_manage[32];   /* 每一个网络的时隙表对应一个时隙表管理表，暂时设定网络数最大为32个 11.05 */

/* setitimer 结构体 */
extern struct itimerval new_value;

/* 信号量 */
extern sem_t  empty[];

/* 互斥量 */
extern pthread_mutex_t mutex_queue[];        /* 6个队列的互斥量 */

/* 读写锁 */
pthread_rwlock_t rw_lock = PTHREAD_RWLOCK_INITIALIZER;

/* 定时器到达标志 */
extern U8 timer_flag;

/* 待发送的勤务帧 */
service_frame_t service_frame;

/* 待发送的MAC帧 */
mac_packet_t mac_packet;

/* 待发送的状态改变帧 */
H2L_MAC_frame_t H2L_MAC_frame;

/* 下发的时隙表 */
LM_neighbor_map_t LM_neighbor_map;

#ifdef _HM_TEST
/* 打印时隙表线程 */
sem_t pf_slot_sem;

extern FILE *fp;
#endif



void *hm_slot_thread(void *arg)
{
	U8  sf_count = 0;          /* 记录勤务帧个数 */
	U8  i,j,bs;
	U8  net_num = 0;           /* 记录满足条件(收到同一个NET节点两次勤务帧)的网络个数 */
	U8  net_i = 0;             /* 记录入网选定哪一个网络 */
	U8  bs_i = 0;              /* 记录选定的基准节点的时隙 */
	U8  node_i = 0;            /* 记录选定的节点号 */
	U8  net_count = 0;		   /* 清空所有时隙表及时隙管理表时对网络的计数 1.13 */

	/* 暂存量 */
	U8  num = 0;
	U8  level = 32;
	U8  max =0;

	int rval;
	pthread_t sf_ls_send;

	void *thread_result;  /* for pthread_join 10.30 */

	U8 node_num;  /* 记录占用某一时隙的节点总数 11.07 */
	U8 bs_num = 0;  /* 记录被占用的时隙数 11.07 */

	memset(netID, 0, sizeof(netID));

	pthread_detach(pthread_self());
	EPT(stderr, "%s: slot thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = pthread_create(&sf_ls_send, NULL, hm_sf_ls_send_thread, (void *)&net_i);
	if (rval != 0) 	
	{
		EPT(stderr, "%s: hm_slot_thread can not create hm_sf_ls_send thread\n", qinfs[re_qin].pname);
		rval = 1;
		goto thread_return;
	}

#ifdef _HM_TEST
	pthread_t pf_slot;
	

	rval = sem_init(&pf_slot_sem, 0, 0);
	if (rval != 0) 	
	{
		EPT(stderr, "%s: hm_slot_thread can not create pf_slot_sem\n", qinfs[re_qin].pname);
		rval = 1;
		goto thread_return;
	}
	
	rval = pthread_create(&pf_slot, NULL, hm_pf_slot_thread, (void *)&net_i);
	if (rval != 0) 	
	{
		EPT(stderr, "%s: hm_slot_thread can not create hm_sf_ls_send thread\n", qinfs[re_qin].pname);
		rval = 1;
		goto thread_return;
	}
#endif

	while(1)
	{
		sf_count = 0;
		timer_flag = 0;

		cur_state = nxt_state;
    	switch(cur_state)
		{
        	case INIT:
				EPT(stderr, "hm_slot_thread: state = 0******************\n");
				/*********** 状态改变 ***********/
            	nxt_state = SCN;				

				/* 状态改变，下发状态改变帧 */
				memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
				H2L_MAC_frame.referenceID = 0xff;
				H2L_MAC_frame.rfclock_lv = 0xff;
				H2L_MAC_frame.localID = localID;
				H2L_MAC_frame.lcclock_lv = 0xff;
				H2L_MAC_frame.state = 0;      /* 节点状态变为 SCN */
				H2L_MAC_frame.res = 0;
				H2L_MAC_frame.slotnum = 8;
				H2L_MAC_frame.slotlen = 40000;

				pthread_mutex_lock(&mutex_queue[4]);
				hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);    /* 送到 TQ_4 队列 */
				sem_post(&(empty[0]));
				pthread_mutex_unlock(&mutex_queue[4]);	
				
				//EPT(stderr, "hm_slot_thread: state = 0,send 0xff\n");
            	break;

        	case SCN:
				EPT(stderr, "hm_slot_thread: state = 1******************\n");
				hm_start_timer1();

				while(1)
				{
					sem_wait(&(empty[1]));  /* 等待缓冲区有数据+1，或者定时器到来+1，如果两者同时+1，定时器到来退出，数据依然没有读取，信号量还是1，下次可以继续读取 */

					if(timer_flag == 1)    /* 如果是定时器到来的话直接退出 */
					{
						EPT(stderr, "hm_slot_thread: timer1 up\n");
						timer_flag = 0;
						break;
					}

					sf_count = 1;          /* 表示读到了勤务帧 */

#if 0
					EPT(stderr, "hm_slot_thread: slot_cache1_flag = %d\n", slot_cache1_flag);
					EPT(stderr, "hm_slot_thread: slot_cache2_flag = %d\n", slot_cache2_flag);
#endif

					/* 从两个缓存区中读取数据 */
					if(slot_cache1_flag == 1)
					{
						hm_MAC_frame_rcv_proc1(&slot_cache1);

						slot_cache1_flag = 0;
					}

					if(slot_cache2_flag == 1)
					{
						hm_MAC_frame_rcv_proc1(&slot_cache2);

						slot_cache2_flag = 0;
					}

					/* 查看是否满足条件，如果满足则关闭定时器，退出循环 */
					for(i=0; i<netnum; i++)
					{
						if(neighbor_map_manage[i]->sf_flag == 1)
						{
							net_num++;    /* 记录满足条件(收到同一个NET节点两次勤务帧)的网络个数 */
						}
					}

					//EPT(stderr, "hm_slot_thread: j = %d\n", j);
					//EPT(stderr, "hm_slot_thread: netnum = %d\n", netnum);
					
					if(net_num == netnum)
					{
                         /* 全置0 */
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* 停止定时器 */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
					/*10.9 清0，防止重复计数*/
					else
						net_num = 0;
				}

				if(sf_count == 0)          /* 满足CON1_1   默认net_num为0 */
				{
					EPT(stderr, "1_1\n");
					/*********** 建网节点建立自己的邻节点维护表 ***********/
					/* 选定BS = 0 更新邻节点表维护表 */
					netID[netnum] = localID;

					neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* 给邻节点时隙表指针分配内存 */
					memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */

					neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* 给时隙表管理结构体分配指针 */
					memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* 初始化时隙表管理结构体 */

					neighbor_map[netnum]->netID = localID;
					neighbor_map[netnum]->BS_flag = 1;
					neighbor_map[netnum]->referenceID = 0xff;      /* 0级节点的基准节点相关信息全为0xff */
					neighbor_map[netnum]->rfclock_lv = 0xff;
					neighbor_map[netnum]->r_BS = 0xff;
					neighbor_map[netnum]->localID = localID;
					neighbor_map[netnum]->lcclock_lv = 0;
					neighbor_map[netnum]->l_BS = 0;					

					neighbor_map[netnum]->BS[0][localID].BS_ID = localID;
					neighbor_map[netnum]->BS[0][localID].BS_flag = 1;
					neighbor_map[netnum]->BS[0][localID].clock_lv = 0;
					neighbor_map[netnum]->BS[0][localID].state = NET;       /* 改变时隙表中的节点状态 */
					neighbor_map[netnum]->BS[0][localID].hop = 0;
					neighbor_map[netnum]->BS[0][localID].life = 0;
					
					neighbor_map[netnum]->BS[0][0].BS_ID++;
					neighbor_map[netnum]->BS_num++;

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[netnum]->netID;
					service_frame.referenceID = neighbor_map[netnum]->referenceID;
					service_frame.rfclock_lv = neighbor_map[netnum]->rfclock_lv;
					service_frame.r_BS = neighbor_map[netnum]->r_BS;
					service_frame.localID = neighbor_map[netnum]->localID;
					service_frame.lcclock_lv = neighbor_map[netnum]->lcclock_lv;
					service_frame.l_BS = neighbor_map[netnum]->l_BS;
					
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[netnum]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
							{					
								if(neighbor_map[netnum]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[netnum]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[netnum]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[netnum]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[netnum]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					service_frame.num = max+1;

					netnum++;   /* netnum现在应该为1 */

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

					/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 状态改变，生成状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** 生成LowMAC时隙表 ***********/
					net_i = 0;
					hm_LowMAC_slot_proc(net_i);    /* 参数为网络号 */

					/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					break;
				}

				if(net_num == netnum)            /* 满足CON1_2 记得清0操作 */
				{
					EPT(stderr, "1_2\n");
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** 选择加入的网络 ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(num < neighbor_map[i]->BS_num)
						{
							num = neighbor_map[i]->BS_num;
							net_i = i;     /* 网络号确定 */
						}
					}

					/*********** 选择上级节点/二维修改 11.07 ***********/
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						//EPT(stderr, "bs_num1 = %d\n", bs_num);
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							//EPT(stderr, "bs_num2 = %d\n", bs_num);
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* 上级节点时隙号确定 */
											node_i = j;  /* 上级节点节点号确定 */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}//EPT(stderr, "bs_num3 = %d\n", bs_num);
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** 选定BS，更新邻节点表维护表 ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* 此时的i即为本节点选定的时隙 */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* 改变时隙表中的节点状态 */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
					service_frame.num = max+1;

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

					/*********** 状态改变 ***********/
					nxt_state = WAN;

					/* 状态改变，生成状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** 生成LowMAC时隙表 ***********/
					hm_LowMAC_slot_proc(net_i);

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** 清空操作 ***********/
					net_num = 0;
					memset(neighbor_map_manage[net_i]->sf_NET_num, 0, sizeof(neighbor_map_manage[net_i]->sf_NET_num));
					memset(&(neighbor_map_manage[net_i]->sf_flag), 0, sizeof(U8));
					sf_count = 0;

					break;
				}

				if(net_num < netnum && net_num != 0)   /* 满足CON1_3 */
				{
					EPT(stderr, "1_3\n");
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** 选择加入的网络 ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(neighbor_map_manage[i]->sf_flag == 1)     /* 只选择加入满足条件(收到同一个NET节点两次勤务帧)的网络 */
						{
							if(num < neighbor_map[i]->BS_num)
							{
								num = neighbor_map[i]->BS_num;
								net_i = i;     /* 网络号确定 */
							}
						}
					}

					/*********** 选择上级节点 ***********/
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* 上级节点时隙号确定 */
											node_i = j;  /* 上级节点节点号确定 */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** 选定BS，更新邻节点表维护表 ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* 此时的i即为本节点选定的时隙 */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* 改变时隙表中的节点状态 */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;	
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					service_frame.num = max+1;					

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

					/*********** 状态改变 ***********/
					nxt_state = WAN;

					/* 状态改变，生成状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** 生成LowMAC时隙表 ***********/
					hm_LowMAC_slot_proc(net_i);

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** 清空操作 ***********/
					net_num = 0;
					memset(neighbor_map_manage[net_i]->sf_NET_num, 0, sizeof(neighbor_map_manage[net_i]->sf_NET_num));
					memset(&(neighbor_map_manage[net_i]->sf_flag), 0, sizeof(U8));
					sf_count = 0;

					break;
				}

            	else           /* 满足CON1_4 */
				{
					EPT(stderr, "1_4\n");
					/*********** 清空操作 ***********/
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* 保证后续使用的正确性 */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* 清空时隙生存期维护线程 10.30*/
												rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
												
												rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
												
												sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
											}
										}
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
											break;
									}
								}	
							}
							free(neighbor_map[i]);
							neighbor_map[i] = NULL;
							free(neighbor_map_manage[i]);
							neighbor_map_manage[i] = NULL;
							
							net_count++;
							if(net_count == netnum)
								break;								
						}							
					}						
					memset(netID, 0, sizeof(netID));
					netnum = 0;
					net_num = 0;
					sf_count = 0;
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** 状态改变 ***********/
					nxt_state = SCN;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

        	case WAN:
				sem_post(&pf_slot_sem);
				
				EPT(stderr, "hm_slot_thread: state = 2******************\n");
				hm_start_timer2();

				while(1)
				{
					sem_wait(&(empty[1]));	/* 等待缓冲区有数据+1，或者定时器到来+1，如果两者同时+1，定时器到来退出，数据依然没有读取，信号量还是1，下次可以继续读取 */

					if(timer_flag == 1)    /* 如果是定时器到来的话直接退出 */
					{
						timer_flag = 0;
						break;
					}

					sf_count = 1;		   /* 表示读到了勤务帧 */

					/* 从两个缓存区中读取数据 */
					if(slot_cache1_flag == 1)
					{
						rval = hm_MAC_frame_rcv_proc2(&slot_cache1, net_i);
						if(rval == 0x08)
						{
							//EPT(stderr, "case WAN sem_post\n");
							/* 发送勤务帧和LowMAC时隙表 */
							sem_post(&(empty[2]));
						}

						slot_cache1_flag = 0;
					}

					if(slot_cache2_flag == 1)
					{
						rval = hm_MAC_frame_rcv_proc2(&slot_cache2, net_i);
						if(rval == 0x08)
						{
							//EPT(stderr, "case WAN sem_post\n");
							/* 发送勤务帧和LowMAC时隙表 */
							sem_post(&(empty[2]));
						}

						slot_cache2_flag = 0;
					}

					//EPT(stderr, "hm_slot_thread: sf_rf_get = %d\n", neighbor_map_manage[net_i]->sf_rf_get);
					//EPT(stderr, "hm_slot_thread: sf_lc_get = %d\n", neighbor_map_manage[net_i]->sf_lc_get);

					/* 查看是否满足条件，如果满足则关闭定时器，退出循环 */
					if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag == 1)
					{
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* 停止定时器 */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
				}

				if(sf_count == 0)     /* 满足CON2_1 */
				{
					EPT(stderr, "2_1\n");
					/*********** 清空操作 ***********/
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* 保证后续使用的正确性 */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* 清空时隙生存期维护线程 10.30*/
												rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
												
												rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
												
												sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
											}
										}
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
											break;
									}
								}	
							}
							free(neighbor_map[i]);
							neighbor_map[i] = NULL;
							free(neighbor_map_manage[i]);
							neighbor_map_manage[i] = NULL;
							
							net_count++;
							if(net_count == netnum)
								break;								
						}							
					}						
					memset(netID, 0, sizeof(netID));
					netnum = 0;					
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** 状态改变 ***********/
					nxt_state = SCN;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
				}

				if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag== 0)     /* 满足CON2_2 */
				{
					EPT(stderr, "2_2\n");
					/*********** 状态改变 ***********/
					nxt_state = WAN;

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** 清空操作 ***********/
					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 1;
					sf_count = 0;

					break;
				}

				if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag == 1)      /* 满足CON2_3 */
				{
					EPT(stderr, "2_3\n");
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/**** 本节点状态改变 ****/
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].state = NET;   /* 当节点状态改变时，必须先更新时隙表!! */

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;			

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
					service_frame.num = max+1;

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

					/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/*********** 发送状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** 清空操作 ***********/
					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 0;
					sf_count = 0;

					break;
				}

				else          /* 满足CON2_4 */
				{
					EPT(stderr, "2_4\n");
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** 重新选择加入网络 ***********/
					/* 先清空本网络的参数 */
					neighbor_map[net_i]->BS_flag = 0;
					neighbor_map[net_i]->referenceID = 0;
					neighbor_map[net_i]->rfclock_lv = 0;
					neighbor_map[net_i]->r_BS = 0;
					neighbor_map[net_i]->localID = 0;
					neighbor_map[net_i]->lcclock_lv = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_ID = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_flag = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].clock_lv = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].state = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].hop = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].life = 0;
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* 两跳以内没有节点和本地节点占用的时隙冲突，直接清零 11.08 */
					neighbor_map[net_i]->l_BS = 0;
					neighbor_map[net_i]->BS_num--;

					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 0;
					sf_count = 0;

					/*********** 选择加入的网络 ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(num < neighbor_map[i]->BS_num)
						{
							num = neighbor_map[i]->BS_num;
							net_i = i;     /* 网络号确定 */
						}
					}

					/*********** 选择上级节点 ***********/
					/* 这里可以考虑排除上一次选定的上级节点，因为上一次的上级节点并不稳定 */
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* 上级节点时隙号确定 */
											node_i = j;  /* 上级节点节点号确定 */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** 选定BS，更新邻节点表维护表 ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* 此时的i即为本节点选定的时隙 */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* 改变时隙表中的节点状态 */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					service_frame.num = max+1;

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

					/*********** 状态改变 ***********/
					nxt_state = WAN;

					/* 状态改变，生成状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** 生成LowMAC时隙表 ***********/
					hm_LowMAC_slot_proc(net_i);

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					break;
				}

			case NET:
#ifdef _HM_TEST
				sem_post(&pf_slot_sem);
#endif				
				EPT(stderr, "hm_slot_thread: state = 3******************\n");
				hm_start_timer3();

				while(1)
				{
					sem_wait(&(empty[1]));	/* 等待缓冲区有数据+1，或者定时器到来+1，如果两者同时+1，定时器到来退出，数据依然没有读取，信号量还是1，下次可以继续读取 */
					
					if(timer_flag == 1)    /* 如果是定时器到来的话直接退出 */
					{
						timer_flag = 0;
						break;
					}

					sf_count = 1;		   /* 表示读到了勤务帧 */

					/* 从两个缓存区中读取数据 */
					if(slot_cache1_flag == 1)
					{	
						//EPT(stderr, "hm_slot_thread: net_i = %d\n", net_i);
						rval = hm_MAC_frame_rcv_proc3(&slot_cache1, net_i);
						if(rval == 0x08)
						{
							EPT(stderr, "hm_slot_thread1: case NET sem_post\n");
							/* 考虑发送勤务帧和LowMAC时隙表 */
							sem_post(&(empty[2]));
						}

						slot_cache1_flag = 0;
					}

					if(slot_cache2_flag == 1)
					{
						rval = hm_MAC_frame_rcv_proc3(&slot_cache2, net_i);
						if(rval == 0x08)
						{
							EPT(stderr, "hm_slot_thread2: case NET sem_post\n");
							/* 考虑发送勤务帧和LowMAC时隙表 */
							sem_post(&(empty[2]));
						}

						slot_cache2_flag = 0;
					}

					/* 查看是否满足条件，如果满足则关闭定时器，退出循环 */
					if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num > 1)
					{						
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* 停止定时器 */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "hm_slot_thread: error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
				}

            	if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && sf_count == 0)    /* 满足CON3_1 */
				{
					EPT(stderr, "3_1\n");
					
					/* 考虑发送勤务帧和LowMAC时隙表 
					sem_post(&(empty[2]));*/

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** 生成勤务帧 ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;
				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop != 2)
									{
										service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
										service_frame.BS[i].flag = 1;
										service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
										service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
										max = i;
										break;
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
						}
					}
					service_frame.num = max+1;

					/* 填充MAC帧 */
					memset(&mac_packet, 0, sizeof(mac_packet));
					mac_packet.pr = 0;
					mac_packet.type = 1;
					mac_packet.subt = 0;
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
					memcpy(mac_packet.data, &service_frame, sizeof(service_frame));
					
					/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** 生成LowMAC时隙表 ***********/
					hm_LowMAC_slot_proc(net_i);

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					break;
            	}

				if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 1)    /* 满足CON3_2 */
				{
					EPT(stderr, "3_2\n");
					/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** 清空操作 ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

				if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 0 && neighbor_map_manage[net_i]->sf_diffnet_get == 1)    /* 满足CON3_3 */
				{
					EPT(stderr, "3_3\n");
                	/*********** 清空操作 ***********/
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* 保证后续使用的正确性 */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* 清空时隙生存期维护线程 10.30*/
												rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
												
												rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
												
												sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
											}
										}
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
											break;
									}
								}	
							}
							free(neighbor_map[i]);
							neighbor_map[i] = NULL;
							free(neighbor_map_manage[i]);
							neighbor_map_manage[i] = NULL;
							
							net_count++;
							if(net_count == netnum)
								break;								
						}							
					}						
					memset(netID, 0, sizeof(netID));
					netnum = 0;
					sf_count = 0;
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** 状态改变 ***********/
					nxt_state = SCN;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && sf_count == 0)    /* 满足CON3_4 */
				{
					EPT(stderr, "3_4\n");
                	/*********** 清空操作 ***********/
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* 保证后续使用的正确性 */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* 清空时隙生存期维护线程 10.30*/
												rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
												
												rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
												
												sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
											}
										}
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
											break;
									}
								}	
							}
							free(neighbor_map[i]);
							neighbor_map[i] = NULL;
							free(neighbor_map_manage[i]);
							neighbor_map_manage[i] = NULL;
							
							net_count++;
							if(net_count == netnum)
								break;								
						}							
					}						
					memset(netID, 0, sizeof(netID));
					netnum = 0;
					
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** 状态改变 ***********/
					nxt_state = SCN;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 0 && neighbor_map_manage[net_i]->sf_diffnet_get == 1)    /* 满足CON3_5 */
				{
					EPT(stderr, "3_5\n");
                	/*********** 清空操作 ***********/
					/* 写锁 */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* 保证后续使用的正确性 */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* 清空时隙生存期维护线程 10.30*/
												rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
												
												rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
												rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
												if(rval != 0)						
													EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
												
												sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
											}
										}
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
											break;
									}
								}	
							}
							free(neighbor_map[i]);
							neighbor_map[i] = NULL;
							free(neighbor_map_manage[i]);
							neighbor_map_manage[i] = NULL;
							
							net_count++;
							if(net_count == netnum)
								break;								
						}							
					}						
					memset(netID, 0, sizeof(netID));
					netnum = 0;
					sf_count = 0;
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** 状态改变 ***********/
					nxt_state = SCN;

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num > 1)    /* 满足CON3_6 */
				{
					EPT(stderr, "3_6\n");
                	/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** 清空操作 ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num == 1)    /* 满足CON3_6_1 */
				{
					EPT(stderr, "3_6_1\n");
                	/*********** 状态改变 ***********/
					nxt_state = NET;

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/* 状态改变，下发状态改变帧 */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** 清空操作 ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

#if 1				
				/* 满足CON3_7 CON3_8 CON3_9 CON3_10 */
				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 1 && neighbor_map_manage[net_i]->sf_rf_num == 0)    
				{	
					/* 每次进入此状态都清零，便于之后使用 12.10 */
					int flag3_7 = 0;  /* 记录是否满足条件3_7 11.15 */
					int flag3_8 = 0;  /* 记录是否满足条件3_8 11.15 */
					int flag3_9 = 0;  /* 记录是否满足条件3_9 11.15 */

					/* 读锁 */
					pthread_rwlock_rdlock(&rw_lock);

					/* 判断具体满足哪个条件 */	
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* 保证后续使用的正确性 */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;

									/* 以下的判断均排除上级节点，因为收不到上级节点发送的勤务帧才会落入这个状态，此时的时隙表中可能还会残存着上级节点的信息(两跳) */
									if(neighbor_map[net_i]->BS[i][j].clock_lv <= neighbor_map[net_i]->rfclock_lv && neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET && neighbor_map[net_i]->BS[i][j].BS_ID != neighbor_map[net_i]->referenceID)
									{
										flag3_7 = 1;  /* 时隙表中有大于等于上级节点时钟级别的一跳NET邻节点，时钟级别越大数值越小 11.15 */
										break;
									}
									
									if(neighbor_map[net_i]->BS[i][j].clock_lv < neighbor_map[net_i]->lcclock_lv && neighbor_map[net_i]->BS[i][j].state == NET && neighbor_map[net_i]->BS[i][j].BS_ID != neighbor_map[net_i]->referenceID)
									{	
										flag3_8 = 1;  /* 两跳范围内有大于本节点时钟级别的NET节点(两跳内) 11.15 */
										EPT(stderr, "net_i = %d i = %d j = %d\n", net_i, i, j);
									}
									else 
									{	
										if(neighbor_map[net_i]->BS[i][j].clock_lv == neighbor_map[net_i]->lcclock_lv && neighbor_map[net_i]->BS[i][j].BS_ID < neighbor_map[net_i]->localID && neighbor_map[net_i]->BS[i][j].state == NET)
											flag3_9 = 1;  /* 两跳范围内有不大于本地时钟级别的节点，且本地节点不是节点号最小的<同级>NET节点 11.15 */
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
									break;
							}
							/* 满足3_7状态直接就可以退出，不满足3_7的状态下，3_8和3_9可以同时存在，判断满足什么条件的时候先判断3_8 12.10 */
							if(flag3_7 == 1)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
														
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);

					/* 时隙表中有大于等于上级节点时钟级别的一跳NET邻节点 */
					if(flag3_7 == 1)        /* 满足CON3_7 */
					{
						EPT(stderr, "3_7\n");
						
						/* 写锁 */
						pthread_rwlock_wrlock(&rw_lock);

						/*********** 先清空本网络的参数 ***********/
						/* 在重新选择上级节点、BS之前，要把之前选定占用的清空 11.01 */								
						neighbor_map[net_i]->BS_flag = 0;
						neighbor_map[net_i]->referenceID = 0;
						neighbor_map[net_i]->rfclock_lv = 0;
						neighbor_map[net_i]->r_BS = 0;
						neighbor_map[net_i]->localID = 0;
						neighbor_map[net_i]->lcclock_lv = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_ID = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_flag = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].clock_lv = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].state = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].hop = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].life = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* 两跳以内没有节点和本地节点占用的时隙冲突，直接清零 11.08 */
						neighbor_map[net_i]->l_BS = 0;
						neighbor_map[net_i]->BS_num--;  //EPT(stderr, "BS_num = %d\n", neighbor_map[net_i]->BS_num);

						neighbor_map_manage[net_i]->sf_samenet_get = 0;
						neighbor_map_manage[net_i]->sf_diffnet_get = 0;
						neighbor_map_manage[net_i]->sf_rf_num = 0;
						sf_count = 0;

						/*********** 重新选择上级节点 ***********/						
						/* 这里可以考虑排除上一次选定的上级节点，因为上一次的上级节点并不稳定 */
						level = 32;				
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
						{
							//EPT(stderr, "bs_num = %d\n", bs_num);
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;  //EPT(stderr, "bs_num = %d\n", bs_num);
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
								{					
									if(neighbor_map[net_i]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
										{
											//EPT(stderr, "i = %d j = %d\n", i, j);
											if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
											{
												level = neighbor_map[net_i]->BS[i][j].clock_lv;
												bs_i = i;    /* 上级节点时隙号确定 */   
												node_i = j;  /* 上级节点节点号确定 */	
												//EPT(stderr, "i = %d j = %d\n", i, j);
											}
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
										break;
								}
							}
						}
						
						neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
						neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
						neighbor_map[net_i]->r_BS = bs_i;

						EPT(stderr, "referenceID = %d rfclock_lv = %d r_BS = %d\n", neighbor_map[net_i]->referenceID, neighbor_map[net_i]->rfclock_lv, neighbor_map[net_i]->r_BS);

						/*********** 选定BS，更新邻节点表维护表 ***********/
						for(i=0; i<MAX_CFS_PSF; i++)
						{
							if(neighbor_map[net_i]->BS[i][0].BS_ID)
								continue;
							break;
						}
						/* 此时的i即为本节点选定的时隙 */
						neighbor_map[net_i]->BS_flag = 1;
						neighbor_map[net_i]->localID = localID;
						neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
						neighbor_map[net_i]->l_BS = i;
						neighbor_map[net_i]->BS_num++;

						neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
						neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
						neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
						neighbor_map[net_i]->BS[i][localID].state = WAN;    /* 改变时隙表中的节点状态 */
						neighbor_map[net_i]->BS[i][localID].hop = 0;
						neighbor_map[net_i]->BS[i][localID].life = 0;
						neighbor_map[net_i]->BS[i][0].BS_ID++;						

						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);

						/* 读锁 */
						pthread_rwlock_rdlock(&rw_lock);						

						/*********** 生成勤务帧 ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[net_i]->netID;
						service_frame.referenceID = neighbor_map[net_i]->referenceID;
						service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						service_frame.r_BS = neighbor_map[net_i]->r_BS;
						service_frame.localID = neighbor_map[net_i]->localID;
						service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						service_frame.l_BS = neighbor_map[net_i]->l_BS;

						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
								{					
									if(neighbor_map[net_i]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[net_i]->BS[i][j].hop != 2)
										{
											service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
											service_frame.BS[i].flag = 1;
											service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
											service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
											max = i;
											break;
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
										break;
								}
							}
						}
						service_frame.num = max+1;

						/* 填充MAC帧 */
						memset(&mac_packet, 0, sizeof(mac_packet));
						mac_packet.pr = 0;
						mac_packet.type = 1;
						mac_packet.subt = 0;
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
						memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

						/*********** 状态改变 ***********/
						nxt_state = WAN;

						/* 状态改变，生成状态改变帧 */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
						H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
						H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** 生成LowMAC时隙表 ***********/
						hm_LowMAC_slot_proc(net_i);

						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);

						/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);		

						break;
					}

					/* 两跳范围内有大于本节点时钟级别的NET节点 11.15 */
					if(flag3_8 == 1)  /* 满足CON3_8 */
					{
						EPT(stderr, "3_8\n");

						/* 写锁 */
						pthread_rwlock_wrlock(&rw_lock);

						/*********** 先清空本网络的参数 ***********/
						/* 在重新选择上级节点、BS之前，要把之前选定占用的清空 11.01 */								
						neighbor_map[net_i]->BS_flag = 0;
						neighbor_map[net_i]->referenceID = 0;
						neighbor_map[net_i]->rfclock_lv = 0;
						neighbor_map[net_i]->r_BS = 0;
						neighbor_map[net_i]->localID = 0;
						neighbor_map[net_i]->lcclock_lv = 0; //EPT(stderr, "l_BS = %d ID = %d\n", neighbor_map[net_i]->l_BS, neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_ID);
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_ID = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].BS_flag = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].clock_lv = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].state = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].hop = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].life = 0;
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* 两跳以内没有节点和本地节点占用的时隙冲突，直接清零 11.08 */
						neighbor_map[net_i]->l_BS = 0;
						neighbor_map[net_i]->BS_num--;

						neighbor_map_manage[net_i]->sf_samenet_get = 0;
						neighbor_map_manage[net_i]->sf_diffnet_get = 0;
						neighbor_map_manage[net_i]->sf_rf_num = 0;
						sf_count = 0;

						/*********** 重新选择上级节点 ***********/						
						/* 这里可以考虑排除上一次选定的上级节点，因为上一次的上级节点并不稳定 */
						level = 32;				
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
								{					
									if(neighbor_map[net_i]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
										{
											if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
											{
												level = neighbor_map[net_i]->BS[i][j].clock_lv;
												bs_i = i;    /* 上级节点时隙号确定 */
												node_i = j;  /* 上级节点节点号确定 */
											}
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
										break;
								}
							}
						}
						
						neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
						neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
						neighbor_map[net_i]->r_BS = bs_i;	
						
						EPT(stderr, "referenceID = %d rfclock_lv = %d r_BS = %d\n", neighbor_map[net_i]->referenceID, neighbor_map[net_i]->rfclock_lv, neighbor_map[net_i]->r_BS);

						/*********** 选定BS，更新邻节点表维护表 ***********/
						for(i=0; i<MAX_CFS_PSF; i++)
						{
							if(neighbor_map[net_i]->BS[i][0].BS_ID)
								continue;
							break;
						}
						/* 此时的i即为本节点选定的时隙 */
						neighbor_map[net_i]->BS_flag = 1;
						neighbor_map[net_i]->localID = localID;
						neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
						neighbor_map[net_i]->l_BS = i;
						neighbor_map[net_i]->BS_num++;

						neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
						neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
						neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
						neighbor_map[net_i]->BS[i][localID].state = WAN;    /* 改变时隙表中的节点状态 */
						neighbor_map[net_i]->BS[i][localID].hop = 0;
						neighbor_map[net_i]->BS[i][localID].life = 0;
						neighbor_map[net_i]->BS[i][0].BS_ID++;						

						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);

						/* 读锁 */
						pthread_rwlock_rdlock(&rw_lock);						

						/*********** 生成勤务帧 ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[net_i]->netID;
						service_frame.referenceID = neighbor_map[net_i]->referenceID;
						service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						service_frame.r_BS = neighbor_map[net_i]->r_BS;
						service_frame.localID = neighbor_map[net_i]->localID;
						service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						service_frame.l_BS = neighbor_map[net_i]->l_BS;

						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
								{					
									if(neighbor_map[net_i]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[net_i]->BS[i][j].hop != 2)
										{
											service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
											service_frame.BS[i].flag = 1;
											service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
											service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
											max = i;
											break;
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
										break;
								}
							}
						}
						service_frame.num = max+1;

						/* 填充MAC帧 */
						memset(&mac_packet, 0, sizeof(mac_packet));
						mac_packet.pr = 0;
						mac_packet.type = 1;
						mac_packet.subt = 0;
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
						memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

						/*********** 状态改变 ***********/
						nxt_state = WAN;

						/* 状态改变，生成状态改变帧 */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
						H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
						H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						H2L_MAC_frame.state = 1;          /* 节点状态变为 WAN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** 生成LowMAC时隙表 ***********/
						hm_LowMAC_slot_proc(net_i);

						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);

						/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);		

						break;
					}
					
					/* 两跳范围内<只有>不大于本地时钟级别的节点，且本地节点不是节点号最小的同级NET节点 11.15 */				
					if(flag3_9 == 1)     /* 满足CON3_9 */
					{
						EPT(stderr, "3_9\n");						

						/*********** 清空操作 ***********/
						/* 写锁 */
						pthread_rwlock_wrlock(&rw_lock);
						
						net_count = 0;
						for(i=0; i<MAX_NET_NUM; i++)
						{
							//U8 bs;
							if(netID[i] != 0)
							{	
								for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
								{
									if(bs_num == neighbor_map[i]->BS_num)
									{
										bs_num = 0;  /* 保证后续使用的正确性 */
										break;
									}
									if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
									{
										bs_num++;
										node_num = 0;
										for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
										{
											if(neighbor_map[i]->BS[bs][j].BS_flag)   
											{
												node_num++;
												if(neighbor_map[i]->BS[bs][j].life)
												{
													/* 清空时隙生存期维护线程 10.30*/
													rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
													rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
													
													rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
													rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
													
													sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
												}
											}
											if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
												break;
										}
									}	
								}
								free(neighbor_map[i]);
								neighbor_map[i] = NULL;
								free(neighbor_map_manage[i]);
								neighbor_map_manage[i] = NULL;
								
								net_count++;
								if(net_count == netnum)
									break;								
							}							
						}						
						memset(netID, 0, sizeof(netID));
						netnum = 0;
						sf_count = 0;
						
						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);						

						/*********** 状态改变 ***********/
						nxt_state = SCN;

						/* 状态改变，下发状态改变帧 */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = 0xff;
						H2L_MAC_frame.rfclock_lv = 0xff;
						H2L_MAC_frame.localID = localID;
						H2L_MAC_frame.lcclock_lv = 0Xff;
						H2L_MAC_frame.state = 0;          /* 节点状态变为 SCN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						/* 延时入网 1.10 
						usleep(300000);*/
						sleep(1*localID);

						break;
					}
				
					/* 两跳范围内只有不大于本地时钟级别的节点，且本地节点是节点号最小的同级NET节点 11.15 */	
					else    /* 满足CON3_10 */
					{
						EPT(stderr, "3_10\n");
						
						/*********** 清空操作 ***********/
						/* 写锁 */
						pthread_rwlock_wrlock(&rw_lock);
						
						net_count = 0;
						for(i=0; i<MAX_NET_NUM; i++)
						{
							//U8 bs;
							if(netID[i] != 0)
							{	
								for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
								{
									if(bs_num == neighbor_map[i]->BS_num)
									{
										bs_num = 0;  /* 保证后续使用的正确性 */
										break;
									}
									if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
									{
										bs_num++;
										node_num = 0;
										for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
										{
											if(neighbor_map[i]->BS[bs][j].BS_flag)   
											{
												node_num++;
												if(neighbor_map[i]->BS[bs][j].life)
												{
													/* 清空时隙生存期维护线程 10.30*/
													rval = pthread_cancel(neighbor_map_manage[i]->bs_timer[bs][j]);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_cancel error code1 %d\n", rval);
													rval = pthread_join(neighbor_map_manage[i]->bs_timer[bs][j], &thread_result);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_join error code1 %d\n", rval);							
													
													rval = pthread_cancel(neighbor_map_manage[i]->bs_tid[bs][j]);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_cancel error code2 %d\n", rval);
													rval = pthread_join(neighbor_map_manage[i]->bs_tid[bs][j], &thread_result);
													if(rval != 0)						
														EPT(stderr, "CON3_10: pthread_join error code2 %d\n", rval);	
													
													sem_destroy(&(neighbor_map_manage[i]->bs_sem[bs][j]));
												}
											}
											if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
												break;
										}
									}	
								}
								free(neighbor_map[i]);
								neighbor_map[i] = NULL;
								free(neighbor_map_manage[i]);
								neighbor_map_manage[i] = NULL;
								
								net_count++;
								if(net_count == netnum)
									break;								
							}							
						}						
						memset(netID, 0, sizeof(netID));
						netnum = 0;
						sf_count = 0;
						
						/* 解锁 */
						pthread_rwlock_unlock(&rw_lock);

						/*********** 建网节点建立自己的邻节点维护表 ***********/
						/* 选定BS = 0 更新邻节点表维护表 */
						netID[netnum] = localID;

						neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* 给邻节点时隙表指针分配内存 */
						memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */

						neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* 给时隙表管理结构体分配指针 */
						memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* 初始化时隙表管理结构体 */

						neighbor_map[netnum]->netID = localID;
						neighbor_map[netnum]->BS_flag = 1;
						neighbor_map[netnum]->referenceID = 0xff;      /* 0级节点的基准节点相关信息全为0xff */
						neighbor_map[netnum]->rfclock_lv = 0xff;
						neighbor_map[netnum]->r_BS = 0xff;
						neighbor_map[netnum]->localID = localID;
						neighbor_map[netnum]->lcclock_lv = 0;
						neighbor_map[netnum]->l_BS = 0;					

						neighbor_map[netnum]->BS[0][localID].BS_ID = localID;
						neighbor_map[netnum]->BS[0][localID].BS_flag = 1;
						neighbor_map[netnum]->BS[0][localID].clock_lv = 0;
						neighbor_map[netnum]->BS[0][localID].state = NET;       /* 改变时隙表中的节点状态 */
						neighbor_map[netnum]->BS[0][localID].hop = 0;
						neighbor_map[netnum]->BS[0][localID].life = 0;
						
						neighbor_map[netnum]->BS[0][0].BS_ID++;
						neighbor_map[netnum]->BS_num++;
						
						/*********** 生成勤务帧 ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[netnum]->netID;
						service_frame.referenceID = neighbor_map[netnum]->referenceID;
						service_frame.rfclock_lv = neighbor_map[netnum]->rfclock_lv;
						service_frame.r_BS = neighbor_map[netnum]->r_BS;
						service_frame.localID = neighbor_map[netnum]->localID;
						service_frame.lcclock_lv = neighbor_map[netnum]->lcclock_lv;
						service_frame.l_BS = neighbor_map[netnum]->l_BS;
						
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
						{
							if(bs_num == neighbor_map[netnum]->BS_num)
							{
								bs_num = 0;  /* 保证后续使用的正确性 */
								break;
							}
							if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32节点轮询 */
								{					
									if(neighbor_map[netnum]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[netnum]->BS[i][j].hop != 2)
										{
											service_frame.BS[i].BS_ID = neighbor_map[netnum]->BS[i][j].BS_ID;
											service_frame.BS[i].flag = 1;
											service_frame.BS[i].clock_lv = neighbor_map[netnum]->BS[i][j].clock_lv;
											service_frame.BS[i].state = neighbor_map[netnum]->BS[i][j].state;
											max = i;
											break;
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
										break;
								}
							}
						}
						service_frame.num = max+1;
						
						EPT(stderr, "3_10: new netNO = %d netID = %d\n", netnum, neighbor_map[netnum]->netID);				

						netnum++;   /* netnum现在应该为1 */

						/* 填充MAC帧 */
						memset(&mac_packet, 0, sizeof(mac_packet));
						mac_packet.pr = 0;
						mac_packet.type = 1;
						mac_packet.subt = 0;
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
						memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

						/*********** 状态改变 ***********/
						nxt_state = NET;

						/* 状态改变，生成状态改变帧 */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = 0xff;
						H2L_MAC_frame.rfclock_lv = 0xff;
						H2L_MAC_frame.localID = localID;
						H2L_MAC_frame.lcclock_lv = 0;
						H2L_MAC_frame.state = 7;          /* 节点状态变为 NET */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** 生成LowMAC时隙表 ***********/
						net_i = 0;
						hm_LowMAC_slot_proc(net_i);    /* 参数为网络号 */

						/*********** 发送LowMAC时隙表 状态改变帧 勤务帧 ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* 送到 TQ_4 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);

						break;
					}
				}
#endif				

				else   /* 以上哪个条件也不符合 */
				{					
					EPT(stderr, "hm_slot_thread: net_i = %d netID = %d localID = %d sf_samenet_get = %d sf_diffnet_get = %d\n", net_i, neighbor_map[net_i]->netID, neighbor_map[net_i]->localID, neighbor_map_manage[net_i]->sf_samenet_get, neighbor_map_manage[net_i]->sf_diffnet_get);	
					EPT(stderr, "hm_slot_thread: no choice in NET\n");
					break;
				}

			default:
				EPT(stderr, "highmac layer slot process receive unknown state, no=%d\n", cur_state);
				break;
    	}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.slot_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: slot thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}

void hm_MAC_frame_rcv_proc1(lm_packet_t *cache_p)
{
	U8   id;       /* for  id = service_frame_p->netID*/
	U8   i;        /* for  for(i=0; i<netnum; i++)   记录时隙号 */
	U8   net_id;   /* 记录当前网络号!!!!! */
	U8   node;     /* 记录占用某时隙的节点号 11.06 */
	U8   net_count = 0;  /* 选择网络时计数 1.11 */  
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc1: cache_p->type = %d\n", cache_p->type);	

	switch(cache_p->type)
	{
		case 0x08:    /* 收到的是勤务帧 */

			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);

			id = service_frame_p->netID;
			/*
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->netID = %d\n", service_frame_p->netID);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->referenceID = %d\n", service_frame_p->referenceID);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->rfclock_lv = %d\n", service_frame_p->rfclock_lv);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->r_BS = %d\n", service_frame_p->r_BS);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->localID = %d\n", service_frame_p->localID);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->lcclock_lv = %d\n", service_frame_p->lcclock_lv);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->l_BS = %d\n", service_frame_p->l_BS);
			EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->num = %d\n", service_frame_p->num);
			*/
			if(netnum == 0)   /* 网络数为0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* 给邻节点时隙表指针分配内存 */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* 给时隙表管理结构体分配指针 */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* 初始化时隙表管理结构体 */

				neighbor_map_p = neighbor_map[netnum];    /* 记录当前邻节点表指针 */
				neighbor_map_p->netID = id;     /* 为新的时隙表填充网络号 */ 
				
				EPT(stderr, "hm_MAC_frame_rcv_proc1: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				
				
				net_id = netnum;               /* 记录当前网络号 */

				netnum++;
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* 此勤务帧属于现有网络节点 */
					{
						neighbor_map_p = neighbor_map[i];  /* 记录当前邻节点表指针 */
						net_id = i;      /* 记录当前网络号 */   
						
						EPT(stderr, "hm_MAC_frame_rcv_proc1: old netNO = %d netID = %d\n", net_id, id);
						break;
					}
					else
					{
						if(netID[i] != 0)
						{
							net_count++;
							if(net_count == netnum)
								break;
						}
					}
				}
				
				if(net_count == netnum)        /* 此勤务帧来自新网络节点 */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* 记录新网络的零级节点号 */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* 给邻节点时隙表指针分配内存 */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* 给时隙表管理结构体分配指针 */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* 初始化时隙表管理结构体 */
		
							neighbor_map_p = neighbor_map[i];	   /* 记录当前邻节点表指针 */
							neighbor_map_p->netID = id; 	/* 为新的时隙表填充网络号 */
							net_id = i;	   /* 记录当前网络号 */
							EPT(stderr, "hm_MAC_frame_rcv_proc1: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}

			/* 记录收到NET节点勤务帧的次数 */
			if(service_frame_p->BS[service_frame_p->l_BS].state == NET)
			{
				neighbor_map_manage[net_id]->sf_NET_num[service_frame_p->l_BS]++;    /* 发来勤务帧的节点对应时隙计数加1 */
				if(neighbor_map_manage[net_id]->sf_NET_num[service_frame_p->l_BS] == 2)
				{
					neighbor_map_manage[net_id]->sf_flag = 1;     /* 收到同一个NET节点的勤务帧2次 */
				}
			}

			/* 开始对勤务帧中每一个表项轮询 */
			for(i=0; i<service_frame_p->num; i++)
			{
				if(service_frame_p->BS[i].flag == 0)    /* 判断勤务帧内此节点是否为有效数据 */
					continue;
				/*
				EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->BS[%d].flag = %d\n", i, service_frame_p->BS[i].flag);
				EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->BS[%d].BS_ID = %d\n", i, service_frame_p->BS[i].BS_ID);
				EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->BS[%d].state = %d\n", i, service_frame_p->BS[i].state);
				EPT(stderr, "hm_MAC_frame_rcv_proc1: service_frame_p->BS[%d].clock_lv = %d\n\n", i, service_frame_p->BS[i].clock_lv);
				*/

				node = service_frame_p->BS[i].BS_ID;

				if(service_frame_p->BS[i].BS_ID == localID)
				{
					continue;
				}

				/* 开启对应的时隙维护线程 */					
				if(neighbor_map_p->BS[i][node].life == 0)  /* 判断此时隙维护线程是否开启 */
				{
					sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]), 0, 0);  /* 初始化相应信号量，开启对应的时隙维护线程 */
					neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
					
					pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
					neighbor_map_p->BS[i][node].life = 1;
				}
				/* 将勤务帧数据对应拷贝到时隙表中 */
				neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
				neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
				neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;

				/* 更新被占用的时隙个数 */
				if(neighbor_map_p->BS[i][node].BS_flag == 0)
				{
					neighbor_map_p->BS[i][node].BS_flag = 1;
					if(neighbor_map_p->BS[i][0].BS_ID == 0)
					{
						neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
					}
					neighbor_map_p->BS[i][0].BS_ID++;
				}

				/* 跳数的判断 */
				if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
				{
					neighbor_map_p->BS[i][node].hop = 1;
					sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
				}
				else
				{
					if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
					{
						neighbor_map_p->BS[i][node].hop = 2;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
					}
					else
					{
						neighbor_map_p->BS[i][node].hop = 1;
						//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
					}
				}
			}

			/* 解锁 */
			pthread_rwlock_unlock(&rw_lock);
			break;

		case 0xff:    /* 收到的是LowMAC反馈帧 */
			break;

		case 0001:    /* 收到的是时隙预约有关帧 */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%d\n", cache_p->type);
			break;
	}
}

U8 hm_MAC_frame_rcv_proc2(lm_packet_t *cache_p, U8 net_i)  /* 参数net_i是本地节点加入的网络号 1.12 */
{
	U8   id;        /* for  id = service_frame_p->netID*/
	U8   i;         /* for  for(i=0; i<netnum; i++)   记录时隙号 */
	U8   net_id;   /* 记录当前勤务帧所在的网络号!!!!! */
	U8   node;     /* 记录占用某时隙的节点号 11.06 */	
	U8   net_count = 0;  /* 选择网络时计数 1.11 */  
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc2: cache_p->type = %d\n", cache_p->type);

	switch(cache_p->type)
	{
		case 0x08:    /* 收到的是勤务帧 */

			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);

			id = service_frame_p->netID;
			//EPT(stderr, "hm_MAC_frame_rcv_proc2: id = %d\n", id);

			if(netnum == 0)   /* 网络数为0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* 给邻节点时隙表指针分配内存 */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* 给时隙表管理结构体分配指针 */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* 初始化时隙表管理结构体 */

				neighbor_map_p = neighbor_map[netnum];    /* 记录当前邻节点表指针 */
				neighbor_map_p->netID = id;     /* 为新的时隙表填充网络号 */
				net_id = netnum;               /* 记录当前网络号 */
				
				EPT(stderr, "hm_MAC_frame_rcv_proc2: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				

				netnum++;
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* 此勤务帧属于现有网络节点 */
					{
						neighbor_map_p = neighbor_map[i];  /* 记录当前邻节点表指针 */
						net_id = i;      /* 记录当前网络号 */   
						
						EPT(stderr, "hm_MAC_frame_rcv_proc2: old netNO = %d netID = %d\n", net_id, id);
						break;
					}
					else
					{
						if(netID[i] != 0)
						{
							net_count++;
							if(net_count == netnum)
								break;
						}
					}
				}
				
				if(net_count == netnum)        /* 此勤务帧来自新网络节点 */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* 记录新网络的零级节点号 */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* 给邻节点时隙表指针分配内存 */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* 给时隙表管理结构体分配指针 */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* 初始化时隙表管理结构体 */
		
							neighbor_map_p = neighbor_map[i];	   /* 记录当前邻节点表指针 */
							neighbor_map_p->netID = id; 	/* 为新的时隙表填充网络号 */
							net_id = i;	   /* 记录当前网络号 */
							EPT(stderr, "hm_MAC_frame_rcv_proc2: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}
			
			if(net_id == net_i)     /* 表明收到的勤务帧属于本网络 */
			{
				if(service_frame_p->localID == neighbor_map_p->referenceID)    /* 表明收到上级节点勤务帧 */
				{
					neighbor_map_manage[net_id]->sf_rf_get = 1;

					//EPT(stderr, "11\n");

					/* 开始对勤务帧中每一个表项轮询 */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)    /* 判断勤务帧内此节点是否为有效数据 */
							continue;

						node = service_frame_p->BS[i].BS_ID;  //EPT(stderr, "BS[%d].BS_ID = %d\n", i, node);

						/* 开启对应的时隙维护线程 */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* 判断勤务帧内此节点是不是本节点 */
						{
							/* 如果收到的本节点信息与现有不符，则舍弃 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;
							
							neighbor_map_manage[net_id]->sf_lc_get = 1;   /* 上级节点勤务帧中包含本节点信息 */
							
							/*neighbor_map_p->BS[i].life = 0;
							pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
						}
						else
						{
							if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* 将勤务帧数据对应拷贝到时隙表中 */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* 将勤务帧数据对应拷贝到时隙表中 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* 更新被占用的时隙个数 */
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* 跳数的判断 */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}

							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
							}
						}
					}

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}

				else
				{
					//EPT(stderr, "22\n");
					/* 开始对勤务帧中每一个表项轮询 */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)    /* 判断勤务帧内此节点是否为有效数据 */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* 开启对应的时隙维护线程 */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* 判断勤务帧内此节点是不是本节点 */
						{
							/* 如果收到的本节点信息与现有不符，则舍弃 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;
							
							/*neighbor_map_p->BS[i].life = 0;
							pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
						}
						else
						{		
							if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* 将勤务帧数据对应拷贝到时隙表中 */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* 将勤务帧数据对应拷贝到时隙表中 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* 更新被占用的时隙个数 */						
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* 跳数的判断 */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
							}
						}
					}

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}

			else  /* 表明收到的勤务帧不属于本网络 */
			{
				/* 开始对勤务帧中每一个表项轮询 */
				for(i=0; i<service_frame_p->num; i++)
				{
					if(service_frame_p->BS[i].flag == 0)    /* 判断勤务帧内此节点是否为有效数据 */
						continue;

					node = service_frame_p->BS[i].BS_ID;

					/* 开启对应的时隙维护线程 */					
					if(service_frame_p->BS[i].BS_ID == localID)    /* 判断勤务帧内此节点是不是本节点 */
					{
						continue;
						/*neighbor_map_p->BS[i].life = 0;
						pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
						pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
						sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
					}
					else
					{							
						if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
						{
							sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
							neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
							
							pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
							neighbor_map_p->BS[i][node].life = 1;
						}
						/* 将勤务帧数据对应拷贝到时隙表中 */
						neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
					}								

					/* 更新被占用的时隙个数 */					
					if(neighbor_map_p->BS[i][node].BS_flag == 0)
					{
						neighbor_map_p->BS[i][node].BS_flag = 1;
						if(neighbor_map_p->BS[i][0].BS_ID == 0)
						{
							neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
						}
						neighbor_map_p->BS[i][0].BS_ID++;
					}

					/* 跳数的判断 */
					if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
					{
						neighbor_map_p->BS[i][node].hop = 1;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
					}
					else
					{
						if(neighbor_map_p->BS_flag == 1)            /* 如果此时隙表选定了BS   判断勤务帧内此节点是不是本节点 */
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}
						}

						if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
						{
							neighbor_map_p->BS[i][node].hop = 2;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
						}
						else
						{
							neighbor_map_p->BS[i][node].hop = 1;
							//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
						}
					}
				}

				/* 解锁 */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}


		case 0xff:    /* 收到的是LowMAC反馈帧 */
			break;

		case 0001:    /* 收到的是时隙预约有关帧 */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%x", cache_p->type);
			break;
	}

	return(cache_p->type);   /* 返回类型值 */
}

U8 hm_MAC_frame_rcv_proc3(lm_packet_t *cache_p, U8 net_i)
{
	U8	 id;		/* for	id = service_frame_p->netID*/
	U8	 i; 		/* for	for(i=0; i<netnum; i++)   记录时隙号 */
	U8	 net_id;   /* 记录当前网络号!!!!! */
	U8   node;     /* 记录占用某时隙的节点号 11.06 */	
	int  res;
	U8   net_count = 0;  /* 选择网络时计数 1.11 */
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc3: cache_p->type = %d\n", cache_p->type);

	switch(cache_p->type)
	{
		case 0x08:	  /* 收到的是勤务帧 */

			/* 写锁 */
			pthread_rwlock_wrlock(&rw_lock);

			id = service_frame_p->netID;
			//EPT(stderr, "hm_MAC_frame_rcv_proc3: id = %d\n", id);

			if(netnum == 0)   /* 网络数为0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));	   /* 给邻节点时隙表指针分配内存 */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));	/* 给时隙表管理结构体分配指针 */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));	/* 初始化时隙表管理结构体 */

				neighbor_map_p = neighbor_map[netnum];	  /* 记录当前邻节点表指针 */
				neighbor_map_p->netID = id; 	/* 为新的时隙表填充网络号 */
				net_id = netnum;			   /* 记录当前网络号 */

				EPT(stderr, "hm_MAC_frame_rcv_proc3: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				

				netnum++;      /* 现在网络数为1 */
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* 此勤务帧属于现有网络节点 */
					{
						neighbor_map_p = neighbor_map[i];  /* 记录当前邻节点表指针 */
						net_id = i;      /* 记录当前网络号 */   
						
						EPT(stderr, "hm_MAC_frame_rcv_proc3: old netNO = %d netID = %d\n", net_id, id);
						break;
					}
					else
					{
						if(netID[i] != 0)
						{
							net_count++;
							if(net_count == netnum)
								break;
						}
					}
				}
				
				if(net_count == netnum)        /* 此勤务帧来自新网络节点 */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* 记录新网络的零级节点号 */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* 给邻节点时隙表指针分配内存 */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* 初始化邻节点时隙表 */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* 给时隙表管理结构体分配指针 */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* 初始化时隙表管理结构体 */
		
							neighbor_map_p = neighbor_map[i];	   /* 记录当前邻节点表指针 */
							neighbor_map_p->netID = id; 	/* 为新的时隙表填充网络号 */
							net_id = i;	   /* 记录当前网络号 */
							EPT(stderr, "hm_MAC_frame_rcv_proc3: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}

			if(net_id == net_i) 	/* 表明收到的勤务帧属于本网络 */
			{
				/* 记录收到本网络勤务帧 */
				neighbor_map_manage[net_id]->sf_samenet_get = 1;			
				EPT(stderr, "hm_MAC_frame_rcv_proc3: samenet sf\n");	

				if(service_frame_p->localID == neighbor_map_p->referenceID)    /* 表明收到上级节点发送的勤务帧 */
				{
					EPT(stderr, "hm_MAC_frame_rcv_proc3: rf sf\n");
					neighbor_map_manage[net_id]->sf_rf_num++;  /* 记录收到上级节点勤务帧的次数/修改为大于1次 1.10 */					

					/* 开始对勤务帧中每一个表项轮询 */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)	/* 判断勤务帧内此节点是否为有效数据 */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* 开启对应的时隙维护线程 */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* 判断勤务帧内此节点是不是本节点 */
						{
							/* 如果收到的本节点信息与现有不符，则舍弃 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;			

							/*neighbor_map_p->BS[i].life = 0;
							res = pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							EPT(stderr, "proc3: pthread_cancel error1 code %d\n", res);
							res = pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							EPT(stderr, "proc3: pthread_cancel error2 code %d\n", res);
							res = sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));
							EPT(stderr, "proc3: sem_destroy error code %d\n", res);*/
						}
						else
						{							
							if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* 将勤务帧数据对应拷贝到时隙表中 */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* 将勤务帧数据对应拷贝到时隙表中 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* 更新被占用的时隙个数 */						
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* 跳数的判断 */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
							}
						}
					}

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}

				else
				{
					/* 开始对勤务帧中每一个表项轮询 */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)	/* 判断勤务帧内此节点是否为有效数据 */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* 开启对应的时隙维护线程 */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* 判断勤务帧内此节点是不是本节点 */
						{
							/* 如果收到的本节点信息与现有不符，则舍弃 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;
							
							/*neighbor_map_p->BS[i].life = 0;					
							res = pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							EPT(stderr, "proc3: pthread_cancel error1 code %d\n", res);
							res = pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							EPT(stderr, "proc3: pthread_cancel error2 code %d\n", res);
							res = sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));
							EPT(stderr, "proc3: sem_destroy error code %d\n", res);*/
						}
						else
						{
							if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
								//EPT(stderr, "test\n");
							}
							/* 将勤务帧数据对应拷贝到时隙表中 */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* 将勤务帧数据对应拷贝到时隙表中 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* 更新被占用的时隙个数 */
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* 跳数的判断 */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
							}
						}
					}

					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}

			else		 /* 表明收到的勤务帧不属于本网络 */
			{
				EPT(stderr, "hm_MAC_frame_rcv_proc3: diffnet sf\n");
				/* 记录收到异网络勤务帧/修改为net_i 1.12 */
				neighbor_map_manage[net_i]->sf_diffnet_get = 1;

				/* 开始对勤务帧中每一个表项轮询 */
				for(i=0; i<service_frame_p->num; i++)
				{
					if(service_frame_p->BS[i].flag == 0)	/* 判断勤务帧内此节点是否为有效数据 */
						continue;
					
					node = service_frame_p->BS[i].BS_ID;  /* 修补 11.23 */

					/* 开启对应的时隙维护线程 */					
					if(service_frame_p->BS[i].BS_ID == localID)    /* 判断勤务帧内此节点是不是本节点 */
					{
						continue;
						/*neighbor_map_p->BS[i].life = 0;
						pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
						pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
						sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
					}
					else
					{							
						if(neighbor_map_p->BS[i][node].life == 0)     /* 判断此时隙维护线程是否开启 */
						{
							sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* 初始化相应信号量，开启对应的时隙维护线程 */
							neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* 记录网络号、时隙号、节点号 */
							
							pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
							neighbor_map_p->BS[i][node].life = 1;
						}
						/* 将勤务帧数据对应拷贝到时隙表中 */
						neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
					}

					/* 更新被占用的时隙个数 */					
					if(neighbor_map_p->BS[i][node].BS_flag == 0)
					{
						neighbor_map_p->BS[i][node].BS_flag = 1;
						if(neighbor_map_p->BS[i][0].BS_ID == 0)
						{
							neighbor_map_p->BS_num++;     /* 更新被占用的时隙个数 */
						}
						neighbor_map_p->BS[i][0].BS_ID++;
					}

					/* 跳数的判断 */
					if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* 判断勤务帧内此节点是不是勤务帧的发送节点 */
					{
						neighbor_map_p->BS[i][node].hop = 1;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* 此时隙更新完成，发出信号 */
					}
					else
					{
						if(neighbor_map_p->BS_flag == 1)			/* 如果此时隙表选定了BS   判断勤务帧内此节点是不是本节点 */
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}
						}
						
						/* 有可能是一跳节点通过其他节点发来的勤务帧，这样就判断如果不是一跳再改成两跳 */
						if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* 如果此时隙表对应节点的跳数为0或2 置2 否则 置1 */
						{
							neighbor_map_p->BS[i][node].hop = 2;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* 此时隙更新完成，发出信号 */
						}
						else  /* 如果是通过其他节点发来的一跳节点的信息，那么不置信号量，等此节点自己发送 */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* 此时隙更新完成，发出信号 */
						}
					}
				}

				/* 解锁 */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}


		case 0xff:	  /* 收到的是LowMAC反馈帧 */
			break;

		case 0001:	  /* 收到的是时隙预约有关帧 */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%x", cache_p->type);
			break;
	}

	//EPT(stderr, "hm_MAC_frame_rcv_proc3: cache_p->type = %d\n", cache_p->type);
	return(cache_p->type);   /* 返回类型值 */
}

/* 生存期修改 11.07 */
void *hm_neighbor_map_thread(void *i_p)
{
	U8 net_i = (*(U32 *)i_p) >> 16;
	U8 bs_i = (*(U32 *)i_p) >> 8;
	U8 node_i = *(U32 *)i_p;
	int res;
	void *thread_result;

	EPT(stderr, "hm_neighbor_map_thread: net_i = %d  bs_i = %d  node = %d\n", net_i, bs_i, node_i);

	res = pthread_create(&(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]), NULL, hm_bs_life_thread, i_p);  /* 先开启一次 */ 
	//EPT(stderr, "pthread_create error code0 %d\n", res);

	while(1)   /* 模拟邻节点表项的更新 */
	{
		sem_wait(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));

		/* 写锁 */
		pthread_rwlock_wrlock(&rw_lock);
		if(neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] == 1)   /* 定时器到达 */
		{
			neighbor_map[net_i]->BS[bs_i][node_i].BS_ID = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].BS_flag = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].clock_lv = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].state = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].hop = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].life = 0;    /* 如果定时器达到的话，说明此表项已过期，那么时隙维护线程也应该终止! */

			/* 节点个数减少 */
			neighbor_map[net_i]->BS[bs_i][0].BS_ID--;
			if(neighbor_map[net_i]->BS[bs_i][0].BS_ID == 0)
			{
				neighbor_map[net_i]->BS_num--;
				EPT(stderr, "hm_neighbor_map_thread: net_i = %d BS_num = %d\n", net_i, neighbor_map[net_i]->BS_num);

				/* 定时器到达后若时隙表中节点数为空，则删除相应表项及参数 1.12 */
				if(neighbor_map[net_i]->BS_num == 0)
				{
					free(neighbor_map[net_i]);
					neighbor_map[net_i] = NULL;
					free(neighbor_map_manage[net_i]);
					neighbor_map_manage[net_i] = NULL;

					netID[net_i] = 0;
					netnum--;
						
					EPT(stderr, "hm_neighbor_map_thread: net_i = %d is gone\n", net_i);
					
					/* 解锁 */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}
			
			/* 清空操作 */
			sem_destroy(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));
			neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] = 0;             /* 定时器结束的标记清零 */
			neighbor_map_manage[net_i]->bs_net_BS_node[bs_i][node_i] = 0;          /* 网络号时隙号清零 */
			
#if 0
			EPT(stderr, "hm_neighbor_map_thread: BS[%d] time up2\n", bs_i);
#endif

			/* 解锁 */
			pthread_rwlock_unlock(&rw_lock);
			break;
		}

		else
		{
			if(neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] == 0)
			{
				res = pthread_cancel(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]);    /* 数据先于定时器到达，关闭定时器 */
				if(res != 0)
					EPT(stderr, "hm_neighbor_map_thread: pthread_cancel error code %d\n", res);

				res = pthread_join(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i], &thread_result);
				if(res != 0)
					EPT(stderr, "hm_neighbor_map_thread: pthread_join error code %d\n", res);
			}
				
			res = pthread_create(&(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]), NULL, hm_bs_life_thread, i_p);   /* 修改完毕开启计时 */
			if(res != 0)
				EPT(stderr, "hm_neighbor_map_thread: pthread_create error code %d\n", res);
		}

		/* 解锁 */
		pthread_rwlock_unlock(&rw_lock);
	}
	
}

void *hm_bs_life_thread(void *i_p)
{
	U8 net_i = (*(U32 *)i_p) >> 16;
	U8 bs_i = (*(U32 *)i_p) >> 8;
	U8 node_i = *(U32 *)i_p;

	int res;
	res = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	if(res != 0)
	{
		perror("Thread pthread_setcancelstate failed");
		exit(1);
	}
	res = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	if(res != 0)
	{
		perror("Thread pthread_setcanceltype failed");
		exit(1);
	}
	
#ifdef _HM_TEST
		EPT(stderr, "hm_bs_life_thread: net_i = %d BS[%d][%d] time begin\n", net_i, bs_i, node_i);
#endif

	struct timeval tv;
	tv.tv_sec = 0;      /* 有待调整 */ 
	tv.tv_usec = 580000;  /* 退网的时间严格按照两个超帧的时间来计算，确保满足某些条件时某些节点已经退网 11.15 */
    select(0,NULL,NULL,NULL,&tv);

	//usleep(600000);

#ifdef _HM_TEST
		EPT(stderr, "hm_bs_life_thread: net_i = %d BS[%d][%d] time up\n", net_i, bs_i, node_i);
#endif

	/* 写锁 */
	pthread_rwlock_wrlock(&rw_lock);

	neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] = 1;      /* 定时器结束要标记 */

	/* 解锁 */
	pthread_rwlock_unlock(&rw_lock);

	sem_post(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));
}


/* 发给LowMAC的时隙表 */
void hm_LowMAC_slot_proc(U8 netnum)
{
	U8 i,max;

	memset(&LM_neighbor_map, 0, sizeof(LM_neighbor_map));
	LM_neighbor_map.localBS = neighbor_map[netnum]->l_BS;

	LM_neighbor_map.slotnum = 8; 
	LM_neighbor_map.slotlen = 40000;

	/*
	EPT(stderr, "hm_LowMAC_slot_proc: neighbor_map[%d]->l_BS = %d\n", netnum, neighbor_map[netnum]->l_BS);
	EPT(stderr, "hm_LowMAC_slot_proc: neighbor_map[%d]->BS[0].BS_ID = %d\n", netnum, neighbor_map[netnum]->BS[0].BS_ID);
	EPT(stderr, "hm_LowMAC_slot_proc: neighbor_map[%d]->BS[1].BS_ID = %d\n", netnum, neighbor_map[netnum]->BS[1].BS_ID);
	EPT(stderr, "hm_LowMAC_slot_proc: neighbor_map[%d]->BS[2].BS_ID = %d\n", netnum, neighbor_map[netnum]->BS[2].BS_ID);
	*/
	
	/* 本节点业务时隙占用算法 */
    for(i=0; i<MAX_CFS_PSF; i++)
    {
    	if(neighbor_map[netnum]->BS[i][0].BS_ID)
			max = i+1;    /* 记录最大时隙节点个数 */
	}
	if(max <= 8)
	{
		for(i=0; i<MAX_CFS_PSF; i++)
		{
			LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS);
		}
	}
	if(max > 8 && max <= 16)
	{
		if(neighbor_map[netnum]->l_BS< max-8)
		{
			for(i=0; i<MAX_CFS_PSF; i++)
			{
				if(i%2 == 0)
					LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS);
			}
		}

		else
		{
			if(neighbor_map[netnum]->l_BS > 7)
			{
				for(i=0; i<MAX_CFS_PSF; i++)
				{
					if(i%2 == 1)
						LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS-8);
				}
			}
			else
			{
				for(i=0; i<MAX_CFS_PSF; i++)
				{
					LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS);
				}
			}
		}

	}
	if(max > 16 && max <= 32)
	{
		if(neighbor_map[netnum]->l_BS < max-16)
		{
			if(neighbor_map[netnum]->l_BS < 8)
			{
				for(i=0; i<MAX_CFS_PSF; i++)
				{
					if(i%4 == 0)
						LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS);
				}
			}
			else
			{
				for(i=0; i<MAX_CFS_PSF; i++)
				{
					if(i%4 == 1)
						LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS-8);
				}
			}
		}

		else
		{
			if(neighbor_map[netnum]->l_BS > 15)
			{
				if(neighbor_map[netnum]->l_BS < 24)
				{
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(i%4 == 2)
							LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS-16);
					}
				}
				else
				{
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(i%4 == 3)
							LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS-24);
					}
				}
			}
			else
			{
				if(neighbor_map[netnum]->l_BS < 8)
				{
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(i%2 == 0)
							LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS);
					}
				}
				else
				{
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(i%2 == 1)
							LM_neighbor_map.fixed_slot[i] =  0b10000000 >> (neighbor_map[netnum]->l_BS-8);
					}
				}
			}
		}
	}
}

void *hm_sf_ls_send_thread(void *arg)
{
	U8 net_i;
	U8 i,j;
	U8 node_num;  /* 记录占用某一时隙的节点总数 */
	U8 bs_num = 0;  /* 记录被占用的时隙数 */
	U8 max;

	pthread_detach(pthread_self());
	while(1)
	{
		sem_wait(&(empty[2]));
		net_i = *(U8 *)arg;
		//EPT(stderr, "hm_sf_ls_send_thread: net_i = %d\n", net_i);

		/* 读锁 */
		pthread_rwlock_rdlock(&rw_lock);

		/*********** 生成勤务帧/二维修改 11.06 ***********/
		memset(&service_frame, 0, sizeof(service_frame));
		service_frame.netID = neighbor_map[net_i]->netID;
		service_frame.referenceID = neighbor_map[net_i]->referenceID;
		service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
		service_frame.r_BS = neighbor_map[net_i]->r_BS;
		service_frame.localID = neighbor_map[net_i]->localID;
		service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
		service_frame.l_BS = neighbor_map[net_i]->l_BS;

		/* 可以再次优化，利用BS_num 11.06/再次优化 11.07 */
		for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31时隙轮询/+1确保bs_num会清零 1.9 */
		{
			if(bs_num == neighbor_map[net_i]->BS_num)
			{
				//EPT(stderr, "hm_sf_ls_send_thread: bs_num = %d\n", bs_num);
				bs_num = 0;  /* 保证后续使用的正确性 */
				break;
			}
			if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
			{
				bs_num++;
				node_num = 0;
				for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
				{					
					if(neighbor_map[net_i]->BS[i][j].BS_flag)
					{
						node_num++;
						if(neighbor_map[net_i]->BS[i][j].hop != 2)
						{
							service_frame.BS[i].BS_ID = neighbor_map[net_i]->BS[i][j].BS_ID;
							service_frame.BS[i].flag = 1;
							service_frame.BS[i].clock_lv = neighbor_map[net_i]->BS[i][j].clock_lv;
							service_frame.BS[i].state = neighbor_map[net_i]->BS[i][j].state;
							max = i;
							break;
						}
					}
					if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
						break;
				}
			}
		}
		service_frame.num = max+1;
		//EPT(stderr, "hm_sf_ls_send_thread: service_frame.num = %d\n", service_frame.num);
		
		/* 填充MAC帧 */
		memset(&mac_packet, 0, sizeof(mac_packet));
		mac_packet.pr = 0;
		mac_packet.type = 1;
		mac_packet.subt = 0;
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
		memcpy(mac_packet.data, &service_frame, sizeof(service_frame));

		/*********** 生成LowMAC时隙表 ***********/
		hm_LowMAC_slot_proc(net_i);

		/* 解锁 */
		pthread_rwlock_unlock(&rw_lock);

		/*********** 发送LowMAC时隙表 勤务帧 ***********/
		pthread_mutex_lock(&mutex_queue[4]);
		hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* 送到 TQ_4 队列 */
		sem_post(&(empty[0]));
		pthread_mutex_unlock(&mutex_queue[4]);

		pthread_mutex_lock(&mutex_queue[5]);
		hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* 送到 TQ_5 队列 */
		sem_post(&(empty[0]));
		pthread_mutex_unlock(&mutex_queue[5]);

	}
}

#ifdef _HM_TEST
void *hm_pf_slot_thread(void *arg)
{
	U8 i,j,node_num = 0,bs_num = 0;
	while(1)
	{
		sem_wait(&pf_slot_sem);
		U8 netnum = *(U8 *)arg;

		/* 读锁 */
		pthread_rwlock_rdlock(&rw_lock);	

		EPT(fp, "neighbor_map[%d]->netID = %d  BS_flag = %d  referenceID = %d  rfclock_lv = %d r_BS = %d  localID = %d  lcclock_lv = %d  l_BS = %d  BS_num = %d\n", 
			netnum, neighbor_map[netnum]->netID, neighbor_map[netnum]->BS_flag, 
			neighbor_map[netnum]->referenceID, neighbor_map[netnum]->rfclock_lv, neighbor_map[netnum]->r_BS,
			neighbor_map[netnum]->localID, neighbor_map[netnum]->lcclock_lv, neighbor_map[netnum]->l_BS,
			neighbor_map[netnum]->BS_num);
		
		for(i=0; i<MAX_CFS_PSF; i++)  /* 0~31时隙轮询 */
		{
			node_num = 0;
			for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
			{
				if(node_num == neighbor_map[netnum]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
					break;
				if(neighbor_map[netnum]->BS[i][j].BS_flag)
				{
					node_num++;
					EPT(stderr, "neighbor_map[%d]->BS[%d][%d].BS_ID = %d  state = %d  clock_lv = %d  life = %d  hop = %d\n", netnum, i, j,
						neighbor_map[netnum]->BS[i][j].BS_ID, neighbor_map[netnum]->BS[i][j].state, neighbor_map[netnum]->BS[i][j].clock_lv,
						neighbor_map[netnum]->BS[i][j].life, neighbor_map[netnum]->BS[i][j].hop);
				}
			}
		}

#if 0
		for(i=0; i<MAX_CFS_PSF; i++)  /* 0~31时隙轮询 */
		{
			if(bs_num == neighbor_map[netnum]->BS_num)
			{
				bs_num = 0;  /* 保证后续使用的正确性 */
				break;
			}
			if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
			{
				bs_num++;
				node_num = 0;
				for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32节点轮询 */
				{					
					if(neighbor_map[netnum]->BS[i][j].BS_flag)
					{
						node_num++;
						EPT(stderr, "neighbor_map[%d]->BS[%d][%d].BS_ID = %d  state = %d  clock_lv = %d  life = %d  hop = %d\n", netnum, i, j,
							neighbor_map[netnum]->BS[i][j].BS_ID, neighbor_map[netnum]->BS[i][j].state, neighbor_map[netnum]->BS[i][j].clock_lv,
							neighbor_map[netnum]->BS[i][j].life, neighbor_map[netnum]->BS[i][j].hop);						
					}
					if(node_num == neighbor_map[netnum]->BS[i][0].BS_ID)  /* 查看是否等于占用此时隙的节点总数 */
						break;
				}
			}
		}
#endif
		
		/* 解锁 */
		pthread_rwlock_unlock(&rw_lock);
	}
}
#endif
