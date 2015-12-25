#include "../mr_common.h"
#include "hm_common.h"
#include "hm_timer.h"
#include "hm_queue_manage.h"
#include "hm_with_lowmac.h"
#include "hm_slot.h"

extern qinfo_t qinfs[];
extern const int cnt_p;

extern int  qs, re_qin, nl_qid, hm_qid, vi_qid, rp_qid, ht_qid, mac_qid;

/* ��������ָ�� */
extern link_queue_t link_queue[];
extern link_queue_t lm2nl_link_queue;

/* �ź��� */
extern sem_t  empty[];

/* slot �� lm2nl �����߳̽������ݵ��������� */
extern lm_packet_t slot_cache1;
extern lm_packet_t slot_cache2;

/* ���������Ƿ���Զ�ȡ�ı�־ 0��ʾ���ɶ���д 1��ʾ�ɶ�����д
slot����� lm2nl����д */
extern int slot_cache1_flag;
extern int slot_cache2_flag;

/* ���������������� */
hm_tshare_t  share = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	1,
	1,
	1,
	1
};
extern pthread_mutex_t mutex_queue[];

extern U8  LSN;



/* ���ؽڵ�ID */
U8 localID;

static pthread_t fq_tid = -1;
static pthread_t rcvfr_nl_tid = -1;
static pthread_t lm2nl_tid = -1;
static pthread_t sndto_lm_tid = -1;
static pthread_t slot_tid = -1;

FILE* fp;

extern int McBSP_fd;

#ifdef _NL_TEST
pthread_mutex_t NT = PTHREAD_MUTEX_INITIALIZER;
lm_packet_t nl_test;

sem_t sem_NT;
#endif


void hm_qrv_kill(int signo)
{
#ifdef _HM_TEST	
	mmsg_t   ht_msg;
	int   rcnt;
	memset(&ht_msg, 0, sizeof(ht_msg));
#endif

	EPT(stderr, "thread %lu catck a signal, no=%d\n", pthread_self(), signo);
	switch (signo)
	{
		case SIGINT:
			
#ifdef _HM_TEST
			ht_msg.mtype = 10;
			ht_msg.node = localID;
			rcnt = msgsnd(ht_qid, (void *)&ht_msg, 2048, 0);
			if (rcnt != 0)
			{
				EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 5,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
				rcnt = 1;
			}
#endif	

			EPT(stderr, "thread %lu catck a SIGINT\n", pthread_self());
			//pthread_exit((void *)1);
			exit(1);
			break;

		default:
			EPT(stderr, "thread %ld catck an unknown signal %d\n", pthread_self(), signo);
			break;
	}
}

