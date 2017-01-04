#include "../mr_common.h"
#include "hm_with_lowmac.h"
#include "hm_queue_manage.h"
#include "hm_common.h"
#include "hm_slot.h"
#include "hm_dslot.h"


#ifdef  _HM_TEST
extern int ht_qid;
#endif

#ifdef _NL_TEST
extern pthread_mutex_t NT;
extern lm_packet_t nl_test;

extern sem_t sem_NT;
#endif

/* ��Ϣ���й�����Ϣ */
extern qinfo_t qinfs[];
extern int re_qin;
extern hm_tshare_t  share;

sem_t empty[SEM_NUM];

U8    HSN[6] = {0,0,0,0,0,0};  /* ��¼ÿ�����еĵ�ǰ��HSNֵ 11.11 */
U8    LSN = 0;
pthread_mutex_t HSN_mute = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t LSN_mute = PTHREAD_MUTEX_INITIALIZER;


extern link_queue_t link_queue[];
extern pthread_mutex_t mutex_queue[];        /* 6�����еĻ����� */

/* ���ڽ�������֡�����ݽṹ */
lm_flow_ctrl_t queue[QUEUE_NUM-3];

/* ��־LowMAC�����Ƿ����ı�־ */
U8 lm_full = 0;

/* �й�LowMAC�������Ĵ����ͻָ� */
pthread_mutex_t lm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  lm_cond = PTHREAD_COND_INITIALIZER;

/* McBSP�Ľӿ������� */
int McBSP_fd; 



int hm_sem_init(sem_t *sem_empty)
{
	int rval = 0;
	int i;
	for(i=0; i<SEM_NUM; i++)
	{
		rval = sem_init(&(sem_empty[i]), 0, 0);
		if(rval)
			break;
	}

	return rval;
}

int hm_sem_destory(sem_t *sem_empty)
{
	int i;
	for(i=0; i<SEM_NUM; i++)
	{
		sem_destroy(&(sem_empty[i]));
	}

	return 0;
}



