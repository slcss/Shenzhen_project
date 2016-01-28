#include "../mr_common.h"
#include "hm_slot.h"
#include "hm_with_lowmac.h"    /* for lm_packet_t */
#include "hm_timer.h"          /* for timer */
#include "hm_queue_manage.h"
#include "hm_common.h"

extern FILE* fp;

/* ���ؽڵ�ID */
extern U8 localID;

/* ��Ϣ���й�����Ϣ */
extern qinfo_t qinfs[];
extern int re_qin;
extern hm_tshare_t  share;


/* slot �� lm2nl �����߳̽������ݵ��������� */
lm_packet_t slot_cache1;
lm_packet_t slot_cache2;

/* ���������Ƿ���Զ�ȡ�ı�־ 0��ʾ���ɶ���д 1��ʾ�ɶ�����д */
int slot_cache1_flag = 0;
int slot_cache2_flag = 0;

/* ��ǰ״̬����һ��״̬ */
U8 cur_state = INIT;
U8 nxt_state = INIT;

/* 6��������� */
extern link_queue_t link_queue[];

/* �����ڽڵ�ʱ϶�� ָ������ ������ÿһ��Ԫ�ض��� neighbor_map_t ���͵�ָ�� */
neighbor_map_t *neighbor_map[32];  /* ��ʱ�趨���������Ϊ32�� 11.05 */
U8  netID[32];      /* ��¼�ִ�����Ŷ�Ӧ�������㼶�ڵ㣬��ʱ�趨���������Ϊ32�� 11.05 */
U8  netnum = 0;     /* ��¼�ִ�������� */

/* ʱ϶������ṹ��ָ������ */
neighbor_map_manage_t *neighbor_map_manage[32];   /* ÿһ�������ʱ϶���Ӧһ��ʱ϶��������ʱ�趨���������Ϊ32�� 11.05 */

/* setitimer �ṹ�� */
extern struct itimerval new_value;

/* �ź��� */
extern sem_t  empty[];

/* ������ */
extern pthread_mutex_t mutex_queue[];        /* 6�����еĻ����� */

/* ��д�� */
pthread_rwlock_t rw_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ��ʱ�������־ */
extern U8 timer_flag;

/* �����͵�����֡ */
service_frame_t service_frame;

/* �����͵�MAC֡ */
mac_packet_t mac_packet;

/* �����͵�״̬�ı�֡ */
H2L_MAC_frame_t H2L_MAC_frame;

/* �·���ʱ϶�� */
LM_neighbor_map_t LM_neighbor_map;

#ifdef _HM_TEST
/* ��ӡʱ϶���߳� */
sem_t pf_slot_sem;

extern FILE *fp;
#endif



