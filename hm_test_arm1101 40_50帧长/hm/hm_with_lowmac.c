#include "../mr_common.h"
#include "hm_with_lowmac.h"
#include "hm_queue_manage.h"
#include "hm_common.h"
#include "hm_slot.h"

#ifdef  _HM_TEST
extern int ht_qid;
#endif

#ifdef _NL_TEST
extern pthread_mutex_t NT;
extern lm_packet_t nl_test;

extern sem_t sem_NT;
#endif

/* 消息队列管理信息 */
extern qinfo_t qinfs[];
extern int re_qin;
extern hm_tshare_t  share;

sem_t empty[3];

U8    HSN[6] = {0,0,0,0,0,0};
U8    LSN = 0;
pthread_mutex_t HSN_mute = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t LSN_mute = PTHREAD_MUTEX_INITIALIZER;


extern link_queue_t link_queue[];
extern pthread_mutex_t mutex_queue[];        /* 6个队列的互斥量 */

/* 用于接收流控帧的数据结构 */
lm_flow_ctrl_t queue[QUEUE_NUM-2];

/* 标志LowMAC队列是否满的标志 */
U8 lm_full = 0;

/* 有关LowMAC队列满的触发和恢复 */
pthread_mutex_t lm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  lm_cond = PTHREAD_COND_INITIALIZER;

/* McBSP的接口描述符 */
int McBSP_fd; 

/* 填充长度 11.14 */
int snd_len;
char snd_buf[512];

int hm_sem_init(sem_t *sem_empty)
{
	int rval = 0;
	int i;
	for(i=0; i<3; i++)
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
	for(i=0; i<3; i++)
	{
		sem_destroy(&(sem_empty[i]));
	}

	return 0;
}