void* hm_rcvfrom_nl_thread(void *arg)
{
	int    qid;
	int    rcnt;
	mmsg_t rx_msg;
	int    rval, stop;

	pthread_detach(pthread_self());		 /*�޸�״̬Ϊunjoinable���߳̽����Զ��ͷ���Դ*/

	qid = *(int *)arg;
	ASSERT(qinfs[re_qin].qid == qid);
	EPT(stderr, "%s: HighMAC receive from netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	if (qid < 0)
	{
		EPT(stdout, "%s: hm_rcvfrom_nl wrong receive queue id %d\n", qinfs[re_qin].pname, qid);
		rval = 1;
		goto thread_return;
	}

	rval = 0;
	stop = 0;
	while(0 == stop)
	{
		memset(&rx_msg, 0, sizeof(rx_msg));
		rcnt = msgrcv(qid, &rx_msg, MAX_DATA_LENGTH, 0, 0);
		if (rcnt < 0)
		{
			if (EIDRM != errno)
			{
				EPT(stderr, "%s: hm_rcvfrom_nl error in receiving msg, no:%d, meaning:%s\n", qinfs[re_qin].pname, errno, strerror(errno));
			}
			else
			{
				EPT(stderr, "%s: hm_rcvfrom_nl quit msg receiving thread\n", qinfs[re_qin].pname);
			}
			rval = 2;
			break;
		}

		// change the message processing function
		rval = hm_rmsg_proc(rcnt, &rx_msg);

		/* �ͷ��ź��� */
		sem_post(&(empty[0]));

		if (rval != 0)
		{
			stop = 1;
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.hm_rcvfrom_nl_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: HighMAC receive from netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void *)&rval);
}




#ifdef _HM_TEST
void* hm_lm2nl_thread(void *arg)
{
	mmsg_t       tx_msg,ht_msg;
	lm_packet_t  package;
	int 		 stop, rval, rcnt;
	U8           LSN = 0;    /* ��¼LSN */
	U8  i = 0;
	U16          type = 0;   /* �������ֳ��鲥��Ϣ */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* ��ͬ�Ĵ������� */
	stop = 0;  /* �ж�ѭ���ı�־λ */
	while(0 == stop)
	{
		memset(&tx_msg, 0, sizeof(tx_msg));
		memset(&ht_msg, 0, sizeof(ht_msg));
		memset(&package, 0, sizeof(package));

		/* judge msg receiving thread alive or not */
		if (ESRCH == pthread_kill(rcvfr_nl_tid, 0))
		{
			EPT(stderr, "%s: hm_rcvfrom_nl thread has ended.\n", qinfs[re_qin].pname);
			rval = 1;
			stop = 1;
			goto thread_return;
		}

		/* ��test����������� */
		rcnt = msgrcv(mac_qid, &ht_msg, MAX_DATA_LENGTH, 0, 0);
		//EPT(fp, "hm_lm2nl_thread: rcv done\n");
		
		if (rcnt < 0)
		{
			if (EIDRM != errno)
			{
				EPT(stderr, "%s: lm2nl error in receiving msg, no:%d, meaning:%s\n", qinfs[re_qin].pname, errno, strerror(errno));
			}
			else
			{
				EPT(stderr, "%s: lm2nl quit msg receiving thread\n", qinfs[re_qin].pname);
			}
			rval = 2;
			break;
		}
		memcpy(&package, &ht_msg.data, rcnt-1);
		//EPT(stderr, "hm_lm2nl_thread: package.type = %d\n", package.type);


#if 0
		if(i == 0)
		{
			EPT(stderr, "ht_msg.mtype = %ld\n", ht_msg.mtype);
			EPT(stderr, "ht_msg.node = %d\n", ht_msg.node);
			EPT(stderr, "package.len = %d\n", package.len);
			EPT(stderr, "package.type = %d\n", package.type);
			EPT(stderr, "package.Lsn = %d\n", package.Lsn);
			EPT(stderr, "package.Hsn = %d\n", package.Hsn);
			
			EPT(stderr, "netID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->netID);
			EPT(stderr, "referenceID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->referenceID);
			EPT(stderr, "rfclock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->rfclock_lv);
			EPT(stderr, "r_BS = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->r_BS);
			EPT(stderr, "localID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->localID);
			EPT(stderr, "lcclock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->lcclock_lv);
			EPT(stderr, "l_BS = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->l_BS);
			EPT(stderr, "num = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->num);
			EPT(stderr, "BS[0].flag = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].flag);
			EPT(stderr, "BS[0].BS_ID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].BS_ID);
			EPT(stderr, "BS[0].state = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].state);
			EPT(stderr, "BS[0].clock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].clock_lv);
			EPT(stderr, "BS[1].flag = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].flag);
			EPT(stderr, "BS[1].BS_ID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].BS_ID);
			EPT(stderr, "BS[1].state = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].state);
			EPT(stderr, "BS[1].clock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].clock_lv);
			i = 1;
		}
#endif

#if 0
		/* �����յ���LSN�Ƿ���ȷ */
		if(package.Lsn == LSN)
		{
			LSN++;
			if(LSN == 16)
				LSN = 0;
		}
		else
			EPT(stderr, "%s: HighMAC receive a wrong lm LSN, LSN = %d\n", qinfs[re_qin].pname, package.Lsn);
#endif

		/* ���ദ�� */
		switch(package.type)
		{
			case 0x00:     /* ·��Э�飬MACЭ������ */
				switch(((mac_packet_t *)package.data)->type)
				{
					case 0:     /* ·��Э��(���а����鲥����) */
						memcpy(&type, ((mac_packet_t *)package.data)->data, 2);   /* ��ȡdata�ڵ����� */
						if(type == MMSG_MAODV)   /* ���鲥���� */
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_MAODV, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
							if(rval != 0)
								goto thread_return;
							break;
						}
						else
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_RPM, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
							if(rval != 0)
								goto thread_return;
							break;
						}	

					case 1:    /* MACЭ������ (��̬ʱ϶���֡)    ��ʱ��������������  1����������������2     subt:0001 -> 1111 ��������չ */
						if(slot_cache1_flag == 0)
						{
							memset(&slot_cache1, 0, sizeof(slot_cache1));
							memcpy(&slot_cache1, &package, package.len);
							slot_cache1.type = ((mac_packet_t *)package.data)->subt;
							slot_cache1_flag = 1;
			            	sem_post(&(empty[1]));							
						}
						else
						{
							memset(&slot_cache2, 0, sizeof(slot_cache2));
							memcpy(&slot_cache2, &package, package.len);
							slot_cache2.type = ((mac_packet_t *)package.data)->subt;
							slot_cache2_flag = 1;
						}
						break;

					default:
						EPT(stderr, "%s: HighMAC receive unknown package from LowMAC 1, no=%d\n", qinfs[re_qin].pname, ((mac_packet_t *)package.data)->type);
						break;
				}
				break;

			case 0x01:     /* ����ҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x02:     /* ��Ƶҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x03:     /* ����ҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x06:     /* ת���� */
				hm_management(package.len-4, package.data);
				break;

			case 0x07:     /* ʱ϶�� */
				hm_management(package.len-4, package.data);
				break;

			case 0x08:     /* ����֡   ��ʱ�������������� */
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, &package, package.len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
					//EPT(stderr, "hm_lm2nl_thread: sem_post\n");
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, &package, package.len);
					slot_cache2_flag = 1;
				}
				break;
   
			case 0x09: 	/* BB RF */
				hm_management(package.len-4, package.data);
				break;

			case 0x0A: 	/* ������֡ */
				hm_nARQ(&package);
				break;

			case 0xff: 	/* ��֡��������֡   ��ʱ��������������   ���Ͳ���0xff */
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, &package, package.len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, &package, package.len);
					slot_cache2_flag = 1;
				}
				break;

			default:
				EPT(stderr, "%s: HighMAC receive unknown msg from LowMAC, no=%d\n", qinfs[re_qin].pname, package.type);
				break;
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.lm2nl_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}

#elif _NL_TEST 
void* hm_lm2nl_thread(void *arg)
{
	mmsg_t       tx_msg,ht_msg;
	lm_packet_t  package;
	int 		 stop, rval, rcnt;
	U8           LSN = 0;    /* ��¼LSN */
	U8  i = 0;
	U16          type = 0;   /* �������ֳ��鲥��Ϣ */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* ��ͬ�Ĵ������� */
	stop = 0;  /* �ж�ѭ���ı�־λ */
	while(0 == stop)
	{
		memset(&tx_msg, 0, sizeof(tx_msg));
		memset(&ht_msg, 0, sizeof(ht_msg));
		memset(&package, 0, sizeof(package));

		/* judge msg receiving thread alive or not */
		if (ESRCH == pthread_kill(rcvfr_nl_tid, 0))
		{
			EPT(stderr, "%s: hm_rcvfrom_nl thread has ended.\n", qinfs[re_qin].pname);
			rval = 1;
			stop = 1;
			goto thread_return;
		}

		/* ��nl_test�н������� */
		sem_wait(&sem_NT);
		pthread_mutex_lock(&NT);
		memcpy(&package, &nl_test, sizeof(nl_test));
		pthread_mutex_unlock(&NT);
		EPT(stderr, "hm_lm2nl_thread: len = %d\n", package.len);
#if 0
		if(i == 0)
		{
			EPT(stderr, "ht_msg.mtype = %ld\n", ht_msg.mtype);
			EPT(stderr, "ht_msg.node = %d\n", ht_msg.node);
			EPT(stderr, "package.len = %d\n", package.len);
			EPT(stderr, "package.type = %d\n", package.type);
			EPT(stderr, "package.Lsn = %d\n", package.Lsn);
			EPT(stderr, "package.Hsn = %d\n", package.Hsn);
			
			EPT(stderr, "netID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->netID);
			EPT(stderr, "referenceID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->referenceID);
			EPT(stderr, "rfclock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->rfclock_lv);
			EPT(stderr, "r_BS = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->r_BS);
			EPT(stderr, "localID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->localID);
			EPT(stderr, "lcclock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->lcclock_lv);
			EPT(stderr, "l_BS = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->l_BS);
			EPT(stderr, "num = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->num);
			EPT(stderr, "BS[0].flag = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].flag);
			EPT(stderr, "BS[0].BS_ID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].BS_ID);
			EPT(stderr, "BS[0].state = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].state);
			EPT(stderr, "BS[0].clock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[0].clock_lv);
			EPT(stderr, "BS[1].flag = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].flag);
			EPT(stderr, "BS[1].BS_ID = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].BS_ID);
			EPT(stderr, "BS[1].state = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].state);
			EPT(stderr, "BS[1].clock_lv = %d\n", ((service_frame_t *)((mac_packet_t *)package.data)->data)->BS[1].clock_lv);
			i = 1;
		}
#endif

#if 0
		/* �����յ���LSN�Ƿ���ȷ */
		if(package.Lsn == LSN)
		{
			LSN++;
			if(LSN == 16)
				LSN = 0;
		}
		else
			EPT(stderr, "%s: HighMAC receive a wrong lm LSN, LSN = %d\n", qinfs[re_qin].pname, package.Lsn);
#endif

		/* ���ദ�� */
		//EPT(stderr, "hm_lm2nl_thread: type = %d\n", package.type);
		switch(package.type)
		{
			case 0x00:     /* ·��Э�飬MACЭ������ */
				switch(((mac_packet_t *)package.data)->type)
				{
					case 0:     /* ·��Э��(���а����鲥����) */
						memcpy(&type, ((mac_packet_t *)package.data)->data, 2);   /* ��ȡdata�ڵ����� */
						if(type == MMSG_MAODV)   /* ���鲥���� */
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_MAODV, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
							if(rval != 0)
								goto thread_return;
							break;
						}
						else
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_RPM, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
							if(rval != 0)
								goto thread_return;
							break;
						}	

					case 1:    /* MACЭ������ (��̬ʱ϶���֡)    ��ʱ��������������  1����������������2     subt:0001 -> 1111 ��������չ */
						if(slot_cache1_flag == 0)
						{
							memset(&slot_cache1, 0, sizeof(slot_cache1));
							memcpy(&slot_cache1, &package, package.len);
							slot_cache1.type = ((mac_packet_t *)package.data)->subt;
							slot_cache1_flag = 1;
			            	sem_post(&(empty[1]));							
						}
						else
						{
							memset(&slot_cache2, 0, sizeof(slot_cache2));
							memcpy(&slot_cache2, &package, package.len);
							slot_cache2.type = ((mac_packet_t *)package.data)->subt;
							slot_cache2_flag = 1;
						}
						break;

					default:
						EPT(stderr, "%s: HighMAC receive unknown package from LowMAC 1, no=%d\n", qinfs[re_qin].pname, ((mac_packet_t *)package.data)->type);
						break;
				}
				break;

			case 0x01:     /* ����ҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x02:     /* ��Ƶҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x03:     /* ����ҵ������ */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x06:     /* ת���� */
				hm_management(package.len-4, package.data);
				break;

			case 0x07:     /* ʱ϶�� */
				hm_management(package.len-4, package.data);
				break;

			case 0x08:     /* ����֡   ��ʱ�������������� */
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, &package, package.len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
					//EPT(stderr, "hm_lm2nl_thread: sem_post\n");
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, &package, package.len);
					slot_cache2_flag = 1;
				}
				break;
   
			case 0x09: 	/* BB RF */
				hm_management(package.len-4, package.data);
				break;

			case 0x0A: 	/* ������֡ */
				hm_nARQ(&package);
				break;

			case 0xff: 	/* ��֡��������֡   ��ʱ��������������   ���Ͳ���0xff */
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, &package, package.len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, &package, package.len);
					slot_cache2_flag = 1;
				}
				break;

			default:
				EPT(stderr, "%s: HighMAC receive unknown msg from LowMAC, no=%d\n", qinfs[re_qin].pname, package.type);
				break;
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.lm2nl_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}


#else
void* hm_lm2nl_thread(void *arg)
{
	mmsg_t       tx_msg;
	lm_packet_t  package;
	int 		 stop, rval;
	//U8           LSN = 0;    /* ��¼LSN */
	U16          type = 0;   /* �������ֳ��鲥��Ϣ */

	U8           hsn[4] = {0,0,0,0};  /* ��¼4�����е���ǰhsn */
	U8           hsn_flag[4] = {0,0,0,0};  /* ��ǵ�һ�ν��յ�4�����е�hsn */
	

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* ��ͬ�Ĵ������� */
	stop = 0;  /* �ж�ѭ���ı�־λ */
	while(0 == stop)
	{
		memset(&tx_msg, 0, sizeof(tx_msg));
		memset(&package, 0, sizeof(package));

		/* judge msg receiving thread alive or not */
		if (ESRCH == pthread_kill(rcvfr_nl_tid, 0))
		{
			EPT(stderr, "%s: hm_rcvfrom_nl thread has ended.\n", qinfs[re_qin].pname);
			rval = 1;
			stop = 1;
			goto thread_return;
		}

		hm_readfm_McBSP(&package);
		EPT(stderr, "%s: HighMAC receive a LowMAC msg, type = %d\n", qinfs[re_qin].pname, package.type);

		/* �����յ���LSN�Ƿ���ȷ */
		if(package.Lsn == LSN)    
		{
			LSN++;
			if(LSN == 16)   /* LSN֮ǰ�����ݿ���ɾ�� */
				LSN = 0;
		}
		else
			EPT(stderr, "%s: HighMAC receive a wrong lm LSN, LSN = %d\n", qinfs[re_qin].pname, package.Lsn);


		/* ���ദ�� */
		switch(package.type)
		{
			case 0xff: 	/* ��֡��������֡   ��ʱ��������������   ���Ͳ���0xff */
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, &package, package.len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, &package, package.len);
					slot_cache2_flag = 1;
				}
				break;

			default:
				switch(package.type && 0x0f)
				{
					case 0x00:     /* ·��Э�飬MACЭ������ */
						switch(((mac_packet_t *)package.data)->type)
						{
							case 0:     /* ·��Э��(���а����鲥����) */
								memcpy(&type, ((mac_packet_t *)package.data)->data, 2);   /* ��ȡdata�ڵ����� */
								if(type == MMSG_MAODV)   /* ���鲥���� */
								{
									rval = hm_smsg_proc(&tx_msg, MMSG_MAODV, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
									if(rval != 0)
										goto thread_return;									
								}
								else
								{
									rval = hm_smsg_proc(&tx_msg, MMSG_RPM, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 Ϊdata����ʵ���� */
									if(rval != 0)
										goto thread_return;									
								}
								break;

							case 1:    /* MACЭ������ (��̬ʱ϶���֡)    ��ʱ��������������  1����������������2     subt:0001 -> 1111 ��������չ */
								if(slot_cache1_flag == 0)
								{
									memset(&slot_cache1, 0, sizeof(slot_cache1));
									memcpy(&slot_cache1, &package, package.len);
									slot_cache1.type = ((mac_packet_t *)package.data)->subt;
									slot_cache1_flag = 1;
					            	sem_post(&(empty[1]));
								}
								else
								{
									memset(&slot_cache2, 0, sizeof(slot_cache2));
									memcpy(&slot_cache2, &package, package.len);
									slot_cache2.type = ((mac_packet_t *)package.data)->subt;
									slot_cache2_flag = 1;
								}
								break;

							default:
								EPT(stderr, "%s: HighMAC receive unknown package from LowMAC , no=%d\n", qinfs[re_qin].pname, ((mac_packet_t *)package.data)->type);
								break;
						}
						break;

					case 0x01:     /* ����ҵ������ */
						rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
						if(rval != 0)
							goto thread_return;
						break;

					case 0x02:     /* ��Ƶҵ������ */
						rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
						if(rval != 0)
							goto thread_return;
						break;

					case 0x03:     /* ����ҵ������ */
						rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
						if(rval != 0)
							goto thread_return;
						break;

					case 0x06:     /* ת���� */
						hm_management(package.len-4, package.data);
						break;

					case 0x07:     /* ʱ϶�� */
						hm_management(package.len-4, package.data);
						break;

					case 0x08:     /* ����֡   ��ʱ�������������� */
						if(slot_cache1_flag == 0)
						{
							memset(&slot_cache1, 0, sizeof(slot_cache1));
							memcpy(&slot_cache1, &package, package.len);
							slot_cache1_flag = 1;
					        sem_post(&(empty[1]));
						}
						else
						{
							memset(&slot_cache2, 0, sizeof(slot_cache2));
							memcpy(&slot_cache2, &package, package.len);
							slot_cache2_flag = 1;
						}
						break;

					case 0x09: 	/* BB RF */
						hm_management(package.len-4, package.data);
						break;

					case 0x0A: 	/* ������֡ */
						hm_nARQ(&package);
						break;					

					default:
						EPT(stderr, "%s: HighMAC receive unknown msg from LowMAC , no=%d\n", qinfs[re_qin].pname, package.type);
						break;
				}
				break;			
		}

		switch(package.type && 0xf0)  /* �鿴���ĸ����еķ���HSN */
		{
			case 0x10:
				if(hsn_flag[0] == 0)  /* ��һ���յ�hsn */
				{
					hsn[0] = package.Hsn;
					hsn_flag[0] = 1;
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_delete_HSN(link_queue, 0, hsn[0], 1);
					pthread_mutex_unlock(&mutex_queue[0]);
				}

				else
				{
					if(hsn[0] == package.Hsn)  /* �ظ��յ���hsn */
					{
						pthread_mutex_lock(&mutex_queue[0]);
						hm_queue_delete_HSN(link_queue, 0, hsn[0], 2);
						pthread_mutex_unlock(&mutex_queue[0]);
					}
					else
					{
						hsn[0] = package.Hsn;  /* ��һ���յ���hsn */
						pthread_mutex_lock(&mutex_queue[0]);
						hm_queue_delete_HSN(link_queue, 0, hsn[0], 1);
						pthread_mutex_unlock(&mutex_queue[0]);
					}
				}				
				break;

			case 0x20:
				if(hsn_flag[1] == 0)  /* ��һ���յ�hsn */
				{
					hsn[1] = package.Hsn;
					hsn_flag[1] = 1;
					pthread_mutex_lock(&mutex_queue[1]);
					hm_queue_delete_HSN(link_queue, 1, hsn[1], 1);
					pthread_mutex_unlock(&mutex_queue[1]);
				}

				else
				{
					if(hsn[1] == package.Hsn)  /* �ظ��յ���hsn */
					{
						pthread_mutex_lock(&mutex_queue[1]);
						hm_queue_delete_HSN(link_queue, 1, hsn[1], 2);
						pthread_mutex_unlock(&mutex_queue[1]);
					}
					else
					{
						hsn[1] = package.Hsn;  /* ��һ���յ���hsn */
						pthread_mutex_lock(&mutex_queue[1]);
						hm_queue_delete_HSN(link_queue, 1, hsn[1], 1);
						pthread_mutex_unlock(&mutex_queue[1]);
					}
				}		
				break;

			case 0x30:
				if(hsn_flag[2] == 0)  /* ��һ���յ�hsn */
				{
					hsn[2] = package.Hsn;
					hsn_flag[2] = 1;
					pthread_mutex_lock(&mutex_queue[2]);
					hm_queue_delete_HSN(link_queue, 2, hsn[2], 1);
					pthread_mutex_unlock(&mutex_queue[2]);
				}

				else
				{
					if(hsn[2] == package.Hsn)  /* �ظ��յ���hsn */
					{
						pthread_mutex_lock(&mutex_queue[2]);
						hm_queue_delete_HSN(link_queue, 2, hsn[2], 2);
						pthread_mutex_unlock(&mutex_queue[2]);
					}
					else
					{
						hsn[2] = package.Hsn;  /* ��һ���յ���hsn */
						pthread_mutex_lock(&mutex_queue[2]);
						hm_queue_delete_HSN(link_queue, 2, hsn[2], 1);
						pthread_mutex_unlock(&mutex_queue[2]);
					}
				}		
				break;

			case 0x40:
				if(hsn_flag[3] == 0)  /* ��һ���յ�hsn */
				{
					hsn[3] = package.Hsn;
					hsn_flag[3] = 1;
					pthread_mutex_lock(&mutex_queue[3]);
					hm_queue_delete_HSN(link_queue, 3, hsn[3], 1);
					pthread_mutex_unlock(&mutex_queue[3]);
				}

				else
				{
					if(hsn[3] == package.Hsn)  /* �ظ��յ���hsn */
					{
						pthread_mutex_lock(&mutex_queue[3]);
						hm_queue_delete_HSN(link_queue, 3, hsn[3], 2);
						pthread_mutex_unlock(&mutex_queue[3]);
					}
					else
					{
						hsn[3] = package.Hsn;  /* ��һ���յ���hsn */
						pthread_mutex_lock(&mutex_queue[3]);
						hm_queue_delete_HSN(link_queue, 3, hsn[3], 1);
						pthread_mutex_unlock(&mutex_queue[3]);
					}
				}		
				break;

			case 0x00:
				EPT(stderr, "%s: HighMAC receive 0x00 from LowMAC\n", qinfs[re_qin].pname);
				break;

			default:
				EPT(stderr, "%s: HighMAC receive unknown msg from LowMAC, no=%d\n", qinfs[re_qin].pname, package.type && 0xf0);
				break;
	
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.lm2nl_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}
#endif



int main(int argc, char* argv[])
{	
	int rval, stop, len;
	U8 j;

#ifndef _HM_TEST
	McBSP_fd = open("/dev/McBSP1",O_RDWR);
	if(McBSP_fd < 0)
	{
		printf("main: open McBSP device error\n");
        goto process_return;
	}
#endif

#ifdef _HM_TEST
	if (argc < 2)
	{
		EPT(stderr, "%s: must privide the address of itself node\n", argv[0]);
		rval = 1;
		goto process_return;
	}
	else {
		j = atoi(argv[1]);
	}

	if (j < MADR_UNI_MIN || j > MADR_UNI_MAX)
	{
		EPT(stderr, "%s: must privide the correct node address\n", argv[0]);
		rval = 1;
		goto process_return;
	}
	
	fp = fopen(argv[1], "w");
	localID = j;
#endif


	EPT(stderr, "%s: main thread id = %lu\n", argv[0], pthread_self());

	/* ��ʼ���ź���*/
	rval = hm_sem_init(empty);
	if (rval != 0)
	{
		EPT(stderr, "%s: Semaphore initial failed\n", argv[0]);
		rval = 2;
		goto process_return;
	}

	/* ��ʼ��6���������*/
	hm_queue_init(link_queue);



#ifdef _HM_TEST
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, argv[1]);
#else
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, argv[0]);
#endif
	if (rval != 0)
	{
		EPT(stderr, "%s: can not create getting queue init thread\n", argv[0]);
		rval = 3;
		goto process_return;
	}



	sleep(1);   /* ��֤���ݶ��г�ʼ����� */



#ifdef _HM_TEST
	/* send self qid to mr_test process */
	mmsg_t tx_msg;
	tx_msg.mtype = MMSG_MT_RQID;
	tx_msg.node = j;
	len = sizeof(tx_msg.node);

	*(int *)tx_msg.data = mac_qid;
	len += sizeof(int);
	EPT(stderr, "main: mac_qid = %d\n", mac_qid);
	msgsnd(ht_qid, (void *)&tx_msg, len, 0);
#endif



	signal(SIGINT, hm_qrv_kill);

	/* create receiving msg thread */
	rval = pthread_create(&rcvfr_nl_tid, NULL, hm_rcvfrom_nl_thread, &(qinfs[re_qin].qid));    /* re_qinΪ�����̵����ݶ��б�� */
	if (rval != 0) {
		EPT(stderr, "%s: can not create hm_rcvfrom_nl thread\n", argv[0]);
		rval = 4;
		goto process_return;
	}

	/* create getting input thread */
	rval = pthread_create(&lm2nl_tid, NULL, hm_lm2nl_thread, NULL);
	if (rval != 0) 	{
		EPT(stderr, "%s: can not create lm2nl thread\n", argv[0]);
		rval = 5;
		goto process_return;
	}

	/* �������͵� LowMAC �߳� */
	rval = pthread_create(&sndto_lm_tid, NULL, hm_sendto_lm_thread, &j);
	if (rval != 0) 	{
		EPT(stderr, "%s: can not create hm_sendto_lm thread\n", argv[0]);
		rval = 6;
		goto process_return;
	}

#ifndef _NL_TEST
	/* ����ʱ϶�߳� */
	rval = pthread_create(&slot_tid, NULL, hm_slot_thread, NULL);
	if (rval != 0) 	{
		EPT(stderr, "%s: can not create slot thread\n", argv[0]);
		rval = 7;
		goto process_return;
	}
#endif

	stop = 0;
	pthread_mutex_lock(&share.mutex);
	while(0 == stop)
	{
		EPT(stderr, "main: waiting for the exit of sub threads\n");
		pthread_cond_wait(&share.cond, &share.mutex);
		EPT(stderr, "%s: share.hm_rcvfrom_nl_run = %d, share.lm2nl_run = %d, share.hm_sendto_lm_run = %d, share.slot_run = %d\n", argv[0], share.hm_rcvfrom_nl_run, share.lm2nl_run, share.hm_sendto_lm_run, share.slot_run);
		if (share.hm_rcvfrom_nl_run == 0 || share.lm2nl_run == 0 || share.hm_sendto_lm_run == 0 || share.slot_run == 0)
		{
			if (share.hm_rcvfrom_nl_run == 0) EPT(stderr, "hm_rcvfrom_nl thread quit\n");
			if (share.lm2nl_run == 0) EPT(stderr, "lm2nl_run thread quit\n");
			if (share.hm_sendto_lm_run == 0) EPT(stderr, "hm_sendto_lm thread quit\n");
			if (share.slot_run == 0) EPT(stderr, "slot thread quit\n");
			stop = 1;
			continue;
		}
	}
	EPT(stderr, "%s: certain thread quit\n", argv[0]);
	pthread_mutex_unlock(&share.mutex);
/*
	pthread_join(mrx_tid, &result);
	EPT(stderr, "msg receiving thread ends, return %d\n", *(int *)result);
	pthread_join(gin_tid, &result);
	EPT(stderr, "input catching thread ends, return %d\n", *(int *)result);
*/
process_return:
	sleep(1);
	mr_queues_delete(); /* ���Լӳ����� */
	close(McBSP_fd);
	exit(rval);
}


#ifdef _NL_TEST
int hm_rmsg_ip_proc(U16 len, void *data)  /* len Ϊ data��ʵ�ʳ��� */
{
	mac_packet_t *rmsg = (mac_packet_t *)data;
	int rval = 0;	
	EPT(stderr, "hm_rmsg_ip_proc: len = %d\n", len);
	
	pthread_mutex_lock(&mutex_queue[3]);
	hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* �͵� TQ_3 ���� */
	pthread_mutex_unlock(&mutex_queue[3]);
			
	return rval;
}
#else
int hm_rmsg_ip_proc(U16 len, void *data)  /* len Ϊ data��ʵ�ʳ��� */
{
	mac_packet_t *rmsg = (mac_packet_t *)data;
	int rval = 0;

	switch(rmsg->subt)
	{
		case 0:    /* ��ʱ��ȱ */

 			break;

		case 1:    /* ����ҵ������ 0x03 */
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* �͵� TQ_3 ���� */
			pthread_mutex_unlock(&mutex_queue[3]);
			break;

		case 2:    /* ��Ƶҵ������ 0x01 */
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_enter(link_queue, 1, len, (char *)rmsg, HL_VO_DATA);    /* �͵� TQ_1 ���� */
			pthread_mutex_unlock(&mutex_queue[1]);
			break;

		case 3:    /* ��Ƶҵ������ 0x02 */
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_enter(link_queue, 2, len, (char *)rmsg, HL_VD_DATA);    /* �͵� TQ_2 ���� */
			pthread_mutex_unlock(&mutex_queue[2]);
			break;

		case 4:    /* ����ҵ������ 0x03 */
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* �͵� TQ_3 ���� */
			pthread_mutex_unlock(&mutex_queue[3]);
			break;

		default:
			EPT(stderr, "hm_rmsg_ip_proc: highmac receive unknown mp msg, no=%d\n", rmsg->subt);
			break;
	}

	return rval;
}
#endif

int hm_rmsg_ft_proc(U16 len, void *data)    /* ·�ɱ�  0x06 */
{
	char *rmsg = (char *)data;
	int rval = 0;

	pthread_mutex_lock(&mutex_queue[4]);
	hm_queue_enter(link_queue, 4, len, (char *)rmsg, HL_FT_DATA);    /* �͵� TQ_4 ���� */
	pthread_mutex_unlock(&mutex_queue[4]);

	return rval;
}

int hm_rmsg_rp_proc(U16 len, void *data)    /* ·��Э������  0x00 */
{
	char *rmsg = (char *)data;
	int rval = 0;

	pthread_mutex_lock(&mutex_queue[0]);
	hm_queue_enter(link_queue, 0, len, (char *)rmsg, HL_RP_DATA);    /* �͵� TQ_0 ���� */
	pthread_mutex_unlock(&mutex_queue[0]);

	return rval;
}

int hm_rmsg_proc(U16 len, void * data)
{
	mmsg_t *rmsg = (mmsg_t *)data;
	int rval = 0;

	//EPT(stderr, "hm_rmsg_proc: highmac receive a msg from netlayer, no=%ld\n", rmsg->mtype);

	switch(rmsg->mtype)
	{
		case MMSG_IP_DATA:    /* ҵ������   0x01 02 03 */
			hm_rmsg_ip_proc(len-1, rmsg->data);    /* �����ݴ�mmsg_t�ṹ���а��� */
			break;

		case MMSG_FT_DATA:    /* ·�ɱ�   0X06 */
			hm_rmsg_ft_proc(len-1, rmsg->data);
			break;

		case MMSG_RPM:    /* ·��Э������  0x00 */
			hm_rmsg_rp_proc(len-1, rmsg->data);    
			break;

		case MMSG_MAODV:    /* �鲥·��Э������  0x00 */
			hm_rmsg_rp_proc(len-1, rmsg->data);    
			break;

		default:
			EPT(stderr, "hm_rmsg_proc: highmac receive unknown msg from netlayer, no=%ld\n", rmsg->mtype);
			//rval=1;
			break;
	}

	return rval;
}

int hm_smsg_proc(mmsg_t *msg, long type, U8 src, U16 len, char *data)
{
	int rval = 0;
	msg->mtype = type;
	msg->node = src;
	memcpy(msg->data, data, len);
	rval = msgsnd(qinfs[0].qid, (void *)msg, len + 1, 0);

	if (rval != 0)
	{
		EPT(stderr, "hm_smsg_proc: write msg failed,errno = %d[%s]\n", errno, strerror(errno));
		rval = 1;
	}
	return rval;
}

int hm_management(U16 len, char *data)
{}


