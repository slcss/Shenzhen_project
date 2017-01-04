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

/* ������� */
extern link_queue_t link_queue[];

/* �ź��� */
extern sem_t  empty[];

/* ���еĻ����� */
extern pthread_mutex_t mutex_queue[];

/* ���ؽڵ�ID */
extern U8 localID;

/* ���ؽڵ��������� */
extern U8 *netp;

/* �����ڽڵ�ʱ϶�� ָ������ ������ÿһ��Ԫ�ض��� neighbor_map_t ���͵�ָ�� */
extern neighbor_map_t *neighbor_map[];  /* ��ʱ�趨���������Ϊ32�� 11.05 */

/* ���ؽڵ�����������һ���ڽڵ�ĸ��� */
extern U8 hop1_conut;

/* ��ʱ���ṹ */
extern tsche_t nl_tsch;

/* dslot.cʹ�õĶ�ʱ�� */
extern tdslot_t dslot_timer;

/* ��ʱ�������־ */
extern U8 timer_flag1;

/* �·���ʱ϶�� */
extern LM_neighbor_map_t LM_neighbor_map;

/* �ڽڵ�������¼�� */
extern U8 hop_table[];  //hop_table[i]=0���ڵ�i����������Χ�ڣ�hop_table[i]=1���ڵ�iΪ1����hop_table[i]=2���ڵ�iΪ2�������ؽڵ���ʱ���ƣ�

/* ��д�� */
extern pthread_rwlock_t rw_lock;

/* �ڵ������ı�־λ 6.29 */
extern U8 in_net;

extern FILE* fp;

/* ͳ�ƶ��г��ȵĻ����� 16.12.08 */
extern pthread_mutex_t all_qlenth;









int data_come = 0;         //�������ݸ���
pthread_mutex_t for_data_come = PTHREAD_MUTEX_INITIALIZER;  //Ϊdata_come���ӻ����� 16.12.13
U8 dslot_order_flag = 0;  //��̬ʱ϶ԤԼ/�ͷű�־��0��ʾ����ԤԼ/�ͷŹ����У�1��ʾ����ԤԼ��2��ʾ�����ͷ�
U8 dslot_order_num = 0;	//��̬ʱ϶ÿ��֡�Ѿ�!ԤԼ��ʱ϶�ܸ���
U8 dslot_drop_num = 0;    //��̬ʱ϶ÿ��֡���ͷŵ�ʱ϶�ܸ���

/* dslot �� lm2nl �����߳̽������ݵ��������� */
lm_packet_t dslot_cache1;
lm_packet_t dslot_cache2;

/* ���������Ƿ���Զ�ȡ�ı�־ 0��ʾ���ɶ���д 1��ʾ�ɶ�����д */
int dslot_cache1_flag = 0;
int dslot_cache2_flag = 0;

/* �����͵�REQ֡ */
REQ_t REQ_frame;
/* �����͵�REP֡ */
REP_t REP_frame;
/* �����͵�ACK֡ */
ACK_t ACK_frame;
/* �����͵�DROP֡ */
DROP_t DROP_frame;

/* �����͵�MAC֡ */
//mac_packet_t mac_packet;

/* �ڵ�ά���Ķ�̬ʱ϶�ܱ��������ؽڵ���������ʹ�ã�dslot_map1��¼���ؽڵ��һ���ڽڵ㣬dslot_map2��¼�����ڵ� */
U8 dslot_map1[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];  //�������D1-D55��55����̬ʱ϶���������1-32���ڵ㣬dslot_map[0][i]��ʾi�ڵ�ռ�õ�ʱ϶������D[i][0]��ʾԤԼ/ռ��iʱ϶�ڵ���ܸ�����ֵΪ0��ʾʱ϶δռ�ã�1��ʾԤռ�ã�2��ʾ��ռ��
U8 dslot_map2[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];