#ifdef _HM_TEST
void *hm_sendto_lm_thread(void *arg)
{
	int rval = 0;
	int stop = 0,stop1 = 0;
	int rcnt,len;
	lm_packet_t package;
	mmsg_t      ht_msg;

	pthread_t busy_queue = -1; /* ���Զ�̬ʱ϶������ҵ������·��߳� 16.12.13 */

	U8 count = 0;  /* ��¼TQ_0��TQ_5�ĸ������з���Ȩ */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = pthread_create(&busy_queue, NULL, hm_sendto_lm_busyqueue_thread, arg);
	if (rval != 0) 	
	{
		EPT(stderr, "%s: hm_sendto_lm_thread can not create hm_sendto_lm_busyqueue_thread\n", qinfs[re_qin].pname);
		rval = 1;
		goto thread_return;
	}

	while(0 == stop1)
	{
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));
		memset(&ht_msg, 0, sizeof(ht_msg));

		/* ���LowMAC������������ͣ���� 
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);*/
		stop = 0;
		while(0 == stop)
		{
			//usleep(4000);
			if(link_queue[4].front != link_queue[4].rear)     /* ״̬�ı�֡��ת������ʱ϶����BB��RF */
			{			
				pthread_mutex_lock(&mutex_queue[4]);
				hm_queue_delete(link_queue, 4, &package);
				pthread_mutex_unlock(&mutex_queue[4]);	

#if 0
				if(package.type == 0xff)
					EPT(stderr, "hm_sendto_lm_thread: 0xff\n");
				else if(package.type == 0x07)
					EPT(stderr, "hm_sendto_lm_thread: 0x07\n");
#endif

				ht_msg.mtype = 4;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{				
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 1,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				//EPT(stderr, "Q4 send\n");
				continue;
			}

			if(link_queue[5].front != link_queue[5].rear)     /* ����֡���� */
			{			
				pthread_mutex_lock(&mutex_queue[5]);
				hm_queue_delete(link_queue, 5, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[5]);			

				ht_msg.mtype = 5;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 3,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				//EPT(stderr, "Q5 send\n");
				continue;
			}		
#if 0
			if(link_queue[0].temp->next != NULL)     /* ·��Э�飬MACЭ������(����������֡) */
			{			
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete_flow_ctrl(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[0]);

				ht_msg.mtype = 1;  //�������0!
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 2,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				EPT(stderr, "Q0 send\n");
				continue;
			}		
#endif
			if(link_queue[0].front != link_queue[0].rear)     /* ·��Э�飬MACЭ������(����������֡) */
			{			
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[0]);

				ht_msg.mtype = 1;  //�������0!
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 2,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				EPT(stderr, "Q0 send\n");
				continue;
			}	
#if 0
			if(link_queue[1].front != link_queue[1].rear)     /* ����ҵ������ */
			{
				pthread_mutex_lock(&mutex_queue[1]);
				hm_queue_delete(link_queue, 1, &package);
				pthread_mutex_unlock(&mutex_queue[1]);

				ht_msg.mtype = 11;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 6,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				//EPT(stderr, "Q1 send\n");
				continue;
			}

			if(link_queue[2].front != link_queue[2].rear)     /* ��Ƶҵ������ */
			{
				pthread_mutex_lock(&mutex_queue[2]);
				hm_queue_delete(link_queue, 2, &package);
				pthread_mutex_unlock(&mutex_queue[2]);

				ht_msg.mtype = 12;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 7,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				continue;
			}

			if(link_queue[3].front != link_queue[3].rear)     /* ����ҵ������ */
			{
				pthread_mutex_lock(&mutex_queue[3]);
				hm_queue_delete(link_queue, 3, &package);
				pthread_mutex_unlock(&mutex_queue[3]);

				ht_msg.mtype = 13;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 8,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				continue;
			}
#endif
			else
			{
				//EPT(stderr, "%s: hm snd to lm wrong\n", qinfs[re_qin].pname);
				stop = 1;
			}	
		}		
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.hm_sendto_lm_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}