void *hm_slot_thread(void *arg)
{
	U8  sf_count = 0;          /* ��¼����֡���� */
	U8  i,j,bs;
	U8  net_num = 0;           /* ��¼��������(�յ�ͬһ��NET�ڵ���������֡)��������� */
	U8  net_i = 0;             /* ��¼����ѡ����һ������ */
	U8  bs_i = 0;              /* ��¼ѡ���Ļ�׼�ڵ��ʱ϶ */
	U8  node_i = 0;            /* ��¼ѡ���Ľڵ�� */
	U8  net_count = 0;		   /* �������ʱ϶��ʱ϶�����ʱ������ļ��� 1.13 */

	/* �ݴ��� */
	U8  num = 0;
	U8  level = 32;
	U8  max =0;

	int rval;
	pthread_t sf_ls_send;

	void *thread_result;  /* for pthread_join 10.30 */

	U8 node_num;  /* ��¼ռ��ĳһʱ϶�Ľڵ����� 11.07 */
	U8 bs_num = 0;  /* ��¼��ռ�õ�ʱ϶�� 11.07 */

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
				/*********** ״̬�ı� ***********/
            	nxt_state = SCN;				

				/* ״̬�ı䣬�·�״̬�ı�֡ */
				memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
				H2L_MAC_frame.referenceID = 0xff;
				H2L_MAC_frame.rfclock_lv = 0xff;
				H2L_MAC_frame.localID = localID;
				H2L_MAC_frame.lcclock_lv = 0xff;
				H2L_MAC_frame.state = 0;      /* �ڵ�״̬��Ϊ SCN */
				H2L_MAC_frame.res = 0;
				H2L_MAC_frame.slotnum = 8;
				H2L_MAC_frame.slotlen = 40000;

				pthread_mutex_lock(&mutex_queue[4]);
				hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);    /* �͵� TQ_4 ���� */
				sem_post(&(empty[0]));
				pthread_mutex_unlock(&mutex_queue[4]);	
				
				//EPT(stderr, "hm_slot_thread: state = 0,send 0xff\n");
            	break;

        	case SCN:
				EPT(stderr, "hm_slot_thread: state = 1******************\n");
				hm_start_timer1();

				while(1)
				{
					sem_wait(&(empty[1]));  /* �ȴ�������������+1�����߶�ʱ������+1���������ͬʱ+1����ʱ�������˳���������Ȼû�ж�ȡ���ź�������1���´ο��Լ�����ȡ */

					if(timer_flag == 1)    /* ����Ƕ�ʱ�������Ļ�ֱ���˳� */
					{
						EPT(stderr, "hm_slot_thread: timer1 up\n");
						timer_flag = 0;
						break;
					}

					sf_count = 1;          /* ��ʾ����������֡ */

#if 0
					EPT(stderr, "hm_slot_thread: slot_cache1_flag = %d\n", slot_cache1_flag);
					EPT(stderr, "hm_slot_thread: slot_cache2_flag = %d\n", slot_cache2_flag);
#endif

					/* �������������ж�ȡ���� */
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

					/* �鿴�Ƿ��������������������رն�ʱ�����˳�ѭ�� */
					for(i=0; i<netnum; i++)
					{
						if(neighbor_map_manage[i]->sf_flag == 1)
						{
							net_num++;    /* ��¼��������(�յ�ͬһ��NET�ڵ���������֡)��������� */
						}
					}

					//EPT(stderr, "hm_slot_thread: j = %d\n", j);
					//EPT(stderr, "hm_slot_thread: netnum = %d\n", netnum);
					
					if(net_num == netnum)
					{
                         /* ȫ��0 */
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* ֹͣ��ʱ�� */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
					/*10.9 ��0����ֹ�ظ�����*/
					else
						net_num = 0;
				}

				if(sf_count == 0)          /* ����CON1_1   Ĭ��net_numΪ0 */
				{
					EPT(stderr, "1_1\n");
					/*********** �����ڵ㽨���Լ����ڽڵ�ά���� ***********/
					/* ѡ��BS = 0 �����ڽڵ��ά���� */
					netID[netnum] = localID;

					neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* ���ڽڵ�ʱ϶��ָ������ڴ� */
					memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */

					neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* ��ʱ϶�����ṹ�����ָ�� */
					memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* ��ʼ��ʱ϶�����ṹ�� */

					neighbor_map[netnum]->netID = localID;
					neighbor_map[netnum]->BS_flag = 1;
					neighbor_map[netnum]->referenceID = 0xff;      /* 0���ڵ�Ļ�׼�ڵ������ϢȫΪ0xff */
					neighbor_map[netnum]->rfclock_lv = 0xff;
					neighbor_map[netnum]->r_BS = 0xff;
					neighbor_map[netnum]->localID = localID;
					neighbor_map[netnum]->lcclock_lv = 0;
					neighbor_map[netnum]->l_BS = 0;					

					neighbor_map[netnum]->BS[0][localID].BS_ID = localID;
					neighbor_map[netnum]->BS[0][localID].BS_flag = 1;
					neighbor_map[netnum]->BS[0][localID].clock_lv = 0;
					neighbor_map[netnum]->BS[0][localID].state = NET;       /* �ı�ʱ϶���еĽڵ�״̬ */
					neighbor_map[netnum]->BS[0][localID].hop = 0;
					neighbor_map[netnum]->BS[0][localID].life = 0;
					
					neighbor_map[netnum]->BS[0][0].BS_ID++;
					neighbor_map[netnum]->BS_num++;

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[netnum]->netID;
					service_frame.referenceID = neighbor_map[netnum]->referenceID;
					service_frame.rfclock_lv = neighbor_map[netnum]->rfclock_lv;
					service_frame.r_BS = neighbor_map[netnum]->r_BS;
					service_frame.localID = neighbor_map[netnum]->localID;
					service_frame.lcclock_lv = neighbor_map[netnum]->lcclock_lv;
					service_frame.l_BS = neighbor_map[netnum]->l_BS;
					
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[netnum]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					service_frame.num = max+1;

					netnum++;   /* netnum����Ӧ��Ϊ1 */

					/* ���MAC֡ */
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

					/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ״̬�ı䣬����״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** ����LowMACʱ϶�� ***********/
					net_i = 0;
					hm_LowMAC_slot_proc(net_i);    /* ����Ϊ����� */

					/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					break;
				}

				if(net_num == netnum)            /* ����CON1_2 �ǵ���0���� */
				{
					EPT(stderr, "1_2\n");
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** ѡ���������� ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(num < neighbor_map[i]->BS_num)
						{
							num = neighbor_map[i]->BS_num;
							net_i = i;     /* �����ȷ�� */
						}
					}

					/*********** ѡ���ϼ��ڵ�/��ά�޸� 11.07 ***********/
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						//EPT(stderr, "bs_num1 = %d\n", bs_num);
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							//EPT(stderr, "bs_num2 = %d\n", bs_num);
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* �ϼ��ڵ�ʱ϶��ȷ�� */
											node_i = j;  /* �ϼ��ڵ�ڵ��ȷ�� */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}//EPT(stderr, "bs_num3 = %d\n", bs_num);
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** ѡ��BS�������ڽڵ��ά���� ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* ��ʱ��i��Ϊ���ڵ�ѡ����ʱ϶ */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* �ı�ʱ϶���еĽڵ�״̬ */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
					service_frame.num = max+1;

					/* ���MAC֡ */
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

					/*********** ״̬�ı� ***********/
					nxt_state = WAN;

					/* ״̬�ı䣬����״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** ����LowMACʱ϶�� ***********/
					hm_LowMAC_slot_proc(net_i);

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** ��ղ��� ***********/
					net_num = 0;
					memset(neighbor_map_manage[net_i]->sf_NET_num, 0, sizeof(neighbor_map_manage[net_i]->sf_NET_num));
					memset(&(neighbor_map_manage[net_i]->sf_flag), 0, sizeof(U8));
					sf_count = 0;

					break;
				}

				if(net_num < netnum && net_num != 0)   /* ����CON1_3 */
				{
					EPT(stderr, "1_3\n");
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** ѡ���������� ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(neighbor_map_manage[i]->sf_flag == 1)     /* ֻѡ�������������(�յ�ͬһ��NET�ڵ���������֡)������ */
						{
							if(num < neighbor_map[i]->BS_num)
							{
								num = neighbor_map[i]->BS_num;
								net_i = i;     /* �����ȷ�� */
							}
						}
					}

					/*********** ѡ���ϼ��ڵ� ***********/
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* �ϼ��ڵ�ʱ϶��ȷ�� */
											node_i = j;  /* �ϼ��ڵ�ڵ��ȷ�� */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** ѡ��BS�������ڽڵ��ά���� ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* ��ʱ��i��Ϊ���ڵ�ѡ����ʱ϶ */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* �ı�ʱ϶���еĽڵ�״̬ */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;	
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					service_frame.num = max+1;					

					/* ���MAC֡ */
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

					/*********** ״̬�ı� ***********/
					nxt_state = WAN;

					/* ״̬�ı䣬����״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** ����LowMACʱ϶�� ***********/
					hm_LowMAC_slot_proc(net_i);

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** ��ղ��� ***********/
					net_num = 0;
					memset(neighbor_map_manage[net_i]->sf_NET_num, 0, sizeof(neighbor_map_manage[net_i]->sf_NET_num));
					memset(&(neighbor_map_manage[net_i]->sf_flag), 0, sizeof(U8));
					sf_count = 0;

					break;
				}

            	else           /* ����CON1_4 */
				{
					EPT(stderr, "1_4\n");
					/*********** ��ղ��� ***********/
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* ���ʱ϶������ά���߳� 10.30*/
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
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** ״̬�ı� ***********/
					nxt_state = SCN;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
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
					sem_wait(&(empty[1]));	/* �ȴ�������������+1�����߶�ʱ������+1���������ͬʱ+1����ʱ�������˳���������Ȼû�ж�ȡ���ź�������1���´ο��Լ�����ȡ */

					if(timer_flag == 1)    /* ����Ƕ�ʱ�������Ļ�ֱ���˳� */
					{
						timer_flag = 0;
						break;
					}

					sf_count = 1;		   /* ��ʾ����������֡ */

					/* �������������ж�ȡ���� */
					if(slot_cache1_flag == 1)
					{
						rval = hm_MAC_frame_rcv_proc2(&slot_cache1, net_i);
						if(rval == 0x08)
						{
							//EPT(stderr, "case WAN sem_post\n");
							/* ��������֡��LowMACʱ϶�� */
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
							/* ��������֡��LowMACʱ϶�� */
							sem_post(&(empty[2]));
						}

						slot_cache2_flag = 0;
					}

					//EPT(stderr, "hm_slot_thread: sf_rf_get = %d\n", neighbor_map_manage[net_i]->sf_rf_get);
					//EPT(stderr, "hm_slot_thread: sf_lc_get = %d\n", neighbor_map_manage[net_i]->sf_lc_get);

					/* �鿴�Ƿ��������������������رն�ʱ�����˳�ѭ�� */
					if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag == 1)
					{
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* ֹͣ��ʱ�� */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
				}

				if(sf_count == 0)     /* ����CON2_1 */
				{
					EPT(stderr, "2_1\n");
					/*********** ��ղ��� ***********/
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* ���ʱ϶������ά���߳� 10.30*/
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
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** ״̬�ı� ***********/
					nxt_state = SCN;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
				}

				if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag== 0)     /* ����CON2_2 */
				{
					EPT(stderr, "2_2\n");
					/*********** ״̬�ı� ***********/
					nxt_state = WAN;

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** ��ղ��� ***********/
					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 1;
					sf_count = 0;

					break;
				}

				if(neighbor_map_manage[net_i]->sf_rf_get == 1 && neighbor_map_manage[net_i]->sf_lc_get == 1 && neighbor_map_manage[net_i]->sf_lc_flag == 1)      /* ����CON2_3 */
				{
					EPT(stderr, "2_3\n");
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/**** ���ڵ�״̬�ı� ****/
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][localID].state = NET;   /* ���ڵ�״̬�ı�ʱ�������ȸ���ʱ϶��!! */

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;			

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
					service_frame.num = max+1;

					/* ���MAC֡ */
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

					/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/*********** ����״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					/*********** ��ղ��� ***********/
					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 0;
					sf_count = 0;

					break;
				}

				else          /* ����CON2_4 */
				{
					EPT(stderr, "2_4\n");
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/*********** ����ѡ��������� ***********/
					/* ����ձ�����Ĳ��� */
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
					neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* ��������û�нڵ�ͱ��ؽڵ�ռ�õ�ʱ϶��ͻ��ֱ������ 11.08 */
					neighbor_map[net_i]->l_BS = 0;
					neighbor_map[net_i]->BS_num--;

					neighbor_map_manage[net_i]->sf_rf_get = 0;
					neighbor_map_manage[net_i]->sf_lc_get = 0;
					neighbor_map_manage[net_i]->sf_lc_flag = 0;
					sf_count = 0;

					/*********** ѡ���������� ***********/
					num = 0;
					for(i=0; i<netnum; i++)
					{
						if(num < neighbor_map[i]->BS_num)
						{
							num = neighbor_map[i]->BS_num;
							net_i = i;     /* �����ȷ�� */
						}
					}

					/*********** ѡ���ϼ��ڵ� ***********/
					/* ������Կ����ų���һ��ѡ�����ϼ��ڵ㣬��Ϊ��һ�ε��ϼ��ڵ㲢���ȶ� */
					level = 32;				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;
									if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
									{
										if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
										{
											level = neighbor_map[net_i]->BS[i][j].clock_lv;
											bs_i = i;    /* �ϼ��ڵ�ʱ϶��ȷ�� */
											node_i = j;  /* �ϼ��ڵ�ڵ��ȷ�� */
										}
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					
					neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
					neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
					neighbor_map[net_i]->r_BS = bs_i;

					/*********** ѡ��BS�������ڽڵ��ά���� ***********/
					for(i=0; i<MAX_CFS_PSF; i++)
					{
						if(neighbor_map[net_i]->BS[i][0].BS_ID)
							continue;
						break;
					}
					/* ��ʱ��i��Ϊ���ڵ�ѡ����ʱ϶ */
					neighbor_map[net_i]->BS_flag = 1;
					neighbor_map[net_i]->localID = localID;
					neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
					neighbor_map[net_i]->l_BS = i;
					neighbor_map[net_i]->BS_num++;

					neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
					neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
					neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
					neighbor_map[net_i]->BS[i][localID].state = WAN;    /* �ı�ʱ϶���еĽڵ�״̬ */
					neighbor_map[net_i]->BS[i][localID].hop = 0;
					neighbor_map[net_i]->BS[i][localID].life = 0;
					neighbor_map[net_i]->BS[i][0].BS_ID++;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;

					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					service_frame.num = max+1;

					/* ���MAC֡ */
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

					/*********** ״̬�ı� ***********/
					nxt_state = WAN;

					/* ״̬�ı䣬����״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** ����LowMACʱ϶�� ***********/
					hm_LowMAC_slot_proc(net_i);

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
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
					sem_wait(&(empty[1]));	/* �ȴ�������������+1�����߶�ʱ������+1���������ͬʱ+1����ʱ�������˳���������Ȼû�ж�ȡ���ź�������1���´ο��Լ�����ȡ */
					
					if(timer_flag == 1)    /* ����Ƕ�ʱ�������Ļ�ֱ���˳� */
					{
						timer_flag = 0;
						break;
					}

					sf_count = 1;		   /* ��ʾ����������֡ */

					/* �������������ж�ȡ���� */
					if(slot_cache1_flag == 1)
					{	
						//EPT(stderr, "hm_slot_thread: net_i = %d\n", net_i);
						rval = hm_MAC_frame_rcv_proc3(&slot_cache1, net_i);
						if(rval == 0x08)
						{
							EPT(stderr, "hm_slot_thread1: case NET sem_post\n");
							/* ���Ƿ�������֡��LowMACʱ϶�� */
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
							/* ���Ƿ�������֡��LowMACʱ϶�� */
							sem_post(&(empty[2]));
						}

						slot_cache2_flag = 0;
					}

					/* �鿴�Ƿ��������������������رն�ʱ�����˳�ѭ�� */
					if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num > 1)
					{						
						new_value.it_value.tv_sec = 0;
                        new_value.it_value.tv_usec = 0;
                        new_value.it_interval.tv_sec = 0;
                        new_value.it_interval.tv_usec = 0;

						rval = setitimer(ITIMER_REAL, &new_value, NULL);   /* ֹͣ��ʱ�� */
						if (-1 == rval)
						{/* failure */
							EPT(stderr, "hm_slot_thread: error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
							exit(1);
						}

						break;
					}
				}

            	if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && sf_count == 0)    /* ����CON3_1 */
				{
					EPT(stderr, "3_1\n");
					
					/* ���Ƿ�������֡��LowMACʱ϶�� 
					sem_post(&(empty[2]));*/

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/*********** ��������֡ ***********/
					memset(&service_frame, 0, sizeof(service_frame));
					service_frame.netID = neighbor_map[net_i]->netID;
					service_frame.referenceID = neighbor_map[net_i]->referenceID;
					service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					service_frame.r_BS = neighbor_map[net_i]->r_BS;
					service_frame.localID = neighbor_map[net_i]->localID;
					service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					service_frame.l_BS = neighbor_map[net_i]->l_BS;
				
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
						}
					}
					service_frame.num = max+1;

					/* ���MAC֡ */
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
					
					/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/*********** ����LowMACʱ϶�� ***********/
					hm_LowMAC_slot_proc(net_i);

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					pthread_mutex_lock(&mutex_queue[5]);
					hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[5]);

					break;
            	}

				if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 1)    /* ����CON3_2 */
				{
					EPT(stderr, "3_2\n");
					/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** ��ղ��� ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

				if(neighbor_map[net_i]->netID == neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 0 && neighbor_map_manage[net_i]->sf_diffnet_get == 1)    /* ����CON3_3 */
				{
					EPT(stderr, "3_3\n");
                	/*********** ��ղ��� ***********/
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* ���ʱ϶������ά���߳� 10.30*/
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
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** ״̬�ı� ***********/
					nxt_state = SCN;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && sf_count == 0)    /* ����CON3_4 */
				{
					EPT(stderr, "3_4\n");
                	/*********** ��ղ��� ***********/
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* ���ʱ϶������ά���߳� 10.30*/
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
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
					
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** ״̬�ı� ***********/
					nxt_state = SCN;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 0 && neighbor_map_manage[net_i]->sf_diffnet_get == 1)    /* ����CON3_5 */
				{
					EPT(stderr, "3_5\n");
                	/*********** ��ղ��� ***********/
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					
					net_count = 0;
					for(i=0; i<MAX_NET_NUM; i++)
					{
						//U8 bs;
						if(netID[i] != 0)
						{	
							for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
							{
								if(bs_num == neighbor_map[i]->BS_num)
								{
									bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
									break;
								}
								if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
								{
									bs_num++;
									node_num = 0;
									for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
									{
										if(neighbor_map[i]->BS[bs][j].BS_flag)   
										{
											node_num++;
											if(neighbor_map[i]->BS[bs][j].life)
											{
												/* ���ʱ϶������ά���߳� 10.30*/
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
										if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);					

					/*********** ״̬�ı� ***********/
					nxt_state = SCN;

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = 0xff;
					H2L_MAC_frame.rfclock_lv = 0xff;
					H2L_MAC_frame.localID = localID;
					H2L_MAC_frame.lcclock_lv = 0Xff;
					H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num > 1)    /* ����CON3_6 */
				{
					EPT(stderr, "3_6\n");
                	/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** ��ղ��� ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_rf_num == 1)    /* ����CON3_6_1 */
				{
					EPT(stderr, "3_6_1\n");
                	/*********** ״̬�ı� ***********/
					nxt_state = NET;

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/* ״̬�ı䣬�·�״̬�ı�֡ */
					memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
					H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
					H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
					H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
					H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
					H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
					H2L_MAC_frame.res = 0;
					H2L_MAC_frame.slotnum = 8;
					H2L_MAC_frame.slotlen = 40000;

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					pthread_mutex_lock(&mutex_queue[4]);
					hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[4]);

					/*********** ��ղ��� ***********/
					neighbor_map_manage[net_i]->sf_samenet_get = 0;
					neighbor_map_manage[net_i]->sf_diffnet_get = 0;
					neighbor_map_manage[net_i]->sf_rf_num = 0;
					sf_count = 0;

					break;
            	}

#if 1				
				/* ����CON3_7 CON3_8 CON3_9 CON3_10 */
				if(neighbor_map[net_i]->netID != neighbor_map[net_i]->localID && neighbor_map_manage[net_i]->sf_samenet_get == 1 && neighbor_map_manage[net_i]->sf_rf_num == 0)    
				{	
					/* ÿ�ν����״̬�����㣬����֮��ʹ�� 12.10 */
					int flag3_7 = 0;  /* ��¼�Ƿ���������3_7 11.15 */
					int flag3_8 = 0;  /* ��¼�Ƿ���������3_8 11.15 */
					int flag3_9 = 0;  /* ��¼�Ƿ���������3_9 11.15 */

					/* ���� */
					pthread_rwlock_rdlock(&rw_lock);

					/* �жϾ��������ĸ����� */	
					for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
					{
						if(bs_num == neighbor_map[net_i]->BS_num)
						{
							bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
							break;
						}
						if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
						{
							bs_num++;
							node_num = 0;
							for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
							{					
								if(neighbor_map[net_i]->BS[i][j].BS_flag)
								{
									node_num++;

									/* ���µ��жϾ��ų��ϼ��ڵ㣬��Ϊ�ղ����ϼ��ڵ㷢�͵�����֡�Ż��������״̬����ʱ��ʱ϶���п��ܻ���д����ϼ��ڵ����Ϣ(����) */
									if(neighbor_map[net_i]->BS[i][j].clock_lv <= neighbor_map[net_i]->rfclock_lv && neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET && neighbor_map[net_i]->BS[i][j].BS_ID != neighbor_map[net_i]->referenceID)
									{
										flag3_7 = 1;  /* ʱ϶�����д��ڵ����ϼ��ڵ�ʱ�Ӽ����һ��NET�ڽڵ㣬ʱ�Ӽ���Խ����ֵԽС 11.15 */
										break;
									}
									
									if(neighbor_map[net_i]->BS[i][j].clock_lv < neighbor_map[net_i]->lcclock_lv && neighbor_map[net_i]->BS[i][j].state == NET && neighbor_map[net_i]->BS[i][j].BS_ID != neighbor_map[net_i]->referenceID)
									{	
										flag3_8 = 1;  /* ������Χ���д��ڱ��ڵ�ʱ�Ӽ����NET�ڵ�(������) 11.15 */
										EPT(stderr, "net_i = %d i = %d j = %d\n", net_i, i, j);
									}
									else 
									{	
										if(neighbor_map[net_i]->BS[i][j].clock_lv == neighbor_map[net_i]->lcclock_lv && neighbor_map[net_i]->BS[i][j].BS_ID < neighbor_map[net_i]->localID && neighbor_map[net_i]->BS[i][j].state == NET)
											flag3_9 = 1;  /* ������Χ���в����ڱ���ʱ�Ӽ���Ľڵ㣬�ұ��ؽڵ㲻�ǽڵ����С��<ͬ��>NET�ڵ� 11.15 */
									}
								}
								if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
									break;
							}
							/* ����3_7״ֱ̬�ӾͿ����˳���������3_7��״̬�£�3_8��3_9����ͬʱ���ڣ��ж�����ʲô������ʱ�����ж�3_8 12.10 */
							if(flag3_7 == 1)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
						}
					}//EPT(stderr, "bs_num = %d\n", bs_num);
														
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ʱ϶�����д��ڵ����ϼ��ڵ�ʱ�Ӽ����һ��NET�ڽڵ� */
					if(flag3_7 == 1)        /* ����CON3_7 */
					{
						EPT(stderr, "3_7\n");
						
						/* д�� */
						pthread_rwlock_wrlock(&rw_lock);

						/*********** ����ձ�����Ĳ��� ***********/
						/* ������ѡ���ϼ��ڵ㡢BS֮ǰ��Ҫ��֮ǰѡ��ռ�õ���� 11.01 */								
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
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* ��������û�нڵ�ͱ��ؽڵ�ռ�õ�ʱ϶��ͻ��ֱ������ 11.08 */
						neighbor_map[net_i]->l_BS = 0;
						neighbor_map[net_i]->BS_num--;  //EPT(stderr, "BS_num = %d\n", neighbor_map[net_i]->BS_num);

						neighbor_map_manage[net_i]->sf_samenet_get = 0;
						neighbor_map_manage[net_i]->sf_diffnet_get = 0;
						neighbor_map_manage[net_i]->sf_rf_num = 0;
						sf_count = 0;

						/*********** ����ѡ���ϼ��ڵ� ***********/						
						/* ������Կ����ų���һ��ѡ�����ϼ��ڵ㣬��Ϊ��һ�ε��ϼ��ڵ㲢���ȶ� */
						level = 32;				
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
						{
							//EPT(stderr, "bs_num = %d\n", bs_num);
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;  //EPT(stderr, "bs_num = %d\n", bs_num);
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
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
												bs_i = i;    /* �ϼ��ڵ�ʱ϶��ȷ�� */   
												node_i = j;  /* �ϼ��ڵ�ڵ��ȷ�� */	
												//EPT(stderr, "i = %d j = %d\n", i, j);
											}
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
										break;
								}
							}
						}
						
						neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
						neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
						neighbor_map[net_i]->r_BS = bs_i;

						EPT(stderr, "referenceID = %d rfclock_lv = %d r_BS = %d\n", neighbor_map[net_i]->referenceID, neighbor_map[net_i]->rfclock_lv, neighbor_map[net_i]->r_BS);

						/*********** ѡ��BS�������ڽڵ��ά���� ***********/
						for(i=0; i<MAX_CFS_PSF; i++)
						{
							if(neighbor_map[net_i]->BS[i][0].BS_ID)
								continue;
							break;
						}
						/* ��ʱ��i��Ϊ���ڵ�ѡ����ʱ϶ */
						neighbor_map[net_i]->BS_flag = 1;
						neighbor_map[net_i]->localID = localID;
						neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
						neighbor_map[net_i]->l_BS = i;
						neighbor_map[net_i]->BS_num++;

						neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
						neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
						neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
						neighbor_map[net_i]->BS[i][localID].state = WAN;    /* �ı�ʱ϶���еĽڵ�״̬ */
						neighbor_map[net_i]->BS[i][localID].hop = 0;
						neighbor_map[net_i]->BS[i][localID].life = 0;
						neighbor_map[net_i]->BS[i][0].BS_ID++;						

						/* ���� */
						pthread_rwlock_unlock(&rw_lock);

						/* ���� */
						pthread_rwlock_rdlock(&rw_lock);						

						/*********** ��������֡ ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[net_i]->netID;
						service_frame.referenceID = neighbor_map[net_i]->referenceID;
						service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						service_frame.r_BS = neighbor_map[net_i]->r_BS;
						service_frame.localID = neighbor_map[net_i]->localID;
						service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						service_frame.l_BS = neighbor_map[net_i]->l_BS;

						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
										break;
								}
							}
						}
						service_frame.num = max+1;

						/* ���MAC֡ */
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

						/*********** ״̬�ı� ***********/
						nxt_state = WAN;

						/* ״̬�ı䣬����״̬�ı�֡ */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
						H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
						H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** ����LowMACʱ϶�� ***********/
						hm_LowMAC_slot_proc(net_i);

						/* ���� */
						pthread_rwlock_unlock(&rw_lock);

						/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);		

						break;
					}

					/* ������Χ���д��ڱ��ڵ�ʱ�Ӽ����NET�ڵ� 11.15 */
					if(flag3_8 == 1)  /* ����CON3_8 */
					{
						EPT(stderr, "3_8\n");

						/* д�� */
						pthread_rwlock_wrlock(&rw_lock);

						/*********** ����ձ�����Ĳ��� ***********/
						/* ������ѡ���ϼ��ڵ㡢BS֮ǰ��Ҫ��֮ǰѡ��ռ�õ���� 11.01 */								
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
						neighbor_map[net_i]->BS[neighbor_map[net_i]->l_BS][0].BS_ID = 0;  /* ��������û�нڵ�ͱ��ؽڵ�ռ�õ�ʱ϶��ͻ��ֱ������ 11.08 */
						neighbor_map[net_i]->l_BS = 0;
						neighbor_map[net_i]->BS_num--;

						neighbor_map_manage[net_i]->sf_samenet_get = 0;
						neighbor_map_manage[net_i]->sf_diffnet_get = 0;
						neighbor_map_manage[net_i]->sf_rf_num = 0;
						sf_count = 0;

						/*********** ����ѡ���ϼ��ڵ� ***********/						
						/* ������Կ����ų���һ��ѡ�����ϼ��ڵ㣬��Ϊ��һ�ε��ϼ��ڵ㲢���ȶ� */
						level = 32;				
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
								{					
									if(neighbor_map[net_i]->BS[i][j].BS_flag)
									{
										node_num++;
										if(neighbor_map[net_i]->BS[i][j].hop == 1 && neighbor_map[net_i]->BS[i][j].state == NET)
										{
											if(level > neighbor_map[net_i]->BS[i][j].clock_lv)
											{
												level = neighbor_map[net_i]->BS[i][j].clock_lv;
												bs_i = i;    /* �ϼ��ڵ�ʱ϶��ȷ�� */
												node_i = j;  /* �ϼ��ڵ�ڵ��ȷ�� */
											}
										}
									}
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
										break;
								}
							}
						}
						
						neighbor_map[net_i]->referenceID = neighbor_map[net_i]->BS[bs_i][node_i].BS_ID;
						neighbor_map[net_i]->rfclock_lv = neighbor_map[net_i]->BS[bs_i][node_i].clock_lv;
						neighbor_map[net_i]->r_BS = bs_i;	
						
						EPT(stderr, "referenceID = %d rfclock_lv = %d r_BS = %d\n", neighbor_map[net_i]->referenceID, neighbor_map[net_i]->rfclock_lv, neighbor_map[net_i]->r_BS);

						/*********** ѡ��BS�������ڽڵ��ά���� ***********/
						for(i=0; i<MAX_CFS_PSF; i++)
						{
							if(neighbor_map[net_i]->BS[i][0].BS_ID)
								continue;
							break;
						}
						/* ��ʱ��i��Ϊ���ڵ�ѡ����ʱ϶ */
						neighbor_map[net_i]->BS_flag = 1;
						neighbor_map[net_i]->localID = localID;
						neighbor_map[net_i]->lcclock_lv = neighbor_map[net_i]->rfclock_lv + 1;
						neighbor_map[net_i]->l_BS = i;
						neighbor_map[net_i]->BS_num++;

						neighbor_map[net_i]->BS[i][localID].BS_ID = localID;
						neighbor_map[net_i]->BS[i][localID].BS_flag = 1;
						neighbor_map[net_i]->BS[i][localID].clock_lv = neighbor_map[net_i]->lcclock_lv;
						neighbor_map[net_i]->BS[i][localID].state = WAN;    /* �ı�ʱ϶���еĽڵ�״̬ */
						neighbor_map[net_i]->BS[i][localID].hop = 0;
						neighbor_map[net_i]->BS[i][localID].life = 0;
						neighbor_map[net_i]->BS[i][0].BS_ID++;						

						/* ���� */
						pthread_rwlock_unlock(&rw_lock);

						/* ���� */
						pthread_rwlock_rdlock(&rw_lock);						

						/*********** ��������֡ ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[net_i]->netID;
						service_frame.referenceID = neighbor_map[net_i]->referenceID;
						service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						service_frame.r_BS = neighbor_map[net_i]->r_BS;
						service_frame.localID = neighbor_map[net_i]->localID;
						service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						service_frame.l_BS = neighbor_map[net_i]->l_BS;

						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
						{
							if(bs_num == neighbor_map[net_i]->BS_num)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
							if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
										break;
								}
							}
						}
						service_frame.num = max+1;

						/* ���MAC֡ */
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

						/*********** ״̬�ı� ***********/
						nxt_state = WAN;

						/* ״̬�ı䣬����״̬�ı�֡ */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = neighbor_map[net_i]->referenceID;
						H2L_MAC_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
						H2L_MAC_frame.localID = neighbor_map[net_i]->localID;
						H2L_MAC_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
						H2L_MAC_frame.state = 1;          /* �ڵ�״̬��Ϊ WAN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** ����LowMACʱ϶�� ***********/
						hm_LowMAC_slot_proc(net_i);

						/* ���� */
						pthread_rwlock_unlock(&rw_lock);

						/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);		

						break;
					}
					
					/* ������Χ��<ֻ��>�����ڱ���ʱ�Ӽ���Ľڵ㣬�ұ��ؽڵ㲻�ǽڵ����С��ͬ��NET�ڵ� 11.15 */				
					if(flag3_9 == 1)     /* ����CON3_9 */
					{
						EPT(stderr, "3_9\n");						

						/*********** ��ղ��� ***********/
						/* д�� */
						pthread_rwlock_wrlock(&rw_lock);
						
						net_count = 0;
						for(i=0; i<MAX_NET_NUM; i++)
						{
							//U8 bs;
							if(netID[i] != 0)
							{	
								for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
								{
									if(bs_num == neighbor_map[i]->BS_num)
									{
										bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
										break;
									}
									if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
									{
										bs_num++;
										node_num = 0;
										for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
										{
											if(neighbor_map[i]->BS[bs][j].BS_flag)   
											{
												node_num++;
												if(neighbor_map[i]->BS[bs][j].life)
												{
													/* ���ʱ϶������ά���߳� 10.30*/
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
											if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
						
						/* ���� */
						pthread_rwlock_unlock(&rw_lock);						

						/*********** ״̬�ı� ***********/
						nxt_state = SCN;

						/* ״̬�ı䣬�·�״̬�ı�֡ */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = 0xff;
						H2L_MAC_frame.rfclock_lv = 0xff;
						H2L_MAC_frame.localID = localID;
						H2L_MAC_frame.lcclock_lv = 0Xff;
						H2L_MAC_frame.state = 0;          /* �ڵ�״̬��Ϊ SCN */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						/* ��ʱ���� 1.10 
						usleep(300000);*/
						sleep(1*localID);

						break;
					}
				
					/* ������Χ��ֻ�в����ڱ���ʱ�Ӽ���Ľڵ㣬�ұ��ؽڵ��ǽڵ����С��ͬ��NET�ڵ� 11.15 */	
					else    /* ����CON3_10 */
					{
						EPT(stderr, "3_10\n");
						
						/*********** ��ղ��� ***********/
						/* д�� */
						pthread_rwlock_wrlock(&rw_lock);
						
						net_count = 0;
						for(i=0; i<MAX_NET_NUM; i++)
						{
							//U8 bs;
							if(netID[i] != 0)
							{	
								for(bs=0; bs<MAX_CFS_PSF + 1; bs++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
								{
									if(bs_num == neighbor_map[i]->BS_num)
									{
										bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
										break;
									}
									if(neighbor_map[i]->BS[bs][0].BS_ID != 0)
									{
										bs_num++;
										node_num = 0;
										for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
										{
											if(neighbor_map[i]->BS[bs][j].BS_flag)   
											{
												node_num++;
												if(neighbor_map[i]->BS[bs][j].life)
												{
													/* ���ʱ϶������ά���߳� 10.30*/
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
											if(node_num == neighbor_map[i]->BS[bs][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
						
						/* ���� */
						pthread_rwlock_unlock(&rw_lock);

						/*********** �����ڵ㽨���Լ����ڽڵ�ά���� ***********/
						/* ѡ��BS = 0 �����ڽڵ��ά���� */
						netID[netnum] = localID;

						neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* ���ڽڵ�ʱ϶��ָ������ڴ� */
						memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */

						neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* ��ʱ϶�����ṹ�����ָ�� */
						memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* ��ʼ��ʱ϶�����ṹ�� */

						neighbor_map[netnum]->netID = localID;
						neighbor_map[netnum]->BS_flag = 1;
						neighbor_map[netnum]->referenceID = 0xff;      /* 0���ڵ�Ļ�׼�ڵ������ϢȫΪ0xff */
						neighbor_map[netnum]->rfclock_lv = 0xff;
						neighbor_map[netnum]->r_BS = 0xff;
						neighbor_map[netnum]->localID = localID;
						neighbor_map[netnum]->lcclock_lv = 0;
						neighbor_map[netnum]->l_BS = 0;					

						neighbor_map[netnum]->BS[0][localID].BS_ID = localID;
						neighbor_map[netnum]->BS[0][localID].BS_flag = 1;
						neighbor_map[netnum]->BS[0][localID].clock_lv = 0;
						neighbor_map[netnum]->BS[0][localID].state = NET;       /* �ı�ʱ϶���еĽڵ�״̬ */
						neighbor_map[netnum]->BS[0][localID].hop = 0;
						neighbor_map[netnum]->BS[0][localID].life = 0;
						
						neighbor_map[netnum]->BS[0][0].BS_ID++;
						neighbor_map[netnum]->BS_num++;
						
						/*********** ��������֡ ***********/
						memset(&service_frame, 0, sizeof(service_frame));
						service_frame.netID = neighbor_map[netnum]->netID;
						service_frame.referenceID = neighbor_map[netnum]->referenceID;
						service_frame.rfclock_lv = neighbor_map[netnum]->rfclock_lv;
						service_frame.r_BS = neighbor_map[netnum]->r_BS;
						service_frame.localID = neighbor_map[netnum]->localID;
						service_frame.lcclock_lv = neighbor_map[netnum]->lcclock_lv;
						service_frame.l_BS = neighbor_map[netnum]->l_BS;
						
						for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
						{
							if(bs_num == neighbor_map[netnum]->BS_num)
							{
								bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
								break;
							}
							if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
							{
								bs_num++;
								node_num = 0;
								for(j = 1; j <= MAX_NODE_CNT; j++)	/* 1~32�ڵ���ѯ */
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
									if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
										break;
								}
							}
						}
						service_frame.num = max+1;
						
						EPT(stderr, "3_10: new netNO = %d netID = %d\n", netnum, neighbor_map[netnum]->netID);				

						netnum++;   /* netnum����Ӧ��Ϊ1 */

						/* ���MAC֡ */
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

						/*********** ״̬�ı� ***********/
						nxt_state = NET;

						/* ״̬�ı䣬����״̬�ı�֡ */
						memset(&H2L_MAC_frame, 0, sizeof(H2L_MAC_frame));
						H2L_MAC_frame.referenceID = 0xff;
						H2L_MAC_frame.rfclock_lv = 0xff;
						H2L_MAC_frame.localID = localID;
						H2L_MAC_frame.lcclock_lv = 0;
						H2L_MAC_frame.state = 7;          /* �ڵ�״̬��Ϊ NET */
						H2L_MAC_frame.res = 0;
						H2L_MAC_frame.slotnum = 8;
						H2L_MAC_frame.slotlen = 40000;

						/*********** ����LowMACʱ϶�� ***********/
						net_i = 0;
						hm_LowMAC_slot_proc(net_i);    /* ����Ϊ����� */

						/*********** ����LowMACʱ϶�� ״̬�ı�֡ ����֡ ***********/
						pthread_mutex_lock(&mutex_queue[4]);
						hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						hm_queue_enter(link_queue, 4, sizeof(H2L_MAC_frame), (char *)&H2L_MAC_frame, HL_IF_DATA);	 /* �͵� TQ_4 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[4]);

						pthread_mutex_lock(&mutex_queue[5]);
						hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
						sem_post(&(empty[0]));
						pthread_mutex_unlock(&mutex_queue[5]);

						break;
					}
				}
#endif				

				else   /* �����ĸ�����Ҳ������ */
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
	U8   i;        /* for  for(i=0; i<netnum; i++)   ��¼ʱ϶�� */
	U8   net_id;   /* ��¼��ǰ�����!!!!! */
	U8   node;     /* ��¼ռ��ĳʱ϶�Ľڵ�� 11.06 */
	U8   net_count = 0;  /* ѡ������ʱ���� 1.11 */  
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc1: cache_p->type = %d\n", cache_p->type);	

	switch(cache_p->type)
	{
		case 0x08:    /* �յ���������֡ */

			/* д�� */
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
			if(netnum == 0)   /* ������Ϊ0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* ���ڽڵ�ʱ϶��ָ������ڴ� */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* ��ʱ϶�����ṹ�����ָ�� */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* ��ʼ��ʱ϶�����ṹ�� */

				neighbor_map_p = neighbor_map[netnum];    /* ��¼��ǰ�ڽڵ��ָ�� */
				neighbor_map_p->netID = id;     /* Ϊ�µ�ʱ϶���������� */ 
				
				EPT(stderr, "hm_MAC_frame_rcv_proc1: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				
				
				net_id = netnum;               /* ��¼��ǰ����� */

				netnum++;
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* ������֡������������ڵ� */
					{
						neighbor_map_p = neighbor_map[i];  /* ��¼��ǰ�ڽڵ��ָ�� */
						net_id = i;      /* ��¼��ǰ����� */   
						
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
				
				if(net_count == netnum)        /* ������֡����������ڵ� */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* ��¼��������㼶�ڵ�� */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* ���ڽڵ�ʱ϶��ָ������ڴ� */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* ��ʱ϶�����ṹ�����ָ�� */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* ��ʼ��ʱ϶�����ṹ�� */
		
							neighbor_map_p = neighbor_map[i];	   /* ��¼��ǰ�ڽڵ��ָ�� */
							neighbor_map_p->netID = id; 	/* Ϊ�µ�ʱ϶���������� */
							net_id = i;	   /* ��¼��ǰ����� */
							EPT(stderr, "hm_MAC_frame_rcv_proc1: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}

			/* ��¼�յ�NET�ڵ�����֡�Ĵ��� */
			if(service_frame_p->BS[service_frame_p->l_BS].state == NET)
			{
				neighbor_map_manage[net_id]->sf_NET_num[service_frame_p->l_BS]++;    /* ��������֡�Ľڵ��Ӧʱ϶������1 */
				if(neighbor_map_manage[net_id]->sf_NET_num[service_frame_p->l_BS] == 2)
				{
					neighbor_map_manage[net_id]->sf_flag = 1;     /* �յ�ͬһ��NET�ڵ������֡2�� */
				}
			}

			/* ��ʼ������֡��ÿһ��������ѯ */
			for(i=0; i<service_frame_p->num; i++)
			{
				if(service_frame_p->BS[i].flag == 0)    /* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
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

				/* ������Ӧ��ʱ϶ά���߳� */					
				if(neighbor_map_p->BS[i][node].life == 0)  /* �жϴ�ʱ϶ά���߳��Ƿ��� */
				{
					sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]), 0, 0);  /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
					neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
					
					pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
					neighbor_map_p->BS[i][node].life = 1;
				}
				/* ������֡���ݶ�Ӧ������ʱ϶���� */
				neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
				neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
				neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;

				/* ���±�ռ�õ�ʱ϶���� */
				if(neighbor_map_p->BS[i][node].BS_flag == 0)
				{
					neighbor_map_p->BS[i][node].BS_flag = 1;
					if(neighbor_map_p->BS[i][0].BS_ID == 0)
					{
						neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
					}
					neighbor_map_p->BS[i][0].BS_ID++;
				}

				/* �������ж� */
				if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
				{
					neighbor_map_p->BS[i][node].hop = 1;
					sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
				}
				else
				{
					if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
					{
						neighbor_map_p->BS[i][node].hop = 2;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
					}
					else
					{
						neighbor_map_p->BS[i][node].hop = 1;
						//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
					}
				}
			}

			/* ���� */
			pthread_rwlock_unlock(&rw_lock);
			break;

		case 0xff:    /* �յ�����LowMAC����֡ */
			break;

		case 0001:    /* �յ�����ʱ϶ԤԼ�й�֡ */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%d\n", cache_p->type);
			break;
	}
}

U8 hm_MAC_frame_rcv_proc2(lm_packet_t *cache_p, U8 net_i)  /* ����net_i�Ǳ��ؽڵ���������� 1.12 */
{
	U8   id;        /* for  id = service_frame_p->netID*/
	U8   i;         /* for  for(i=0; i<netnum; i++)   ��¼ʱ϶�� */
	U8   net_id;   /* ��¼��ǰ����֡���ڵ������!!!!! */
	U8   node;     /* ��¼ռ��ĳʱ϶�Ľڵ�� 11.06 */	
	U8   net_count = 0;  /* ѡ������ʱ���� 1.11 */  
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc2: cache_p->type = %d\n", cache_p->type);

	switch(cache_p->type)
	{
		case 0x08:    /* �յ���������֡ */

			/* д�� */
			pthread_rwlock_wrlock(&rw_lock);

			id = service_frame_p->netID;
			//EPT(stderr, "hm_MAC_frame_rcv_proc2: id = %d\n", id);

			if(netnum == 0)   /* ������Ϊ0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));     /* ���ڽڵ�ʱ϶��ָ������ڴ� */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));    /* ��ʱ϶�����ṹ�����ָ�� */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));  /* ��ʼ��ʱ϶�����ṹ�� */

				neighbor_map_p = neighbor_map[netnum];    /* ��¼��ǰ�ڽڵ��ָ�� */
				neighbor_map_p->netID = id;     /* Ϊ�µ�ʱ϶���������� */
				net_id = netnum;               /* ��¼��ǰ����� */
				
				EPT(stderr, "hm_MAC_frame_rcv_proc2: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				

				netnum++;
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* ������֡������������ڵ� */
					{
						neighbor_map_p = neighbor_map[i];  /* ��¼��ǰ�ڽڵ��ָ�� */
						net_id = i;      /* ��¼��ǰ����� */   
						
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
				
				if(net_count == netnum)        /* ������֡����������ڵ� */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* ��¼��������㼶�ڵ�� */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* ���ڽڵ�ʱ϶��ָ������ڴ� */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* ��ʱ϶�����ṹ�����ָ�� */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* ��ʼ��ʱ϶�����ṹ�� */
		
							neighbor_map_p = neighbor_map[i];	   /* ��¼��ǰ�ڽڵ��ָ�� */
							neighbor_map_p->netID = id; 	/* Ϊ�µ�ʱ϶���������� */
							net_id = i;	   /* ��¼��ǰ����� */
							EPT(stderr, "hm_MAC_frame_rcv_proc2: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}
			
			if(net_id == net_i)     /* �����յ�������֡���ڱ����� */
			{
				if(service_frame_p->localID == neighbor_map_p->referenceID)    /* �����յ��ϼ��ڵ�����֡ */
				{
					neighbor_map_manage[net_id]->sf_rf_get = 1;

					//EPT(stderr, "11\n");

					/* ��ʼ������֡��ÿһ��������ѯ */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)    /* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
							continue;

						node = service_frame_p->BS[i].BS_ID;  //EPT(stderr, "BS[%d].BS_ID = %d\n", i, node);

						/* ������Ӧ��ʱ϶ά���߳� */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							/* ����յ��ı��ڵ���Ϣ�����в����������� 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;
							
							neighbor_map_manage[net_id]->sf_lc_get = 1;   /* �ϼ��ڵ�����֡�а������ڵ���Ϣ */
							
							/*neighbor_map_p->BS[i].life = 0;
							pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
						}
						else
						{
							if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* ������֡���ݶ�Ӧ������ʱ϶���� */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* ������֡���ݶ�Ӧ������ʱ϶���� 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* ���±�ռ�õ�ʱ϶���� */
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* �������ж� */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}

							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
							}
						}
					}

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}

				else
				{
					//EPT(stderr, "22\n");
					/* ��ʼ������֡��ÿһ��������ѯ */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)    /* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* ������Ӧ��ʱ϶ά���߳� */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							/* ����յ��ı��ڵ���Ϣ�����в����������� 12.14 */
							if(i != neighbor_map_p->l_BS)
								continue;
							
							/*neighbor_map_p->BS[i].life = 0;
							pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
							pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
							sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
						}
						else
						{		
							if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* ������֡���ݶ�Ӧ������ʱ϶���� */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* ������֡���ݶ�Ӧ������ʱ϶���� 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* ���±�ռ�õ�ʱ϶���� */						
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* �������ж� */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
							}
						}
					}

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}

			else  /* �����յ�������֡�����ڱ����� */
			{
				/* ��ʼ������֡��ÿһ��������ѯ */
				for(i=0; i<service_frame_p->num; i++)
				{
					if(service_frame_p->BS[i].flag == 0)    /* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
						continue;

					node = service_frame_p->BS[i].BS_ID;

					/* ������Ӧ��ʱ϶ά���߳� */					
					if(service_frame_p->BS[i].BS_ID == localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
					{
						continue;
						/*neighbor_map_p->BS[i].life = 0;
						pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
						pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
						sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
					}
					else
					{							
						if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
						{
							sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
							neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
							
							pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
							neighbor_map_p->BS[i][node].life = 1;
						}
						/* ������֡���ݶ�Ӧ������ʱ϶���� */
						neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
					}								

					/* ���±�ռ�õ�ʱ϶���� */					
					if(neighbor_map_p->BS[i][node].BS_flag == 0)
					{
						neighbor_map_p->BS[i][node].BS_flag = 1;
						if(neighbor_map_p->BS[i][0].BS_ID == 0)
						{
							neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
						}
						neighbor_map_p->BS[i][0].BS_ID++;
					}

					/* �������ж� */
					if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)     /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
					{
						neighbor_map_p->BS[i][node].hop = 1;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
					}
					else
					{
						if(neighbor_map_p->BS_flag == 1)            /* �����ʱ϶��ѡ����BS   �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}
						}

						if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)     /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
						{
							neighbor_map_p->BS[i][node].hop = 2;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
						}
						else
						{
							neighbor_map_p->BS[i][node].hop = 1;
							//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
						}
					}
				}

				/* ���� */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}


		case 0xff:    /* �յ�����LowMAC����֡ */
			break;

		case 0001:    /* �յ�����ʱ϶ԤԼ�й�֡ */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%x", cache_p->type);
			break;
	}

	return(cache_p->type);   /* ��������ֵ */
}

U8 hm_MAC_frame_rcv_proc3(lm_packet_t *cache_p, U8 net_i)
{
	U8	 id;		/* for	id = service_frame_p->netID*/
	U8	 i; 		/* for	for(i=0; i<netnum; i++)   ��¼ʱ϶�� */
	U8	 net_id;   /* ��¼��ǰ�����!!!!! */
	U8   node;     /* ��¼ռ��ĳʱ϶�Ľڵ�� 11.06 */	
	int  res;
	U8   net_count = 0;  /* ѡ������ʱ���� 1.11 */
	
	neighbor_map_t *neighbor_map_p;
	service_frame_t *service_frame_p = (service_frame_t *)((mac_packet_t *)cache_p->data)->data;
	//EPT(stderr, "hm_MAC_frame_rcv_proc3: cache_p->type = %d\n", cache_p->type);

	switch(cache_p->type)
	{
		case 0x08:	  /* �յ���������֡ */

			/* д�� */
			pthread_rwlock_wrlock(&rw_lock);

			id = service_frame_p->netID;
			//EPT(stderr, "hm_MAC_frame_rcv_proc3: id = %d\n", id);

			if(netnum == 0)   /* ������Ϊ0 */
			{
				netID[netnum] = id;

				neighbor_map[netnum] = malloc(sizeof(neighbor_map_t));	   /* ���ڽڵ�ʱ϶��ָ������ڴ� */
				memset(neighbor_map[netnum], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */

				neighbor_map_manage[netnum] = malloc(sizeof(neighbor_map_manage_t));	/* ��ʱ϶�����ṹ�����ָ�� */
				memset(neighbor_map_manage[netnum], 0, sizeof(neighbor_map_manage_t));	/* ��ʼ��ʱ϶�����ṹ�� */

				neighbor_map_p = neighbor_map[netnum];	  /* ��¼��ǰ�ڽڵ��ָ�� */
				neighbor_map_p->netID = id; 	/* Ϊ�µ�ʱ϶���������� */
				net_id = netnum;			   /* ��¼��ǰ����� */

				EPT(stderr, "hm_MAC_frame_rcv_proc3: new0 netNO = %d netID = %d\n", netnum, neighbor_map_p->netID);				

				netnum++;      /* ����������Ϊ1 */
			}
			else
			{
				for(i=0; i<MAX_NET_NUM; i++)
				{
					if(id == netID[i])    /* ������֡������������ڵ� */
					{
						neighbor_map_p = neighbor_map[i];  /* ��¼��ǰ�ڽڵ��ָ�� */
						net_id = i;      /* ��¼��ǰ����� */   
						
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
				
				if(net_count == netnum)        /* ������֡����������ڵ� */
				{
					for(i=0; i<MAX_NET_NUM; i++)
					{
						if(netID[i] == 0)
						{							
							netID[i] = id;    /* ��¼��������㼶�ڵ�� */
							
							neighbor_map[i] = malloc(sizeof(neighbor_map_t));	   /* ���ڽڵ�ʱ϶��ָ������ڴ� */
							memset(neighbor_map[i], 0, sizeof(neighbor_map_t));   /* ��ʼ���ڽڵ�ʱ϶�� */
		
							neighbor_map_manage[i] = malloc(sizeof(neighbor_map_manage_t));	/* ��ʱ϶�����ṹ�����ָ�� */
							memset(neighbor_map_manage[i], 0, sizeof(neighbor_map_manage_t));	/* ��ʼ��ʱ϶�����ṹ�� */
		
							neighbor_map_p = neighbor_map[i];	   /* ��¼��ǰ�ڽڵ��ָ�� */
							neighbor_map_p->netID = id; 	/* Ϊ�µ�ʱ϶���������� */
							net_id = i;	   /* ��¼��ǰ����� */
							EPT(stderr, "hm_MAC_frame_rcv_proc3: new netNO = %d netID = %d\n", net_id, neighbor_map_p->netID);	
							break;
						}
					}					
					netnum++;
				}
			}

			if(net_id == net_i) 	/* �����յ�������֡���ڱ����� */
			{
				/* ��¼�յ�����������֡ */
				neighbor_map_manage[net_id]->sf_samenet_get = 1;			
				EPT(stderr, "hm_MAC_frame_rcv_proc3: samenet sf\n");	

				if(service_frame_p->localID == neighbor_map_p->referenceID)    /* �����յ��ϼ��ڵ㷢�͵�����֡ */
				{
					EPT(stderr, "hm_MAC_frame_rcv_proc3: rf sf\n");
					neighbor_map_manage[net_id]->sf_rf_num++;  /* ��¼�յ��ϼ��ڵ�����֡�Ĵ���/�޸�Ϊ����1�� 1.10 */					

					/* ��ʼ������֡��ÿһ��������ѯ */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)	/* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* ������Ӧ��ʱ϶ά���߳� */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							/* ����յ��ı��ڵ���Ϣ�����в����������� 12.14 */
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
							if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
							}
							/* ������֡���ݶ�Ӧ������ʱ϶���� */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* ������֡���ݶ�Ӧ������ʱ϶���� 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* ���±�ռ�õ�ʱ϶���� */						
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* �������ж� */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
							}
						}
					}

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}

				else
				{
					/* ��ʼ������֡��ÿһ��������ѯ */
					for(i=0; i<service_frame_p->num; i++)
					{
						if(service_frame_p->BS[i].flag == 0)	/* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
							continue;

						node = service_frame_p->BS[i].BS_ID;

						/* ������Ӧ��ʱ϶ά���߳� */
						if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							/* ����յ��ı��ڵ���Ϣ�����в����������� 12.14 */
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
							if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
							{
								sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
								neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
								
								pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
								neighbor_map_p->BS[i][node].life = 1;
								//EPT(stderr, "test\n");
							}
							/* ������֡���ݶ�Ӧ������ʱ϶���� */
							neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
							neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
							neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
						}

						/* ������֡���ݶ�Ӧ������ʱ϶���� 
						neighbor_map_p->BS[i].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i].state = service_frame_p->BS[i].state;*/

						/* ���±�ռ�õ�ʱ϶���� */
						if(neighbor_map_p->BS[i][node].BS_flag == 0)
						{
							neighbor_map_p->BS[i][node].BS_flag = 1;
							if(neighbor_map_p->BS[i][0].BS_ID == 0)
							{
								neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
							}
							neighbor_map_p->BS[i][0].BS_ID++;
						}

						/* �������ж� */
						if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
						}
						else
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}


							if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
							{
								neighbor_map_p->BS[i][node].hop = 2;
								sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
							}
							else
							{
								neighbor_map_p->BS[i][node].hop = 1;
								//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
							}
						}
					}

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}

			else		 /* �����յ�������֡�����ڱ����� */
			{
				EPT(stderr, "hm_MAC_frame_rcv_proc3: diffnet sf\n");
				/* ��¼�յ�����������֡/�޸�Ϊnet_i 1.12 */
				neighbor_map_manage[net_i]->sf_diffnet_get = 1;

				/* ��ʼ������֡��ÿһ��������ѯ */
				for(i=0; i<service_frame_p->num; i++)
				{
					if(service_frame_p->BS[i].flag == 0)	/* �ж�����֡�ڴ˽ڵ��Ƿ�Ϊ��Ч���� */
						continue;
					
					node = service_frame_p->BS[i].BS_ID;  /* �޲� 11.23 */

					/* ������Ӧ��ʱ϶ά���߳� */					
					if(service_frame_p->BS[i].BS_ID == localID)    /* �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
					{
						continue;
						/*neighbor_map_p->BS[i].life = 0;
						pthread_cancel(neighbor_map_manage[net_id]->bs_id[i]);
						pthread_cancel(neighbor_map_manage[net_id]->bs_timer[i]);
						sem_destroy(&(neighbor_map_manage[net_id]->bs_sem[i]));*/
					}
					else
					{							
						if(neighbor_map_p->BS[i][node].life == 0)     /* �жϴ�ʱ϶ά���߳��Ƿ��� */
						{
							sem_init(&(neighbor_map_manage[net_id]->bs_sem[i][node]),0,0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ʱ϶ά���߳� */
							neighbor_map_manage[net_id]->bs_net_BS_node[i][node] = (net_id<<16) | (i<<8) | node;  /* ��¼����š�ʱ϶�š��ڵ�� */
							
							pthread_create(&(neighbor_map_manage[net_id]->bs_tid[i][node]), NULL, hm_neighbor_map_thread, (void *)&neighbor_map_manage[net_id]->bs_net_BS_node[i][node]);
							neighbor_map_p->BS[i][node].life = 1;
						}
						/* ������֡���ݶ�Ӧ������ʱ϶���� */
						neighbor_map_p->BS[i][node].BS_ID = service_frame_p->BS[i].BS_ID;
						neighbor_map_p->BS[i][node].clock_lv = service_frame_p->BS[i].clock_lv;
						neighbor_map_p->BS[i][node].state = service_frame_p->BS[i].state;
					}

					/* ���±�ռ�õ�ʱ϶���� */					
					if(neighbor_map_p->BS[i][node].BS_flag == 0)
					{
						neighbor_map_p->BS[i][node].BS_flag = 1;
						if(neighbor_map_p->BS[i][0].BS_ID == 0)
						{
							neighbor_map_p->BS_num++;     /* ���±�ռ�õ�ʱ϶���� */
						}
						neighbor_map_p->BS[i][0].BS_ID++;
					}

					/* �������ж� */
					if(service_frame_p->BS[i].BS_ID == service_frame_p->localID)	 /* �ж�����֡�ڴ˽ڵ��ǲ�������֡�ķ��ͽڵ� */
					{
						neighbor_map_p->BS[i][node].hop = 1;
						sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));		 /* ��ʱ϶������ɣ������ź� */
					}
					else
					{
						if(neighbor_map_p->BS_flag == 1)			/* �����ʱ϶��ѡ����BS   �ж�����֡�ڴ˽ڵ��ǲ��Ǳ��ڵ� */
						{
							if(service_frame_p->BS[i].BS_ID == neighbor_map_p->localID)
							{
								neighbor_map_p->BS[i][node].hop = 0;
								continue;
							}
						}
						
						/* �п�����һ���ڵ�ͨ�������ڵ㷢��������֡���������ж��������һ���ٸĳ����� */
						if(neighbor_map_p->BS[i][node].hop == 0 || neighbor_map_p->BS[i][node].hop == 2)	 /* �����ʱ϶���Ӧ�ڵ������Ϊ0��2 ��2 ���� ��1 */
						{
							neighbor_map_p->BS[i][node].hop = 2;
							sem_post(&(neighbor_map_manage[net_id]->bs_sem[i][node]));	 /* ��ʱ϶������ɣ������ź� */
						}
						else  /* �����ͨ�������ڵ㷢����һ���ڵ����Ϣ����ô�����ź������ȴ˽ڵ��Լ����� */
						{
							neighbor_map_p->BS[i][node].hop = 1;
							//sem_post(&(neighbor_map_manage[net_id]->bs_sem[i]));	 /* ��ʱ϶������ɣ������ź� */
						}
					}
				}

				/* ���� */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}


		case 0xff:	  /* �յ�����LowMAC����֡ */
			break;

		case 0001:	  /* �յ�����ʱ϶ԤԼ�й�֡ */
			break;

		default:
			EPT(stderr, "highmac layer slot process receive unknown MAC frame, no=%x", cache_p->type);
			break;
	}

	//EPT(stderr, "hm_MAC_frame_rcv_proc3: cache_p->type = %d\n", cache_p->type);
	return(cache_p->type);   /* ��������ֵ */
}

/* �������޸� 11.07 */
void *hm_neighbor_map_thread(void *i_p)
{
	U8 net_i = (*(U32 *)i_p) >> 16;
	U8 bs_i = (*(U32 *)i_p) >> 8;
	U8 node_i = *(U32 *)i_p;
	int res;
	void *thread_result;

	EPT(stderr, "hm_neighbor_map_thread: net_i = %d  bs_i = %d  node = %d\n", net_i, bs_i, node_i);

	res = pthread_create(&(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]), NULL, hm_bs_life_thread, i_p);  /* �ȿ���һ�� */ 
	//EPT(stderr, "pthread_create error code0 %d\n", res);

	while(1)   /* ģ���ڽڵ����ĸ��� */
	{
		sem_wait(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));

		/* д�� */
		pthread_rwlock_wrlock(&rw_lock);
		if(neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] == 1)   /* ��ʱ������ */
		{
			neighbor_map[net_i]->BS[bs_i][node_i].BS_ID = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].BS_flag = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].clock_lv = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].state = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].hop = 0;
			neighbor_map[net_i]->BS[bs_i][node_i].life = 0;    /* �����ʱ���ﵽ�Ļ���˵���˱����ѹ��ڣ���ôʱ϶ά���߳�ҲӦ����ֹ! */

			/* �ڵ�������� */
			neighbor_map[net_i]->BS[bs_i][0].BS_ID--;
			if(neighbor_map[net_i]->BS[bs_i][0].BS_ID == 0)
			{
				neighbor_map[net_i]->BS_num--;
				EPT(stderr, "hm_neighbor_map_thread: net_i = %d BS_num = %d\n", net_i, neighbor_map[net_i]->BS_num);

				/* ��ʱ���������ʱ϶���нڵ���Ϊ�գ���ɾ����Ӧ������� 1.12 */
				if(neighbor_map[net_i]->BS_num == 0)
				{
					free(neighbor_map[net_i]);
					neighbor_map[net_i] = NULL;
					free(neighbor_map_manage[net_i]);
					neighbor_map_manage[net_i] = NULL;

					netID[net_i] = 0;
					netnum--;
						
					EPT(stderr, "hm_neighbor_map_thread: net_i = %d is gone\n", net_i);
					
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					break;
				}
			}
			
			/* ��ղ��� */
			sem_destroy(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));
			neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] = 0;             /* ��ʱ�������ı������ */
			neighbor_map_manage[net_i]->bs_net_BS_node[bs_i][node_i] = 0;          /* �����ʱ϶������ */
			