#ifdef _HM_TEST
void *hm_sendto_lm_thread(void *arg)
{
	int rval = 0;
	int stop = 0;
	int rcnt,len;
	lm_packet_t package;
	mmsg_t      ht_msg;

	U8 count = 0;  /* 记录TQ_0，TQ_5哪个队列有发送权 */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	while(0 == stop)
	{
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));
		memset(&ht_msg, 0, sizeof(ht_msg));

		/* 如果LowMAC队列满，则暂停发送 */
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);

		if(link_queue[4].front != link_queue[4].rear)     /* 转发表，时隙表，BB，RF */
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
				
				/*(if(errno == EAGAIN)
				{
					printf("snd queue full , blocked\n...clean this queue...\n");
					
					mmsg_t temp_buff;
					while( msgrcv(qid_test[addr1], &temp_buff, snd_len,0,IPC_NOWAIT) != -1 );
				}*/
				EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 1,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
				rval = 1;
				goto thread_return;
			}
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front == link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列不空，勤务帧队列空 */
		{
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
			pthread_mutex_unlock(&mutex_queue[0]);

			ht_msg.mtype = 0;
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
			continue;
		}

		if(link_queue[0].front == link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列空，勤务帧队列不空 */
		{
			pthread_mutex_lock(&mutex_queue[5]);
			hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
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
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列以及勤务帧队列都不空 */
		{
			if(count == 0)
			{
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
				pthread_mutex_unlock(&mutex_queue[0]);

				ht_msg.mtype = 0;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 4,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				count = 1;
				continue;
			}
			else
			{
				pthread_mutex_lock(&mutex_queue[5]);
				hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
				pthread_mutex_unlock(&mutex_queue[5]);

				ht_msg.mtype = 5;
				ht_msg.node = *(U8 *)arg;
				memcpy(ht_msg.data, &package, package.len);
				len = package.len + 1;
				rcnt = msgsnd(ht_qid, &ht_msg, len, 0);
				if (rcnt != 0)
				{
					EPT(stderr, "%s: hm_sendto_lm msgsnd() write msg failed 5,errno = %d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
					rval = 1;
					goto thread_return;
				}
				count = 0;
				continue;
			}
		}

		if(link_queue[1].front != link_queue[1].rear)     /* 语音业务数据 */
		{
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);

			ht_msg.mtype = 1;
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
			continue;
		}

		if(link_queue[2].front != link_queue[2].rear)     /* 视频业务数据 */
		{
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_delete(link_queue, 2, &package);
			pthread_mutex_unlock(&mutex_queue[2]);

			ht_msg.mtype = 2;
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

		if(link_queue[3].front != link_queue[3].rear)     /* 其他业务数据 */
		{
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_delete(link_queue, 3, &package);
			pthread_mutex_unlock(&mutex_queue[3]);

			ht_msg.mtype = 3;
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

#elif _NL_TEST 
void *hm_sendto_lm_thread(void *arg)
{
	int rval = 0;
	int stop = 0;
	lm_packet_t package;

	U8 count = 0;  /* 记录TQ_0，TQ_5哪个队列有发送权 */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	sem_init(&sem_NT, 0, 0);

	while(0 == stop)
	{
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));

		/* 如果LowMAC队列满，则暂停发送 */
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);

		if(link_queue[4].front != link_queue[4].rear)     /* 转发表，时隙表，BB，RF */
		{
			pthread_mutex_lock(&mutex_queue[4]);
			hm_queue_delete(link_queue, 4, &package);
			pthread_mutex_unlock(&mutex_queue[4]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front == link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列不空，勤务帧队列空 */
		{
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
			pthread_mutex_unlock(&mutex_queue[0]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front == link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列空，勤务帧队列不空 */
		{
			pthread_mutex_lock(&mutex_queue[5]);
			hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
			pthread_mutex_unlock(&mutex_queue[5]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列以及勤务帧队列都不空 */
		{
			if(count == 0)
			{
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
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
				hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
				pthread_mutex_unlock(&mutex_queue[5]);

				rval = hm_sendto_NT(&package, package.len);
				if(rval != 0)
					goto thread_return;
				count = 0;
				continue;
			}
		}

		if(link_queue[1].front != link_queue[1].rear)     /* 语音业务数据 */
		{
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[2].front != link_queue[2].rear)     /* 视频业务数据 */
		{
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_delete(link_queue, 2, &package);
			pthread_mutex_unlock(&mutex_queue[2]);

			rval = hm_sendto_NT(&package, package.len);
			if(rval != 0)
				goto thread_return;
			continue;
		}

		if(link_queue[3].front != link_queue[3].rear)     /* 其他业务数据 */
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

	U8 count = 0;  /* 记录TQ_0，TQ_5哪个队列有发送权 */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: HighMAC send to LowMAC thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	while(0 == stop)
	{		
		sem_wait(&(empty[0]));
		memset(&package, 0, sizeof(package));		

		/* 如果LowMAC队列满，则暂停发送 */
		pthread_mutex_lock(&lm_mutex);
		if(lm_full == 1)
		{
			pthread_cond_wait(&lm_cond, &lm_mutex);
		}
		pthread_mutex_unlock(&lm_mutex);

		if(link_queue[4].front != link_queue[4].rear)     /* 转发表，时隙表，BB，RF */
		{
			pthread_mutex_lock(&mutex_queue[4]);
			hm_queue_delete(link_queue, 4, &package);
			pthread_mutex_unlock(&mutex_queue[4]);

			printf("hm_sendto_lm_thread: q4 send\n");
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);

			//exit(1);
			
			//if(rval != 0)
				//goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front == link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列不空，勤务帧队列空 */
		{
			pthread_mutex_lock(&mutex_queue[0]);
			hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
			pthread_mutex_unlock(&mutex_queue[0]);

			printf("hm_sendto_lm_thread: q05 send1\n");
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);
			
			continue;
		}

		if(link_queue[0].front == link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列空，勤务帧队列不空 */
		{
			pthread_mutex_lock(&mutex_queue[5]);
			hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
			pthread_mutex_unlock(&mutex_queue[5]);

			printf("hm_sendto_lm_thread: q05 send2\n");
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);
			
			//if(rval != 0)
				//goto thread_return;
			continue;
		}

		if(link_queue[0].front != link_queue[0].rear && link_queue[5].front != link_queue[5].rear)     /* 路由协议，MAC协议数据(不包括勤务帧)队列以及勤务帧队列都不空 */
		{
			printf("hm_sendto_lm_thread: q05 send3\n");
			if(count == 0)
			{
				pthread_mutex_lock(&mutex_queue[0]);
				hm_queue_delete(link_queue, 0, &package);   /* package.len 是 package.data 里有效数据的长度 */
				pthread_mutex_unlock(&mutex_queue[0]);

				/* 填充512字节发送 11.14 */
				memset(snd_buf, 0, sizeof(snd_buf));
				memcpy(snd_buf, &package, snd_len);
				hm_sendto_McBSP(snd_buf, 512);
				
				count = 1;
				continue;
			}
			else
			{
				pthread_mutex_lock(&mutex_queue[5]);
				hm_queue_delete(link_queue, 5, &package);   /* package.len 是 package.data 里有效数据的长度 */
				pthread_mutex_unlock(&mutex_queue[5]);

				/* 填充512字节发送 11.14 */
				memset(snd_buf, 0, sizeof(snd_buf));
				memcpy(snd_buf, &package, snd_len);
				hm_sendto_McBSP(snd_buf, 512);
				
				count = 0;
				continue;
			}
		}

		if(link_queue[1].front != link_queue[1].rear)     /* 语音业务数据 */
		{
			printf("hm_sendto_lm_thread: q1 send\n");
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_delete(link_queue, 1, &package);
			pthread_mutex_unlock(&mutex_queue[1]);
			
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);
			
			continue;
		}

		if(link_queue[2].front != link_queue[2].rear)     /* 视频业务数据 */
		{
			printf("hm_sendto_lm_thread: q2 send\n");
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_delete(link_queue, 2, &package);
			pthread_mutex_unlock(&mutex_queue[2]);
			
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);
			
			continue;
		}

		if(link_queue[3].front != link_queue[3].rear)     /* 其他业务数据 */
		{
			printf("hm_sendto_lm_thread: q3 send\n");
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_delete(link_queue, 3, &package);
			pthread_mutex_unlock(&mutex_queue[3]);
			
			/* 填充512字节发送 11.14 */
			memset(snd_buf, 0, sizeof(snd_buf));
			memcpy(snd_buf, &package, snd_len);
			hm_sendto_McBSP(snd_buf, 512);
			
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



void hm_get_HLsn(lm_packet_t *package, U8 i)
{
	package->Hsn = HSN[i];
	HSN[i]++;
	if(HSN[i] == 16)
		HSN[i] = 0;		

	package->Lsn = LSN;
}

int hm_nARQ(lm_packet_t *package)
{
	U8  i;
	U8  hsn;

	memset(&queue, 0, sizeof(queue));
	memcpy(queue, package->data, package->len-4);
	for(i=0; i<QUEUE_NUM-2; i++)
	{
		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 0)      /* 无操作 */
			continue;

		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 2)      /* 正常反馈HSN */
		{
			hsn = queue[i].HSN;     /* hsn以前的数据均被LowMAC正确接收 */
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 1);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 0 && queue[i].HSN_flag == 1)      /* 队列正常，HSN出错，反馈HSN */
		{
			hsn = queue[i].HSN;     /* hsn以前的数据均被LowMAC正确接收 */
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 2);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 1 && queue[i].HSN_flag == 0)      /* 队列即将满，不用反馈HSN，HighMAC启动动态时隙预约 */
		{
			/* 启动动态时隙预约机制 */
			continue;
		}

		if(queue[i].q_flag == 1 && queue[i].HSN_flag == 1)      /* 队列已满，反馈HSN，HighMAC启动动态时隙预约 */
		{
			/* 启动动态时隙预约机制，停止向下发送 */

			pthread_mutex_lock(&lm_mutex);
			lm_full = 1;
			pthread_mutex_unlock(&lm_mutex);

			hsn = queue[i].HSN;     /* hsn以前的数据均被LowMAC正确接收 */
			pthread_mutex_lock(&mutex_queue[i]);
			hm_queue_delete_HSN(link_queue, i, hsn, 2);
			pthread_mutex_unlock(&mutex_queue[i]);
			continue;
		}

		if(queue[i].q_flag == 2 && queue[i].HSN_flag == 0)      /* 队列恢复正常  恢复正常的机制待考虑 */
		{
			pthread_mutex_lock(&lm_mutex);
			pthread_cond_signal(&lm_cond);
			lm_full = 0;
			pthread_mutex_unlock(&lm_mutex);
			continue;
		}
	}
}

int hm_sendto_McBSP(char *package, U16 len)
{
	write(McBSP_fd, package, len);
}

int hm_readfm_McBSP(char *package)
{
	read(McBSP_fd, package, 512);
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