#elif _NL_TEST 
void *hm_sendto_lm_thread(void *arg)
{
	int rval = 0;
	int stop = 0;
	lm_packet_t package;

	U8 count = 0;  /* ��¼TQ_0��TQ_5�ĸ������з���Ȩ */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	sem_init(&sem_NT, 0, 0);

	while(0 == stop)
	{
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));

		/* ���LowMAC������������ͣ���� */
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);

		if(link_queue[4].front != link_queue[4].rear)     /* ת������ʱ϶����BB��RF */
		{
			pthread_mutex_lock(&mutex_queue[4]);
			hm_queue_delete(link_queue, 4, &package);
			pthread_mutex_unlock(&mutex_queue[4]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front == link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)���в��գ�����֡���п� */
		{
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_delete(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
			pthread_mutex_unlock(&mutex_queue[0]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front == link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)���пգ�����֡���в��� */
		{
			pthread_mutex_lock(&mutex_queue[5]);
			hm_queue_delete(link_queue, 5, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
			pthread_mutex_unlock(&mutex_queue[5]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)�����Լ�����֡���ж����� */
		{
			if(count == 0)
			{
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[0]);

				rval = hm_sendto_NT(&package, package.len);
				if(rval != 0)
					goto thread_return;
				count = 1;
				continue;
			}
			else
			{
				pthread_mutex_lock(&mutex_queue[5]);
				hm_queue_delete(link_queue, 5, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[5]);

				rval = hm_sendto_NT(&package, package.len);
				if(rval != 0)
					goto thread_return;
				count = 0;
				continue;
			}
		}

		if(link_queue[1].front != link_queue[1].rear)     /* ����ҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[2].front != link_queue[2].rear)     /* ��Ƶҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_delete(link_queue, 2, &package);
			pthread_mutex_unlock(&mutex_queue[2]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[3].front != link_queue[3].rear)     /* ����ҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_delete(link_queue, 3, &package);
			pthread_mutex_unlock(&mutex_queue[3]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}
		else
		{
			EPT(stderr, "%s: hm snd to lm wrong\n", qinfs[re_qin].pname);
			stop = 1;
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.hm_sendto_lm_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}

#else
void *hm_sendto_lm_thread(void *arg)
{
	int rval = 0;
	int stop = 0;
	 
	lm_packet_t package;

	U8 count = 0;  /* ��¼TQ_0��TQ_5�ĸ������з���Ȩ */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	while(0 == stop)
	{
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));

		/* ���LowMAC������������ͣ���� */
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);

		if(link_queue[4].front != link_queue[4].rear)     /* ת������ʱ϶����BB��RF */
		{
			pthread_mutex_lock(&mutex_queue[4]);
			hm_queue_delete(link_queue, 4, &package);
			pthread_mutex_unlock(&mutex_queue[4]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front == link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)���в��գ�����֡���п� */
		{
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_delete_flow_ctrl(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
			pthread_mutex_unlock(&mutex_queue[0]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front == link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)���пգ�����֡���в��� */
		{
			pthread_mutex_lock(&mutex_queue[5]);
			hm_queue_delete(link_queue, 5, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
			pthread_mutex_unlock(&mutex_queue[5]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* ·��Э�飬MACЭ������(����������֡)�����Լ�����֡���ж����� */
		{
			if(count == 0)
			{
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete_flow_ctrl(link_queue, 0, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[0]);

				rval = hm_sendto_McBSP(&package, package.len);
				if(rval != 0)
					goto thread_return;
				count = 1;
				continue;
			}
			else
			{
				pthread_mutex_lock(&mutex_queue[5]);
				hm_queue_delete(link_queue, 5, &package);   /* package.len �� package.data ����Ч���ݵĳ��� */
				pthread_mutex_unlock(&mutex_queue[5]);

				rval = hm_sendto_McBSP(&package, package.len);
				if(rval != 0)
					goto thread_return;
				count = 0;
				continue;
			}
		}

		if(link_queue[1].front != link_queue[1].rear)     /* ����ҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete_flow_ctrl(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[2].front != link_queue[2].rear)     /* ��Ƶҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_delete_flow_ctrl(link_queue, 2, &package);
			pthread_mutex_unlock(&mutex_queue[2]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[3].front != link_queue[3].rear)     /* ����ҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_delete_flow_ctrl(link_queue, 3, &package);
			pthread_mutex_unlock(&mutex_queue[3]);

			rval = hm_sendto_McBSP(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}
		else
		{
			EPT(stderr, "%s: hm snd to lm wrong\n", qinfs[re_qin].pname);
			stop = 1;
		}
	}

thread_return:
	pthread_mutex_lock(&share.mutex);
	share.hm_sendto_lm_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
	EPT(stderr, "%s: LowMAC send to netlayer thread ends\n", qinfs[re_qin].pname);
	sleep(1);
	pthread_exit((void*)&rval);
}
#endif



U8 hm_get_HLsn(lm_packet_t *package, U8 i)
{
	package->Hsn = HSN[i];
	HSN[i]++;
	if(HSN[i] == 16)
		HSN[i] = 0;		

	package->Lsn = LSN;
	return package->Hsn;
}

int hm_nARQ(lm_packet_t *package)
{
	U8  i;
	U8  hsn;
	//EPT(stderr, "receive ARQ!!!!!\n");
	memset(&queue, 0, sizeof(queue));
	memcpy(queue, package->data, package->len-4);
	for(i=0; i<QUEUE_NUM-3; i++)
	{
		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 0)      /* �޲��� */
			continue;
		if(queue[i].HSN < 0 || queue[i].HSN > 15)  /* HSNУ�飬��ֹLowMAC���� */
		{	
			EPT(stderr, "nARQ receive wrong HSN %d from LM\n", queue[i].HSN);
			continue;
		}
		hsn = queue[i].HSN;     /* hsn��ǰ�����ݾ���LowMAC��ȷ���� */
		//EPT(stderr, "i = %d hsn = %d\n", i, hsn);
		pthread_mutex_lock(&mutex_queue[i]);
		hm_queue_delete_HSN(link_queue, i, hsn, 1);
		pthread_mutex_unlock(&mutex_queue[i]);
		break;

#if 0		
		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 0)      /* �޲��� */
			continue;

		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 2)      /* ��������HSN */
		{
			if(queue[i].HSN < 0 || queue[i].HSN > 15)  /* HSNУ�飬��ֹLowMAC���� */
				continue;
			hsn = queue[i].HSN;     /* hsn��ǰ�����ݾ���LowMAC��ȷ���� */
			//EPT(stderr, "i = %d hsn = %d\n", i, hsn);
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 1);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 1)      /* ����������HSN����������HSN */
		{
			if(queue[i].HSN < 0 || queue[i].HSN > 15)
				continue;
			hsn = queue[i].HSN;     /* hsn��ǰ�����ݾ���LowMAC��ȷ���� */
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 2);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 1 && queue[i].HSN_flag == 0)      /* ���м����������÷���HSN��HighMAC������̬ʱ϶ԤԼ */
		{
			/* ������̬ʱ϶ԤԼ���� */
			continue;
		}

		if(queue[i].q_flag == 1 && queue[i].HSN_flag == 1)      /* ��������������HSN��HighMAC������̬ʱ϶ԤԼ */
		{
			/* ������̬ʱ϶ԤԼ���ƣ�ֹͣ���·��� */

			pthread_mutex_lock(&lm_mutex);
			lm_full = 1;
			pthread_mutex_unlock(&lm_mutex);

			if(queue[i].HSN < 0 || queue[i].HSN > 15)
				continue;
			hsn = queue[i].HSN;     /* hsn��ǰ�����ݾ���LowMAC��ȷ���� */
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 2);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 2 && queue[i].HSN_flag == 0)      /* ���лָ�����  �ָ������Ļ��ƴ����� */
		{
			pthread_mutex_lock(&lm_mutex);
			pthread_cond_signal(&lm_cond);
			lm_full = 0;
			pthread_mutex_unlock(&lm_mutex);
			continue;
		}
#endif
	}
}

int hm_sendto_McBSP(lm_packet_t *package, U16 len)
{
#ifndef _HM_TEST
	write(McBSP_fd, package, len);
#endif
}

int hm_readfm_McBSP(lm_packet_t *package)
{
#ifndef _HM_TEST
	read(McBSP_fd, package, 512);
#endif
}


#ifdef _NL_TEST 
int hm_sendto_NT(lm_packet_t *package, U16 len)
{
	pthread_mutex_lock(&NT);
	memset(&nl_test, 0, sizeof(nl_test));
	memcpy(&nl_test, package, len);
	pthread_mutex_unlock(&NT);

	EPT(stderr, "hm_sendto_NT: len = %d\n", len);
	sem_post(&sem_NT);

	usleep(100);
	return 0;
}
#endif

void *hm_sendto_lm_busyqueue_thread(void *arg)
{	
	int time = 0;
	int rcnt,len;
	lm_packet_t package;
	mmsg_t      ht_msg;

	sleep(10);
	EPT(stderr, "ready\n");
	while(1)
	{
		time = 64000/hm_get_slot_num();
		usleep(time);

		if(link_queue[1].front != link_queue[1].rear)     /* ����ҵ������ */
		{
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);

			ht_msg.mtype = 11;
			ht_msg.node = *(U8 *)arg;
			memcpy(ht_msg.data, &package, package.len);
			len = package.len + 1;
			rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
			if (rcnt != 0)
			{
				EPT(stderr, "%s: hm_sendto_lm_busyqueue_thread write msg failed 6,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));								
			}
			//EPT(stderr, "Q1 send\n");			
		}
	}
	
}