#if 0
			EPT(stderr, "hm_neighbor_map_thread: BS[%d] time up2\n", bs_i);
#endif

			/* ���� */
			pthread_rwlock_unlock(&rw_lock);
			break;
		}

		else
		{
			if(neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] == 0)
			{
				res = pthread_cancel(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]);    /* �������ڶ�ʱ������رն�ʱ�� */
				if(res != 0)
					EPT(stderr, "hm_neighbor_map_thread: pthread_cancel error code %d\n", res);

				res = pthread_join(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i], &thread_result);
				if(res != 0)
					EPT(stderr, "hm_neighbor_map_thread: pthread_join error code %d\n", res);
			}
				
			res = pthread_create(&(neighbor_map_manage[net_i]->bs_timer[bs_i][node_i]), NULL, hm_bs_life_thread, i_p);   /* �޸���Ͽ�����ʱ */
			if(res != 0)
				EPT(stderr, "hm_neighbor_map_thread: pthread_create error code %d\n", res);
		}

		/* ���� */
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
	tv.tv_sec = 0;      /* �д����� */ 
	tv.tv_usec = 580000;  /* ������ʱ���ϸ���������֡��ʱ�������㣬ȷ������ĳЩ����ʱĳЩ�ڵ��Ѿ����� 11.15 */
    select(0,NULL,NULL,NULL,&tv);

	//usleep(600000);