/* ׼��̬ʱ϶ԤԼռ�ñ� */
U8 dslot_temp_map[MAX_DSLS_PCF+1];  //ռ�õ�ʱ϶��һ���´�ʹ��Ҫ��գ�dslot_temp_map[0]Ϊ��

/* ����REQ�Ľڵ�ͳ���յ���REP֡����������Ӧ������ĳ���ڵ� */
U8 REP_count[MAX_NODE_CNT+1];  //REP_count[0]Ϊ����ڵ��������1Ϊ����0Ϊ������

/* ����REQ�Ľڵ�ͳ���յ���REP֡Я���Ķ�̬ʱ϶ */
U8 REP_dslot[MAX_DSLS_PCF+1];  //ͳ��REP֡�а�����ʱ϶����

U8 ask_node = 0;  //��¼ʱ϶ԤԼ����ڵ�

/* ���еĸ��ֲ��� 16.12.15 */
float W = 0.1;  //����ƽ���ӳ���Ȩֵ
float M1 = 0;  //ԤԼ����
float M2 = 0;  //�ͷ�����



#if 0
void *hm_dslot_thread(void *arg)
{
	U8 i,stop = 0;
	U8 count = 0;  //��̬ʱ϶ռ�ü���
	U8 dcur_state = DINIT;
	U8 dnxt_state = DINIT;
	int rval,result = 0;
	void *p = NULL;	

	/* �����͵�MAC֡ */
	mac_packet_t mac_packet;
	
	while(0 == stop)
	{
		dcur_state = dnxt_state;
		switch(dcur_state)
		{
        	case DINIT:
				ask_node = 0;  //����¼������ڵ���0
				sem_wait(&(empty[3]));  //��������������Ҳ�����Ǳ������������һ���ж�
				
				if(dslot_cache1_flag == 1)
				{
					EPT(stderr, "d0_1\n");
					
					dnxt_state = DREQ;
					break;
				}
				else if(dslot_order_flag == 1) //���붯̬ʱ϶ԤԼ���� 
				{
					EPT(stderr, "d0_2\n");

					/* ����REQ֡ */
					memset(&REQ_frame, 0, sizeof(REQ_frame));
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][0] == 0)  //ѡȡû�б�ռ�õĶ�̬ʱ϶
						{							
							dslot_temp_map[i] = 1;  //��¼��׼��̬ʱ϶ԤԼռ�ñ��У��������֮�󻹻��õ�
							dslot_map[i][localID] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
							count++;
							if(count == dslot_order_num)
							{
								break;
								count = 0;
							}
						}
					}
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* ���MAC֡ */
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
					
					/* �·�REQ֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;
					
					dnxt_state = DREP;
					break;
				}
				else if(dslot_order_flag == 2) //���붯̬ʱ϶�ͷŹ��� 
				{
					EPT(stderr, "d0_3\n");

					/* ����DROP֡ */
					memset(&DROP_frame, 0, sizeof(DROP_frame));
					DROP_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][localID] == 2)  //ѡȡ��ռ�õĶ�̬ʱ϶
						{	
							dslot_map[i][localID] = 0;
							dslot_map[i][0]--;
							dslot_map[0][localID]--;	 //��ղ���						
							DROP_frame.slot_select[i] = 1;  //��¼�±��ͷŵĶ�̬ʱ϶
							
							count++;
							if(count == dslot_drop_num)
							{
								break;
								count = 0;
							}
						}
					}
					count = 0;  //��ֹ�ͷŵ�ʱ϶��ռ�е�ʱ϶����
					
					/* ���MAC֡ */
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
					
					/* �·�DROP֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(DROP_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬���ͷŹ��̽���
					break;
				}
				else
				{
					EPT(stderr, "DINIT:dslot_order_flag = %d\n", dslot_order_flag);
					break;
				}
				
			case DREQ:		
				rval = hm_dslot_MAC_rcv_proc1(&dslot_cache1);  //�ж��յ���֡����				
				
				//dslot_cache1_flag = 0;  //���dslot_cache1

				if(rval = 1)
				{
					EPT(stderr, "d1_1\n");
					
					/* ����REP֡ */
					p = (REQ_t *)((mac_packet_t *)dslot_cache1.data)->data;	
					ask_node = ((REQ_t *)p)->node;   //��¼����ڵ�
					memset(&REP_frame, 0, sizeof(REP_frame));
					REP_frame.select_flag = 2;						
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REQ_t *)p)->slot_select[i] == 1 && dslot_map[i][((REQ_t *)p)->node] == 0)
						{
							dslot_map[i][((REQ_t *)p)->node] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
							REP_frame.slot_select[i] = 1;  //������ռ�õ�ʱ϶��¼��REP֡��
						}
						else
							REP_frame.select_flag = 1;	
					}					
					REP_frame.node_REQ = ((REQ_t *)p)->node;
					REP_frame.node = localID;								
					
					p = NULL;
					dslot_cache1_flag = 0;  //���dslot_cache1

					/* ���MAC֡ */
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
					
					/* �·�REP֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DACK;
					break;
				}

				if(rval = 2)
				{
					EPT(stderr, "d1_2\n");					
					
					/* ����REP֡ */
					p = (REQ_t *)((mac_packet_t *)dslot_cache1.data)->data;					
					memset(&REP_frame, 0, sizeof(REP_frame));				
					REP_frame.node_REQ = ((REQ_t *)p)->node;
					REP_frame.node = localID;					
					REP_frame.select_flag = 0;
					
					p = NULL;
					dslot_cache1_flag = 0;  //���dslot_cache1

					/* ���MAC֡ */
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
					
					/* �·�REP֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}

				if(rval = 3)
				{
					EPT(stderr, "d1_3\n");
					dslot_cache1_flag = 0;  //���dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}

				if(rval = 4)
				{
					EPT(stderr, "d1_4\n");					

					/* �յ�1���ڵ㷢��������ռ��REP����ʱ������ȷ��ԤԼ�ɹ�����Ҫ�ȴ�ʱ϶��ĸ��� */
					p = (REP_t *)((mac_packet_t *)dslot_cache1.data)->data;
					ask_node = ((REP_t *)p)->node_REQ;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REP_t *)p)->slot_select[i] == 1)
							dslot_map[i][((REP_t *)p)->node_REQ] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
					}					
					
					/* ������ʱ��4��ʱ���ݶ�������֡ */
					nl_tsch.tmask = nl_tsch.tmask^(1<3);
					while(1)
					{						
						sem_wait(&(empty[4]));  //�ȴ����������ݻ��߶�ʱ������
						if(timer_flag1 == 1)    //��ʱ���������˳�
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
					/* ��û��ռ�õ�Ԥռ��ʱ϶��� */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(((REP_t *)p)->slot_select[i] == 1 && dslot_map[i][((REP_t *)p)->node_REQ] == 1)
							dslot_map[i][((REP_t *)p)->node_REQ] = 0;  //��ѡ�õ�ʱ϶���ΪԤռ��
					}
					p = NULL;
					dslot_cache1_flag = 0;  //���dslot_cache1
					
					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}

				if(rval = 5)
				{
					EPT(stderr, "d1_5\n");
					dslot_cache1_flag = 0;  //���dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}

				if(rval = 6)
				{
					EPT(stderr, "d1_6\n");
					
					p = (ACK_t *)((mac_packet_t *)dslot_cache1.data)->data;
					/* ���¶�̬ʱ϶�� */
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
					dslot_cache1_flag = 0;  //���dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}

				if(rval = 7)
				{
					EPT(stderr, "d1_7\n");

					p = (DROP_t *)((mac_packet_t *)dslot_cache1.data)->data;
					/* ���¶�̬ʱ϶�� */
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
					dslot_cache1_flag = 0;  //���dslot_cache1

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���
					break;
				}
				
			case DREP:
				memset(REP_count, 0, sizeof(REP_count));
				/* ������ʱ��5��ʱ���ݶ�������֡ */
				nl_tsch.tmask = nl_tsch.tmask^(1<4);
				while(1)
				{						
					sem_wait(&(empty[4]));  //�ȴ����������ݻ��߶�ʱ������
					if(timer_flag1 == 1)    //��ʱ���������˳�
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

					/* ���¶�̬ʱ϶�� */
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

					/* ����ACK֡ */
					memset(&ACK_frame, 0, sizeof(ACK_frame));
					ACK_frame.node = localID;
					memcpy(ACK_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));

					/* ���MAC֡ */
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

					/* �·�ACK֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���				
					break;
				}
				
				else
				{
					EPT(stderr, "d2_2\n");

					/* ��֮ǰԤռ�õ�ʱ϶��� */
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_temp_map[i] == 1 && dslot_map[i][localID] == 1)
							dslot_map[i][localID] = 0;
					}
					/* ����REQ֡ */
					memset(&REQ_frame, 0, sizeof(REQ_frame));
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map[i][0] == 0)  //ѡȡû�б�ռ�õĶ�̬ʱ϶
						{							
							dslot_temp_map[i] = 1;  //��¼��׼��̬ʱ϶ԤԼռ�ñ���
							dslot_map[i][localID] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
							count++;
							if(count == dslot_order_num)
							{
								break;
								count = 0;
							}
						}
					}
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* ���MAC֡ */
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
					
					/* �·�REQ֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					data_come++;

					dnxt_state = DREP;
					break;
				}

			case DACK:
				/* ������ʱ��6��ʱ���ݶ�������֡ */
				nl_tsch.tmask = nl_tsch.tmask^(1<5);
				while(1)
				{						
					sem_wait(&(empty[4]));  //�ȴ����������ݻ��߶�ʱ������
					if(timer_flag1 == 1)    //��ʱ���������˳�
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
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���	
					break;
				}
				
				else
				{
					EPT(stderr, "d3_2\n");
					dnxt_state = DINIT;
					dslot_order_flag = 0;  //�ص�DINIT״̬��ԤԼ���̽���	
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
	dslot_order_flag = 1;  //��־�ѽ��붯̬ʱ϶ԤԼ����
	pthread_t dslot1_tid = -1;
	mac_packet_t mac_packet;

	/* ����dslot�����߳� */
	rval = pthread_create(&dslot1_tid, NULL, hm_dslot_thread, NULL);
	if (rval != 0) 	
	{
		EPT(stderr, "hm_dslot_test_thread: can not create hm_dslot_thread\n");
	}

	/* д�� */
	pthread_rwlock_wrlock(&rw_lock);

	/* ����REQ֡ */					
	memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
	REQ_frame.node = localID;
	for(i = 1; i <= MAX_DSLS_PCF; i++)
	{
		if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //ѡȡû�б�ռ�õĶ�̬ʱ϶
		{	
			EPT(stderr, "take slot %d\n", i);
			dslot_temp_map[i] = 1;  //��¼��׼��̬ʱ϶ԤԼռ�ñ��У��������֮�󻹻��õ�
			dslot_map1[i][localID] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
			dslot_map1[i][0]++;
			//dslot_map1[0][localID]++;
			
			/* ������Ӧ��ʱ�� */
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

	/* ���� */
	pthread_rwlock_unlock(&rw_lock);

	/* ���MAC֡ */
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
	
	/* �·�REQ֡ */
	pthread_mutex_lock(&mutex_queue[0]);
	hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);  /* �͵� TQ_0 ���� */
	sem_post(&(empty[0]));
	EPT(stderr, "REQ in Q0\n");
	pthread_mutex_unlock(&mutex_queue[0]);
	data_come++;

	/* ����REQ_timer */
	nl_tsch.tmask = nl_tsch.tmask^(1<<3);	
	pause();
	EPT(stderr, "pause end\n");
}

#else
void *hm_dslot_test_thread(void *arg)
{
	U8 i,stop = 0,count = 0;
	float N1 = 0;  //ƽ��ҵ������
	float N2 = 0;  //ƽ����������
	float N1max = 0;  //����600msʱ�ӣ��ڵ�ǰ�ķ��������£�֧�ֵ����ҵ������
	float N1max_1 = 0;
	float N2min = 0;  //����600msʱ�ӣ��ڵ�ǰ��ҵ�������£�֧�ֵ���С��������
	float sum = 0;  //10��data_come�ĺ�
	float avg_new = 0;	//�µ�ƽ���ӳ�
	float avg_old = 0;	//�ɵ�ƽ���ӳ�
	int q_len = 0;		//˲ʱ�ӳ�
	mac_packet_t mac_packet;
	int rval;
	pthread_t dslot1_tid = -1;
	U8 temp = 0;   //��¼ÿ��ԤԼʱ϶��Ŀ������ֵ
	U8 order_temp = 0;   //��¼ÿ��ԤԼʱ϶��Ŀ����ʱֵ
	U8 order_temp_num = 0;   //��¼��ͬԤԼʱ϶��Ŀ�Ĵ���
	U8 drop_temp = 0;   //��¼ÿ���ͷ�ʱ϶��Ŀ����ʱֵ
	U8 drop_temp_num = 0;   //��¼��ͬ�ͷ�ʱ϶��Ŀ�Ĵ���
	

	/* ����dslot�����߳� */
	rval = pthread_create(&dslot1_tid, NULL, hm_dslot_thread, NULL);
	if (rval != 0) 	
	{
		EPT(stderr, "hm_dslot_test_thread: can not create hm_dslot_thread\n");
	}

	for(i = 1; i<= 10; i++)
	{
		usleep(64000);  //ÿ��֡ͳ��һ��
		pthread_mutex_lock(&for_data_come);
		sum = sum + data_come;		
		hm_dslot_test_queue_enter(link_queue, 6, &data_come);  //��10��ͳ�Ƶ�data_comeֵ������У����еĳ������Ϊ10
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

		/* ������ֲ��� */
		N1 = sum/10;              EPT(fp, "\nN1 = %f\n", N1);
		N2 = hm_get_slot_num();  EPT(fp, "N2 = %f\n", N2);
		if(!N2)  //�ڵ�û�����Ͳ�������̬ʱ϶ԤԼ
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
		
		if(N1 > N1max || N1 < (N2min))  //��Ҫ�ı�W�����Ըı�ӳ�  
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
		//Ϊ�Ѿ�ԤԼ���Ķ�̬ʱ϶��Ŀ
		N1max_1 = 2*(N2-dslot_order_num)*(4*(N2-dslot_order_num)-1)/(8*(N2-dslot_order_num)-1);  EPT(fp, "N1max_1 = %f\n", N1max_1);
		N2min = ((N1+0.25)+sqrt((N1+0.25)*(N1+0.25)-N1/2))/2;   EPT(fp, "N2min = %f\n", N2min);
		
		if(N1 > N1max || N2 < N2min || (N2min < N1max && (int)N2min != (int)N1max))  //��Ҫ�ı�W�����Ըı�ӳ�  
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

		/* �ж�ԤԼ/�ͷ����� */
		if(avg_new > M1 && in_net == 2)  //�����ڵ��ʱ϶ԤԼ
		{
			EPT(fp, "111\n");
			if((N1+0) >= N1max || N2 <= N2min)
			{
				EPT(fp, "222\n");	
				/* ԤԼ��̬ʱ϶ */
				if(N2min+0-N2 <= 0)
					continue;
				if((N2min+0-N2) == (int)(N2min+0-N2))
					temp = (int)(N2min+0-N2);
				else
					temp = (int)(N2min+0-N2+1);

				/* ��ֹ���ԤԼ������3��ԤԼʱ϶��Ŀ��ͬ�ſ�ʼԤԼ 16.12.23 */
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
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //ѡȡû�б�ռ�õĶ�̬ʱ϶
						{						
							dslot_map1[i][localID] = 2;  //��ѡ�õ�ʱ϶���Ϊ��ռ��
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
					count = 0; //��ֹԤԼ�ıȿ��õĻ���
					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
				}
			}			
		}
		else if(avg_new > M1 && in_net == 1)  //ʱ϶ԤԼ��������ڵ������������
		{
			EPT(fp, "333\n");
			if((N1+0) >= N1max || N2 <= N2min)
			{
				EPT(fp, "444\n");
				/* ԤԼ��̬ʱ϶������Ϊÿ��֡N1-N2 */
				//dslot_order_num = (int)(N1+0.5) - (int)(N2+0.5);
				/* ԤԼ��̬ʱ϶ */
				if(N2min-N2 <= 0)
					continue;
				if((N2min-N2) == (int)(N2min-N2))
					temp = (int)(N2min-N2);
				else
					temp = (int)(N2min-N2+1);

				/* ��ֹ���ԤԼ������3��ԤԼʱ϶��Ŀ��ͬ�ſ�ʼԤԼ 16.12.23 */
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
					dslot_order_flag = 1;  //��־�ѽ��붯̬ʱ϶ԤԼ����

					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/* ����REQ֡ */					
					memset(dslot_temp_map, 0, sizeof(dslot_temp_map));
					REQ_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][0] == 0 && dslot_map2[i][0] == 0)  //ѡȡû�б�ռ�õĶ�̬ʱ϶
						{							
							dslot_temp_map[i] = 1;  //��¼��׼��̬ʱ϶ԤԼռ�ñ��У��������֮�󻹻��õ�
							dslot_map1[i][localID] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
							dslot_map1[i][0]++;
							//dslot_map1[0][localID]++;
							
							/* ������Ӧ��ʱ�� */
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
					count = 0; //��ֹԤԼ�ıȿ��õĻ���
					memcpy(REQ_frame.slot_select, dslot_temp_map, sizeof(dslot_temp_map));	

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);

					/* ���MAC֡ */
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
					
					/* �·�REQ֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(REQ_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
					sem_post(&(empty[0]));
					pthread_mutex_unlock(&mutex_queue[0]);
					pthread_mutex_lock(&for_data_come);
					data_come++;
					pthread_mutex_unlock(&for_data_come);

					/* ����REQ_timer */
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
				/* �ͷŶ�̬ʱ϶ */
				if(N2min-N2 >= 0)
					continue;
				if((N2-N2min) == (int)(N2-N2min))
					dslot_drop_num = (int)(N2-N2min);
				else
					dslot_drop_num = (int)(N2-N2min);	

				/* ��ֹ���ԤԼ������4���ͷ�ʱ϶��Ŀ��ͬ�ſ�ʼԤԼ 16.12.23 */
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
					/* д�� */
					pthread_rwlock_wrlock(&rw_lock);

					/* ����DROP֡ */
					memset(&DROP_frame, 0, sizeof(DROP_frame));
					DROP_frame.node = localID;
					for(i = 1; i <= MAX_DSLS_PCF; i++)
					{
						if(dslot_map1[i][localID] == 2)  //ѡȡ��ռ�õĶ�̬ʱ϶
						{	
							dslot_map1[i][localID] = 0;
							dslot_map1[i][0]--;
							dslot_map1[0][localID]--;	 //��ղ���		
							dslot_order_num--;
							DROP_frame.slot_select[i] = 1;  //��¼�±��ͷŵĶ�̬ʱ϶

							/* �رն�Ӧ��ʱ�� */							
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
					count = 0;  //��ֹ�ͷŵ�ʱ϶��ռ�е�ʱ϶����

					/* ���� */
					pthread_rwlock_unlock(&rw_lock);
					
					/* ���MAC֡ */
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
					
					/* �·�DROP֡ */
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_enter(link_queue, 0, 8+sizeof(DROP_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
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
		sem_wait(&(empty[3]));  //�ȴ����������ݻ��߶�ʱ������
		
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
			/* д�� */
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
						dslot_map1[i][REQ_p->node] = 1;  //��ѡ�õ�ʱ϶���ΪԤռ��
						EPT(stderr, "dslot_map1[%d][%d] = %d\n", i, REQ_p->node, dslot_map1[i][REQ_p->node]);
						dslot_map1[i][0]++;
						//dslot_map1[0][REQ_p->node]++;
						REP_frame.slot_select[i] = 1;   //������ռ�õ�ʱ϶��¼��REP֡��

						/* ������Ӧ��ʱ�� */
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

			/* ���� */
			pthread_rwlock_unlock(&rw_lock);
			
			/* ���MAC֡ */
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
			
			/* �·�REP֡ */
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_enter(link_queue, 0, 8+sizeof(REP_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
			sem_post(&(empty[0]));
			pthread_mutex_unlock(&mutex_queue[0]);
			EPT(stderr, "REP in Q0\n");
			pthread_mutex_lock(&for_data_come);
			data_come++;
			pthread_mutex_unlock(&for_data_come);
			break;
			
		case REP:
			/* д�� */
			pthread_rwlock_wrlock(&rw_lock);
			
			EPT(stderr, "rcv REP\n");
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;			
			
			if(REP_p->node_REQ == localID)
			{
				if(dslot_order_flag == 0)  //������ڵ��Ѳ���ԤԼ�����У���������շ������ڵ��REP֡
				{
					/* ���� */
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

							/* �رն�Ӧ��ʱ�� */							
							dslot_timer.procs[i][0].wait--;							
							dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(localID-1));													
						}
					}
					dslot_order_flag = 0;  //�رձ��ؽڵ�ԤԼ���̱�־	
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
								/* �����ʱ��δ��ʱ������dslot_map1[i][localID]=0Ҳ������ */
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
								/* ���MAC֡ */
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

								/* �·�ACK֡ */
								pthread_mutex_lock(&mutex_queue[0]);
								hm_queue_enter(link_queue, 0, 8+sizeof(ACK_frame), (char *)&mac_packet, HL_MP_DATA);     /* �͵� TQ_0 ���� */
								sem_post(&(empty[0]));
								pthread_mutex_unlock(&mutex_queue[0]);
								EPT(stderr, "ACK in Q0\n");
								pthread_mutex_lock(&for_data_come);
								data_come++;
								pthread_mutex_unlock(&for_data_come);
							}

							/* ֹͣREQ_timer */
							nl_tsch.tmask = nl_tsch.tmask | (1<<3);
							nl_tsch.procs[3].wait = nl_tsch.procs[3].period; 
							EPT(stderr, "REQ_timer is shutdown\n");
							/* ���ͳ������ */
							memset(REP_count, 0, sizeof(REP_count));
							memset(REP_dslot, 0, sizeof(REP_dslot));
							dslot_order_flag = 0;  //�رձ��ؽڵ�ԤԼ���̱�־	
						}
					}
				}	
				/* ���� */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}	
			/* �ж�REP_p->node_REQ�Ǳ��ؽڵ�ļ����ڽڵ� */
			if(hop_table[REP_p->node_REQ] == 1)
			{	
				/* ���� */
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

								/* ������Ӧ��ʱ�� */
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
				/* ���� */
				pthread_rwlock_unlock(&rw_lock);
				break;
			}			
			
		case ACK:
			/* д�� */
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
					
					/* �رն�Ӧ��ʱ�� */							
					dslot_timer.procs[i][0].wait--;							
					dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(ACK_p->node-1));
				}
			}
			/* ���� */
			pthread_rwlock_unlock(&rw_lock);
			break;

		case DROP:
			/* д�� */
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

					/* �رն�Ӧ��ʱ�� */							
					dslot_timer.procs[i][0].wait--;							
					dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(DROP_p->node-1));
				}
			}
			/* ���� */
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
	/* ����ֵ��ÿ���ڵ�ƽ��ÿ��֡ռ�õ�ʱ϶ */
	return (result/MAX_CFS_PSF + dslot_map1[0][localID]);	
}

U8 bit_count(U8 v)
{
	U8 c; //��λ�����ۼ�

	for (c = 0; v; c++)
	{
		v &= v - 1; //ȥ����͵���λ
	}
	return c;
}

/* ������� */
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
	Q[id].real_l++;  /* ������ʵ���ȼ�1 */
	Q[id].rear = p;
	return rval;
}

/* ���ݳ��� */
int hm_dslot_test_queue_delete(link_queue_t *Q, U8 id)  /* ���ݱ����� */
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
		case 0x02:    /* �յ�����ʱ϶ԤԼ����֡REQ */
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

		case 0x03:    /* �յ�����ʱ϶ԤԼ��Ӧ֡REP */
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;
			hop = 0;

			/* �ж�REP_p->node�Ǳ��ؽڵ�ļ����ڽڵ� */
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

		case 0x04:    /* �յ�����ʱ϶ԤԼȷ��֡ACK */
			rval = 6;			
			break;

		case 0x05:    /* �յ�����ʱ϶�ͷ�֪ͨ֡DROP */
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
		case 0x02:    /* �յ�����ʱ϶ԤԼ����֡REQ */
		case 0x03:    /* �յ�����ʱ϶ԤԼ��Ӧ֡REP */
			break;
			
		case 0x04:    /* �յ�����ʱ϶ԤԼȷ��֡ACK */
			ACK_p = (ACK_t *)((mac_packet_t *)cache_p->data)->data;
			if(ACK_p->node != ask_node)
				break;
			for(i = 1; i < MAX_DSLS_PCF; i++)
			{
				/* �鿴ACK֡���ĸ�ʱ϶��ռ�� */
				if(ACK_p->slot_select[i] == 1)
				{
					/* �鿴��ʱ϶�Ƿ��ѱ���¼�� */
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
		case 0x02:    /* �յ�����ʱ϶ԤԼ����֡REQ */
		case 0x04:    /* �յ�����ʱ϶ԤԼȷ��֡ACK */
			break;

		case 0x03:    /* �յ�����ʱ϶ԤԼ��Ӧ֡REP */
			REP_p = (REP_t *)((mac_packet_t *)cache_p->data)->data;

			/* �ж�REP��Ӧ�ķ���REQ�Ľڵ��Ƿ�Ϊ���ؽڵ� */
			if(REP_p->node_REQ != localID)
				break;
			if(REP_p->select_flag == 2)  //ȫ������ռ��
			{
				if(REP_count[REP_p->node] == 0)
				{
					REP_count[REP_p->node] = 1;
					REP_count[0]++;					
				}
			}
			else if(REP_p->select_flag == 1)  //��������ռ��
			{
				if(REP_count[REP_p->node] == 0)
				{
					REP_count[REP_p->node] = 1;
					REP_count[0]++;					
				}
				for(i = 1; i <= MAX_DSLS_PCF; i++)
					dslot_temp_map[i] = dslot_temp_map[i] && REP_p->slot_select[i];  //����ԤԼ��ʱ϶

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
		case 0x02:    /* �յ�����ʱ϶ԤԼ����֡REQ */
		case 0x03:    /* �յ�����ʱ϶ԤԼ��Ӧ֡REP */
			break;

		case 0X04:	   /* �յ�����ʱ϶ԤԼȷ��֡ACK */ 
			ACK_p = (ACK_t *)((mac_packet_t *)cache_p->data)->data;
			if(ACK_p->node != ask_node)
				break;
			for(i = 1; i < MAX_DSLS_PCF; i++)
			{
				/* �鿴ACK֡���ĸ�ʱ϶��ռ�� */
				if(ACK_p->slot_select[i] == 1)
				{
					/* �鿴��ʱ϶�Ƿ��ѱ���¼�� */
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