#ifdef _HM_TEST
		EPT(stderr, "hm_bs_life_thread: net_i = %d BS[%d][%d] time up\n", net_i, bs_i, node_i);
#endif

	/* д�� */
	pthread_rwlock_wrlock(&rw_lock);

	neighbor_map_manage[net_i]->bs_timeup[bs_i][node_i] = 1;      /* ��ʱ������Ҫ��� */

	/* ���� */
	pthread_rwlock_unlock(&rw_lock);

	sem_post(&(neighbor_map_manage[net_i]->bs_sem[bs_i][node_i]));
}


/* ����LowMAC��ʱ϶�� */
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
	
	/* ���ڵ�ҵ��ʱ϶ռ���㷨 */
    for(i=0; i<MAX_CFS_PSF; i++)
    {
    	if(neighbor_map[netnum]->BS[i][0].BS_ID)
			max = i+1;    /* ��¼���ʱ϶�ڵ���� */
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
	U8 node_num;  /* ��¼ռ��ĳһʱ϶�Ľڵ����� */
	U8 bs_num = 0;  /* ��¼��ռ�õ�ʱ϶�� */
	U8 max;

	pthread_detach(pthread_self());
	while(1)
	{
		sem_wait(&(empty[2]));
		net_i = *(U8 *)arg;
		//EPT(stderr, "hm_sf_ls_send_thread: net_i = %d\n", net_i);

		/* ���� */
		pthread_rwlock_rdlock(&rw_lock);

		/*********** ��������֡/��ά�޸� 11.06 ***********/
		memset(&service_frame, 0, sizeof(service_frame));
		service_frame.netID = neighbor_map[net_i]->netID;
		service_frame.referenceID = neighbor_map[net_i]->referenceID;
		service_frame.rfclock_lv = neighbor_map[net_i]->rfclock_lv;
		service_frame.r_BS = neighbor_map[net_i]->r_BS;
		service_frame.localID = neighbor_map[net_i]->localID;
		service_frame.lcclock_lv = neighbor_map[net_i]->lcclock_lv;
		service_frame.l_BS = neighbor_map[net_i]->l_BS;

		/* �����ٴ��Ż�������BS_num 11.06/�ٴ��Ż� 11.07 */
		for(i=0; i<MAX_CFS_PSF + 1; i++)  /* 0~31ʱ϶��ѯ/+1ȷ��bs_num������ 1.9 */
		{
			if(bs_num == neighbor_map[net_i]->BS_num)
			{
				//EPT(stderr, "hm_sf_ls_send_thread: bs_num = %d\n", bs_num);
				bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
				break;
			}
			if(neighbor_map[net_i]->BS[i][0].BS_ID != 0)
			{
				bs_num++;
				node_num = 0;
				for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
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
					if(node_num == neighbor_map[net_i]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
						break;
				}
			}
		}
		service_frame.num = max+1;
		//EPT(stderr, "hm_sf_ls_send_thread: service_frame.num = %d\n", service_frame.num);
		
		/* ���MAC֡ */
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

		/*********** ����LowMACʱ϶�� ***********/
		hm_LowMAC_slot_proc(net_i);

		/* ���� */
		pthread_rwlock_unlock(&rw_lock);

		/*********** ����LowMACʱ϶�� ����֡ ***********/
		pthread_mutex_lock(&mutex_queue[4]);
		hm_queue_enter(link_queue, 4, sizeof(LM_neighbor_map), (char *)&LM_neighbor_map, HL_ST_DATA);	 /* �͵� TQ_4 ���� */
		sem_post(&(empty[0]));
		pthread_mutex_unlock(&mutex_queue[4]);

		pthread_mutex_lock(&mutex_queue[5]);
		hm_queue_enter(link_queue, 5, 8+sizeof(service_frame), (char *)&mac_packet, HL_SF_DATA);     /* �͵� TQ_5 ���� */
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

		/* ���� */
		pthread_rwlock_rdlock(&rw_lock);	

		EPT(fp, "neighbor_map[%d]->netID = %d  BS_flag = %d  referenceID = %d  rfclock_lv = %d r_BS = %d  localID = %d  lcclock_lv = %d  l_BS = %d  BS_num = %d\n", 
			netnum, neighbor_map[netnum]->netID, neighbor_map[netnum]->BS_flag, 
			neighbor_map[netnum]->referenceID, neighbor_map[netnum]->rfclock_lv, neighbor_map[netnum]->r_BS,
			neighbor_map[netnum]->localID, neighbor_map[netnum]->lcclock_lv, neighbor_map[netnum]->l_BS,
			neighbor_map[netnum]->BS_num);
		
		for(i=0; i<MAX_CFS_PSF; i++)  /* 0~31ʱ϶��ѯ */
		{
			node_num = 0;
			for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
			{
				if(node_num == neighbor_map[netnum]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
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
		for(i=0; i<MAX_CFS_PSF; i++)  /* 0~31ʱ϶��ѯ */
		{
			if(bs_num == neighbor_map[netnum]->BS_num)
			{
				bs_num = 0;  /* ��֤����ʹ�õ���ȷ�� */
				break;
			}
			if(neighbor_map[netnum]->BS[i][0].BS_ID != 0)
			{
				bs_num++;
				node_num = 0;
				for(j = 1; j <= MAX_NODE_CNT; j++)  /* 1~32�ڵ���ѯ */
				{					
					if(neighbor_map[netnum]->BS[i][j].BS_flag)
					{
						node_num++;
						EPT(stderr, "neighbor_map[%d]->BS[%d][%d].BS_ID = %d  state = %d  clock_lv = %d  life = %d  hop = %d\n", netnum, i, j,
							neighbor_map[netnum]->BS[i][j].BS_ID, neighbor_map[netnum]->BS[i][j].state, neighbor_map[netnum]->BS[i][j].clock_lv,
							neighbor_map[netnum]->BS[i][j].life, neighbor_map[netnum]->BS[i][j].hop);						
					}
					if(node_num == neighbor_map[netnum]->BS[i][0].BS_ID)  /* �鿴�Ƿ����ռ�ô�ʱ϶�Ľڵ����� */
						break;
				}
			}
		}
#endif
		
		/* ���� */
		pthread_rwlock_unlock(&rw_lock);
	}
}
#endif
