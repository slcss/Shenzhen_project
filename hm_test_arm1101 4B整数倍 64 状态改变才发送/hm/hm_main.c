#include "../mr_common.h"
#include "hm_common.h"
#include "hm_timer.h"
#include "hm_queue_manage.h"
#include "hm_with_lowmac.h"
#include "hm_slot.h"

extern qinfo_t qinfs[];
extern const int cnt_p;

extern int  qs, re_qin, nl_qid, hm_qid, vi_qid, rp_qid, ht_qid, mac_qid;

/* 队列链表指针 */
extern link_queue_t link_queue[];
extern link_queue_t lm2nl_link_queue;

/* 信号量 */
extern sem_t  empty[];

/* slot 和 lm2nl 两个线程交互数据的两个缓存 */
extern lm_packet_t slot_cache1;
extern lm_packet_t slot_cache2;

/* 两个缓存是否可以读取的标志 0表示不可读可写 1表示可读不可写
slot负责读 lm2nl负责写 */
extern int slot_cache1_flag;
extern int slot_cache2_flag;

/* 互斥量，条件变量 */
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



/* 本地节点ID */
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

	pthread_detach(pthread_self());		 /*修改状态为unjoinable，线程结束自动释放资源*/

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

		/* 释放信号量 */
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
	U8           LSN = 0;    /* 记录LSN */
	U8  i = 0;
	U16          type = 0;   /* 用于区分出组播信息 */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* 不同的错误类型 */
	stop = 0;  /* 判断循环的标志位 */
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

		/* 从test程序接收数据 */
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
		/* 检验收到的LSN是否正确 */
		if(package.Lsn == LSN)
		{
			LSN++;
			if(LSN == 16)
				LSN = 0;
		}
		else
			EPT(stderr, "%s: HighMAC receive a wrong lm LSN, LSN = %d\n", qinfs[re_qin].pname, package.Lsn);
#endif

		/* 分类处理 */
		switch(package.type)
		{
			case 0x00:     /* 路由协议，MAC协议数据 */
				switch(((mac_packet_t *)package.data)->type)
				{
					case 0:     /* 路由协议(其中包含组播数据) */
						memcpy(&type, ((mac_packet_t *)package.data)->data, 2);   /* 读取data内的类型 */
						if(type == MMSG_MAODV)   /* 是组播数据 */
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_MAODV, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 为data的真实长度 */
							if(rval != 0)
								goto thread_return;
							break;
						}
						else
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_RPM, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 为data的真实长度 */
							if(rval != 0)
								goto thread_return;
							break;
						}	

					case 1:    /* MAC协议数据 (动态时隙相关帧)    暂时考虑两个缓存区  1缓存区不可用再用2     subt:0001 -> 1111 都可以扩展 */
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

			case 0x01:     /* 语音业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x02:     /* 视频业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x03:     /* 其他业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x06:     /* 转发表 */
				hm_management(package.len-4, package.data);
				break;

			case 0x07:     /* 时隙表 */
				hm_management(package.len-4, package.data);
				break;

			case 0x08:     /* 勤务帧   暂时考虑两个缓存区 */
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

			case 0x0A: 	/* 纯流控帧 */
				hm_nARQ(&package);
				break;

			case 0xff: 	/* 超帧结束反馈帧   暂时考虑两个缓存区   类型采用0xff */
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
	U8           LSN = 0;    /* 记录LSN */
	U8  i = 0;
	U16          type = 0;   /* 用于区分出组播信息 */

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* 不同的错误类型 */
	stop = 0;  /* 判断循环的标志位 */
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

		/* 从nl_test中接收数据 */
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
		/* 检验收到的LSN是否正确 */
		if(package.Lsn == LSN)
		{
			LSN++;
			if(LSN == 16)
				LSN = 0;
		}
		else
			EPT(stderr, "%s: HighMAC receive a wrong lm LSN, LSN = %d\n", qinfs[re_qin].pname, package.Lsn);
#endif

		/* 分类处理 */
		//EPT(stderr, "hm_lm2nl_thread: type = %d\n", package.type);
		switch(package.type)
		{
			case 0x00:     /* 路由协议，MAC协议数据 */
				switch(((mac_packet_t *)package.data)->type)
				{
					case 0:     /* 路由协议(其中包含组播数据) */
						memcpy(&type, ((mac_packet_t *)package.data)->data, 2);   /* 读取data内的类型 */
						if(type == MMSG_MAODV)   /* 是组播数据 */
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_MAODV, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 为data的真实长度 */
							if(rval != 0)
								goto thread_return;
							break;
						}
						else
						{
							rval = hm_smsg_proc(&tx_msg, MMSG_RPM, ((mac_packet_t *)package.data)->src, package.len-4, package.data);  /* package.len-4 为data的真实长度 */
							if(rval != 0)
								goto thread_return;
							break;
						}	

					case 1:    /* MAC协议数据 (动态时隙相关帧)    暂时考虑两个缓存区  1缓存区不可用再用2     subt:0001 -> 1111 都可以扩展 */
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

			case 0x01:     /* 语音业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x02:     /* 视频业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x03:     /* 其他业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case 0x06:     /* 转发表 */
				hm_management(package.len-4, package.data);
				break;

			case 0x07:     /* 时隙表 */
				hm_management(package.len-4, package.data);
				break;

			case 0x08:     /* 勤务帧   暂时考虑两个缓存区 */
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

			case 0x0A: 	/* 纯流控帧 */
				hm_nARQ(&package);
				break;

			case 0xff: 	/* 超帧结束反馈帧   暂时考虑两个缓存区   类型采用0xff */
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
	char         rcv_buf[512];
	lm_packet_t  *package;
	package = (lm_packet_t  *)rcv_buf;
	int 		 stop, rval;
	//U8           LSN = 0;    /* 记录LSN */
	U16          type = 0;   /* 用于区分出组播信息 */

	U8           hsn[4] = {0,0,0,0};  /* 记录4个队列的最前hsn */
	U8           hsn_flag[4] = {0,0,0,0};  /* 标记第一次接收到4个队列的hsn */

	/* 用于修正数据结构 12.18 */
	U8 temp_data[1024] = {0};
	U16 temp_len = 0;

	pthread_detach(pthread_self());
	EPT(stderr, "%s: LowMAC send to netlayer thread id = %lu\n", qinfs[re_qin].pname, pthread_self());

	rval = 0;  /* 不同的错误类型 */
	stop = 0;  /* 判断循环的标志位 */
	while(0 == stop)
	{
		memset(&tx_msg, 0, sizeof(tx_msg));
		memset(rcv_buf, 0, sizeof(rcv_buf));

		/* judge msg receiving thread alive or not */
		if (ESRCH == pthread_kill(rcvfr_nl_tid, 0))
		{
			EPT(stderr, "hm_lm2nl_thread: hm_rcvfrom_nl thread has ended.\n");
			rval = 1;
			stop = 1;
			goto thread_return;
		}

		hm_readfm_McBSP(rcv_buf);		
		package->len = ntohs(package->len); 
		EPT(stderr, "hm_lm2nl_thread: package->len = %x\n", package->len);
		
		//EPT(stderr, "hm_lm2nl_thread: HighMAC receive a LowMAC msg, type = %x\n", package->type);
		//EPT(stderr, "hm_lm2nl_thread: HighMAC receive a LowMAC Lsn, Lsn = %d\n", package->Lsn);

#if 1		
		/* 检验收到的LSN是否正确 */
		if(package->Lsn == LSN)    
		{
			LSN++;
			if(LSN >= 16)   /* LSN之前的数据可以删掉 */
				LSN = 0;
		}
		else
		{
			EPT(stderr, "hm_lm2nl_thread: HighMAC receive a wrong lm LSN, LSN = %d\n", package->Lsn);

			/* LSN出错修改 1.24 */
			LSN = package->Lsn+1;
			if(LSN >= 16)   /* LSN之前的数据可以删掉 */
				LSN = 0;
		}
#endif

		/* 分类处理 */		
		switch(package->type & 0x0f)
		{
			case LH_RPMP_DATA:     /* 一切外发的数据，包括路由协议数据，MAC协议数据(包含勤务帧)，业务数据 */
				switch(((mac_packet_t *)package->data)->type)
				{
					/* 数据帧 */
					case 0:     
						switch(((mac_packet_t *)package->data)->subt)
						{
							case 0:  /* 路由协议(其中包含组播数据) */
								EPT(stderr, "hm_lm2nl_thread: Route data\n");							
																
								memcpy(temp_data, package->data, 8);
								temp_len = *(U16 *)(package->data+8);
								memcpy(temp_data+8, package->data+10, temp_len);
								
								rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package->data)->src, temp_len+8, temp_data);  /* package.len-4 为data的真实长度加填充的长度，temp_len才是真实长度 1.21 */
								if(rval != 0)
									goto thread_return;								
								break;
								
							case 1:	 /* 数据业务 */
								EPT(stderr, "hm_lm2nl_thread: Business1 data\n");
								
								memcpy(temp_data, package->data, 8);
								temp_len = *(U16 *)(package->data+8);
								memcpy(temp_data+8, package->data+10, temp_len);
									
								rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package->data)->src, temp_len+8, temp_data);
								if(rval != 0)
									goto thread_return;
								break;

							case 2:  /* 语音业务 */
								EPT(stderr, "hm_lm2nl_thread: Business2 data\n");								
								
								memcpy(temp_data, package->data, 8);
								temp_len = *(U16 *)(package->data+8);
								memcpy(temp_data+8, package->data+10, temp_len);
								
								rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package->data)->src, temp_len+8, temp_data);
								if(rval != 0)
									goto thread_return;
								break;

							case 3:  /* 视频业务 */	
								EPT(stderr, "hm_lm2nl_thread: Business3 data\n");
								
								memcpy(temp_data, package->data, 8);
								temp_len = *(U16 *)(package->data+8);
								memcpy(temp_data+8, package->data+10, temp_len);
								
								rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package->data)->src, temp_len+8, temp_data);
								if(rval != 0)
									goto thread_return;
								break;

							case 4:  /* 其他数据业务 */
								EPT(stderr, "hm_lm2nl_thread: Business4 data\n");
								
								memcpy(temp_data, package->data, 8);
								temp_len = *(U16 *)(package->data+8);
								memcpy(temp_data+8, package->data+10, temp_len);
								
								rval = hm_smsg_proc(&tx_msg, MMSG_MP_DATA, ((mac_packet_t *)package->data)->src, temp_len+8, temp_data);
								if(rval != 0)
									goto thread_return;
								break;

							default:								
								EPT(stderr, "hm_lm2nl_thread: HighMAC receive unknown Route&Business data from LowMAC , type = %d\n", ((mac_packet_t *)package->data)->subt);
								break;
						}
						break;						
						
					/* 控制帧 */
					case 1:    
						switch(((mac_packet_t *)package->data)->subt)
						{
							case 0:  /* 勤务帧，类型采用0x00 */
								EPT(stderr, "hm_lm2nl_thread: sf data\n");
								if(slot_cache1_flag == 0)
								{
									memset(&slot_cache1, 0, sizeof(slot_cache1));
									memcpy(&slot_cache1, package, package->len);
									slot_cache1.type = ((mac_packet_t *)package->data)->subt;
									slot_cache1_flag = 1;
							        sem_post(&(empty[1]));
								}
								else
								{
									memset(&slot_cache2, 0, sizeof(slot_cache2));
									memcpy(&slot_cache2, package, package->len);
									slot_cache2.type = ((mac_packet_t *)package->data)->subt;
									slot_cache2_flag = 1;
								}
								break;

							case 2:  /* 时隙预约请求帧，类型采用0x02 */	
								EPT(stderr, "hm_lm2nl_thread: slot ask data\n");
								if(slot_cache1_flag == 0)
								{
									memset(&slot_cache1, 0, sizeof(slot_cache1));
									memcpy(&slot_cache1, package, package->len);
									slot_cache1.type = ((mac_packet_t *)package->data)->subt;
									slot_cache1_flag = 1;
					            	sem_post(&(empty[1]));
								}
								else
								{
									memset(&slot_cache2, 0, sizeof(slot_cache2));
									memcpy(&slot_cache2, package, package->len);
									slot_cache2.type = ((mac_packet_t *)package->data)->subt;
									slot_cache2_flag = 1;
								}
								break;

							case 3:  /* 时隙预约响应帧，类型采用0x03 */
								EPT(stderr, "hm_lm2nl_thread: slot resp data\n");
								if(slot_cache1_flag == 0)
								{
									memset(&slot_cache1, 0, sizeof(slot_cache1));
									memcpy(&slot_cache1, package, package->len);
									slot_cache1.type = ((mac_packet_t *)package->data)->subt;
									slot_cache1_flag = 1;
					            	sem_post(&(empty[1]));
								}
								else
								{
									memset(&slot_cache2, 0, sizeof(slot_cache2));
									memcpy(&slot_cache2, package, package->len);
									slot_cache2.type = ((mac_packet_t *)package->data)->subt;
									slot_cache2_flag = 1;
								}
								break;

							case 4:  /* 时隙预约确认帧，类型采用0x04 */
								EPT(stderr, "hm_lm2nl_thread: slot conf data\n");
								if(slot_cache1_flag == 0)
								{
									memset(&slot_cache1, 0, sizeof(slot_cache1));
									memcpy(&slot_cache1, package, package->len);
									slot_cache1.type = ((mac_packet_t *)package->data)->subt;
									slot_cache1_flag = 1;
					            	sem_post(&(empty[1]));
								}
								else
								{
									memset(&slot_cache2, 0, sizeof(slot_cache2));
									memcpy(&slot_cache2, package, package->len);
									slot_cache2.type = ((mac_packet_t *)package->data)->subt;
									slot_cache2_flag = 1;
								}
								break;
								
							default:								
								EPT(stderr, "hm_lm2nl_thread: HighMAC receive unknown MAC data from LowMAC , type = %d\n", ((mac_packet_t *)package->data)->subt);
								break;
						}
						break;

					default:
						EPT(stderr, "hm_lm2nl_thread: HighMAC receive unknown MAC frame from LowMAC , type = %d\n", ((mac_packet_t *)package->data)->type);
						break;
				}
				break;
#if 0
			case LH_VO_DATA:     /* 语音业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case LH_VD_DATA:     /* 视频业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;

			case LH_OT_DATA:     /* 其他业务数据 */
				rval = hm_smsg_proc(&tx_msg, MMSG_IP_DATA, ((mac_packet_t *)package.data)->src, package.len-4, package.data);
				if(rval != 0)
					goto thread_return;
				break;
#endif
			case LH_FT_DATA:     /* 转发表反馈 */
				EPT(stderr, "hm_lm2nl_thread: LH_FT_DATA\n");
				hm_management(package->len-4, package->data, LH_FC_DATA);
				break;

			case LH_ST_DATA:     /* 时隙表反馈 */
				EPT(stderr, "hm_lm2nl_thread: LH_ST_DATA\n");
				hm_management(package->len-4, package->data, LH_ST_DATA);
				break;

			case LH_SF_DATA:     /* 勤务帧反馈 */
				EPT(stderr, "hm_lm2nl_thread: LH_SF_DATA\n");
				hm_management(package->len-4, package->data, LH_SF_DATA);
				break;				

			case LH_BBRF_DATA: 	/* BB RF反馈 */
				EPT(stderr, "hm_lm2nl_thread: LH_BBRF_DATA\n");
				hm_management(package->len-4, package->data, LH_BBRF_DATA);
				break;

			case LH_FC_DATA: 	/* 纯流控帧反馈 */
				EPT(stderr, "hm_lm2nl_thread: LH_FC_DATA\n");
				hm_nARQ(package);
				break;		

			case LH_FF_DATA: 	/* 超帧结束反馈帧，暂时考虑两个缓存区，类型采用0x0f */
				EPT(stderr, "hm_lm2nl_thread: LH_FF_DATA\n");
				if(slot_cache1_flag == 0)
				{
					memset(&slot_cache1, 0, sizeof(slot_cache1));
					memcpy(&slot_cache1, package, package->len);
					slot_cache1_flag = 1;
			        sem_post(&(empty[1]));
				}
				else
				{
					memset(&slot_cache2, 0, sizeof(slot_cache2));
					memcpy(&slot_cache2, package, package->len);
					slot_cache2_flag = 1;
				}
				break;	

			default:
				EPT(stderr, "hm_lm2nl_thread: HighMAC receive unknown LowMAC msg, type = %d\n", package->type);				
				break;
		}

#if 0							
		switch(package->type & 0xf0)  /* 查看是哪个队列的反馈HSN */
		{
			case 0x10:
				if(hsn_flag[0] == 0)  /* 第一次收到hsn */
				{
					hsn[0] = package->Hsn;
					hsn_flag[0] = 1;
					pthread_mutex_lock(&mutex_queue[0]);
					hm_queue_delete_HSN(link_queue, 0, hsn[0], 1);
					pthread_mutex_unlock(&mutex_queue[0]);
				}

				else
				{
					if(hsn[0] == package->Hsn)  /* 重复收到此hsn */
					{
						pthread_mutex_lock(&mutex_queue[0]);
						hm_queue_delete_HSN(link_queue, 0, hsn[0], 2);
						pthread_mutex_unlock(&mutex_queue[0]);
					}
					else
					{
						hsn[0] = package->Hsn;  /* 第一次收到此hsn */
						pthread_mutex_lock(&mutex_queue[0]);
						hm_queue_delete_HSN(link_queue, 0, hsn[0], 1);
						pthread_mutex_unlock(&mutex_queue[0]);
					}
				}				
				break;

			case 0x20:
				if(hsn_flag[1] == 0)  /* 第一次收到hsn */
				{
					hsn[1] = package->Hsn;
					hsn_flag[1] = 1;
					pthread_mutex_lock(&mutex_queue[1]);
					hm_queue_delete_HSN(link_queue, 1, hsn[1], 1);
					pthread_mutex_unlock(&mutex_queue[1]);
				}

				else
				{
					if(hsn[1] == package->Hsn)  /* 重复收到此hsn */
					{
						pthread_mutex_lock(&mutex_queue[1]);
						hm_queue_delete_HSN(link_queue, 1, hsn[1], 2);
						pthread_mutex_unlock(&mutex_queue[1]);
					}
					else
					{
						hsn[1] = package->Hsn;  /* 第一次收到此hsn */
						pthread_mutex_lock(&mutex_queue[1]);
						hm_queue_delete_HSN(link_queue, 1, hsn[1], 1);
						pthread_mutex_unlock(&mutex_queue[1]);
					}
				}		
				break;

			case 0x30:
				if(hsn_flag[2] == 0)  /* 第一次收到hsn */
				{
					hsn[2] = package->Hsn;
					hsn_flag[2] = 1;
					pthread_mutex_lock(&mutex_queue[2]);
					hm_queue_delete_HSN(link_queue, 2, hsn[2], 1);
					pthread_mutex_unlock(&mutex_queue[2]);
				}

				else
				{
					if(hsn[2] == package->Hsn)  /* 重复收到此hsn */
					{
						pthread_mutex_lock(&mutex_queue[2]);
						hm_queue_delete_HSN(link_queue, 2, hsn[2], 2);
						pthread_mutex_unlock(&mutex_queue[2]);
					}
					else
					{
						hsn[2] = package->Hsn;  /* 第一次收到此hsn */
						pthread_mutex_lock(&mutex_queue[2]);
						hm_queue_delete_HSN(link_queue, 2, hsn[2], 1);
						pthread_mutex_unlock(&mutex_queue[2]);
					}
				}		
				break;

			case 0x40:
				if(hsn_flag[3] == 0)  /* 第一次收到hsn */
				{
					hsn[3] = package->Hsn;
					hsn_flag[3] = 1;
					pthread_mutex_lock(&mutex_queue[3]);
					hm_queue_delete_HSN(link_queue, 3, hsn[3], 1);
					pthread_mutex_unlock(&mutex_queue[3]);
				}

				else
				{
					if(hsn[3] == package->Hsn)  /* 重复收到此hsn */
					{
						pthread_mutex_lock(&mutex_queue[3]);
						hm_queue_delete_HSN(link_queue, 3, hsn[3], 2);
						pthread_mutex_unlock(&mutex_queue[3]);
					}
					else
					{
						hsn[3] = package->Hsn;  /* 第一次收到此hsn */
						pthread_mutex_lock(&mutex_queue[3]);
						hm_queue_delete_HSN(link_queue, 3, hsn[3], 1);
						pthread_mutex_unlock(&mutex_queue[3]);
					}
				}		
				break;

			case 0x00:
				//EPT(stderr, "hm_lm2nl_thread: HighMAC receive 0x00 queue HSN from LowMAC\n");
				break;

			default:
				EPT(stderr, "hm_lm2nl_thread: HighMAC receive unknown queue HSN from LowMAC, type = %d\n", package->type && 0xf0);
				break;	
		}
#endif
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

	McBSP_fd = open("/dev/McBSP1",O_RDWR);
	if(McBSP_fd < 0)
	{
		printf("main: open McBSP device error\n");
        exit(1);
	}

#if 1
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

	/* 初始化信号量*/
	rval = hm_sem_init(empty);
	if (rval != 0)
	{
		EPT(stderr, "%s: Semaphore initial failed\n", argv[0]);
		rval = 2;
		goto process_return;
	}

	/* 初始化6个链表队列*/
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



	sleep(1);   /* 保证数据队列初始化完成 */



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
	rval = pthread_create(&rcvfr_nl_tid, NULL, hm_rcvfrom_nl_thread, &(qinfs[re_qin].qid));    /* re_qin为本进程的数据队列标号 */
	if (rval != 0) {
		EPT(stderr, "%s: can not create hm_rcvfrom_nl thread\n", argv[0]);
		rval = 4;
		goto process_return;
	}


#ifdef _LM2NL_BEGIN
	/* create getting input thread */
	rval = pthread_create(&lm2nl_tid, NULL, hm_lm2nl_thread, NULL);
	if (rval != 0) 	{
		EPT(stderr, "%s: can not create lm2nl thread\n", argv[0]);
		rval = 5;
		goto process_return;
	}
#endif

	/* 创建发送到 LowMAC 线程 */
	rval = pthread_create(&sndto_lm_tid, NULL, hm_sendto_lm_thread, &j);
	if (rval != 0) 	{
		EPT(stderr, "%s: can not create hm_sendto_lm thread\n", argv[0]);
		rval = 6;
		goto process_return;
	}

#ifndef _NL_TEST
	/* 创建时隙线程 */
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
	mr_queues_delete(); /* 可以加出错处理 */
	close(McBSP_fd);
	exit(rval);
}


#ifdef _NL_TEST
int hm_rmsg_ip_proc(U16 len, void *data)  /* len 为 data的实际长度 */
{
	mac_packet_t *rmsg = (mac_packet_t *)data;
	int rval = 0;	
	EPT(stderr, "hm_rmsg_ip_proc: len = %d\n", len);
	
	pthread_mutex_lock(&mutex_queue[3]);
	hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* 送到 TQ_3 队列 */
	pthread_mutex_unlock(&mutex_queue[3]);
			
	return rval;
}
#else
int hm_rmsg_ip_proc(U16 len, void *data)  /* len 为 data的实际长度 */
{
	mac_packet_t *rmsg = (mac_packet_t *)data;
	int rval = 0;

	switch(rmsg->subt)
	{
		case 0:    /* 暂时空缺 */
			EPT(stderr, "hm_rmsg_ip_proc: 0000\n");

 			break;

		case 1:    /* 其他业务数据 0x03 */
			EPT(stderr, "hm_rmsg_ip_proc: 1111\n");
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* 送到 TQ_3 队列 */
			pthread_mutex_unlock(&mutex_queue[3]);
			break;

		case 2:    /* 音频业务数据 0x01 */
			EPT(stderr, "hm_rmsg_ip_proc: 2222\n");
			pthread_mutex_lock(&mutex_queue[1]);
			hm_queue_enter(link_queue, 1, len, (char *)rmsg, HL_VO_DATA);    /* 送到 TQ_1 队列 */
			pthread_mutex_unlock(&mutex_queue[1]);
			break;

		case 3:    /* 视频业务数据 0x02 */
			EPT(stderr, "hm_rmsg_ip_proc: 3333\n");
			pthread_mutex_lock(&mutex_queue[2]);
			hm_queue_enter(link_queue, 2, len, (char *)rmsg, HL_VD_DATA);    /* 送到 TQ_2 队列 */
			pthread_mutex_unlock(&mutex_queue[2]);
			break;

		case 4:    /* 其他业务数据 0x03 */
			EPT(stderr, "hm_rmsg_ip_proc: 4444\n");
			pthread_mutex_lock(&mutex_queue[3]);
			hm_queue_enter(link_queue, 3, len, (char *)rmsg, HL_OT_DATA);    /* 送到 TQ_3 队列 */
			pthread_mutex_unlock(&mutex_queue[3]);
			break;

		default:
			EPT(stderr, "hm_rmsg_ip_proc: highmac receive unknown mp msg, no=%d\n", rmsg->subt);
			break;
	}

	return rval;
}
#endif

int hm_rmsg_ft_proc(U16 len, void *data)    /* 路由表  0x06 */
{
	char *rmsg = (char *)data;
	int rval = 0;

	pthread_mutex_lock(&mutex_queue[4]);
	hm_queue_enter(link_queue, 4, len, (char *)rmsg, HL_FT_DATA);    /* 送到 TQ_4 队列 */
	pthread_mutex_unlock(&mutex_queue[4]);

	return rval;
}

int hm_rmsg_rp_proc(U16 len, void *data)    /* 路由协议数据  0x00 */
{
	char *rmsg = (char *)data;
	int rval = 0;

	pthread_mutex_lock(&mutex_queue[0]);
	hm_queue_enter(link_queue, 0, len, (char *)rmsg, HL_RP_DATA);    /* 送到 TQ_0 队列 */
	pthread_mutex_unlock(&mutex_queue[0]);

	return rval;
}

int hm_rmsg_proc(U16 len, void * data)
{
	mmsg_t *rmsg = (mmsg_t *)data;
	int i,rval = 0;

	/* 在MAC帧头后加两字节用来表示有效数据长度 12.18 */
	U8 temp[2048] = {0};  /* temp数组用来存储修改后的数据结构 */
	U16 temp_len = 0;
	U16 MACdata_len = 0;

	//EPT(stderr, "hm_rmsg_proc: highmac receive a msg from netlayer, no=%ld\n", rmsg->mtype);

	switch(rmsg->mtype)
	{
		case MMSG_IP_DATA:    /* 业务数据   0x01 02 03 */		
			EPT(stderr, "hm_rmsg_proc: 11111\n");

			/* 将网络层发来的MAC帧数据填充到temp数组中 1.21 */
			memcpy(temp, rmsg->data, 8);
			
			MACdata_len = len - 9;
			memcpy(temp+8, &MACdata_len, 2);			
			
			memcpy(temp+10, rmsg->data+8, len - 9);

			/* temp数组的长度在填充前为len+1，填充后为temp_len 1.21 */
			if((len+1)%4 != 0)			
				temp_len = 4-(len+1)%4+(len+1);			
			else
				temp_len = len+1;		
			EPT(stderr, "hm_rmsg_proc: temp_len = %d\n", temp_len);
			
			hm_rmsg_ip_proc(temp_len, temp);    
			break;

		case MMSG_FT_DATA:    /* 路由表   0X06 */	
			EPT(stderr, "*****hm_rmsg_proc: 22222\n");
			hm_rmsg_ft_proc(len-1-8-4, rmsg->data+8+4);
			break;

		case MMSG_RPM:    /* 路由协议数据  0x00 */
			//EPT(stderr, "hm_rmsg_proc: 33333\n");

			/* 将网络层发来的MAC帧数据填充到temp数组中 1.21 */
			memcpy(temp, rmsg->data, 8);
			
			MACdata_len = len - 9;
			memcpy(temp+8, &MACdata_len, 2);			
			
			memcpy(temp+10, rmsg->data+8, len - 9);

			/* temp数组的长度在填充前为len+1，填充后为temp_len 1.21 */
			if((len+1)%4 != 0)			
				temp_len = 4-(len+1)%4+(len+1);			
			else
				temp_len = len+1;		
			//EPT(stderr, "hm_rmsg_proc: temp_len = %d\n", temp_len);
			
			hm_rmsg_rp_proc(temp_len, temp);    
			break;

		case MMSG_MAODV:    /* 组播路由协议数据  0x00 */
			EPT(stderr, "hm_rmsg_proc: 44444\n");

			/* 将网络层发来的MAC帧数据填充到temp数组中 1.21 */
			memcpy(temp, rmsg->data, 8);
			
			MACdata_len = len - 9;
			memcpy(temp+8, &MACdata_len, 2);			
			
			memcpy(temp+10, rmsg->data+8, len - 9);

			/* temp数组的长度在填充前为len+1，填充后为temp_len 1.21 */
			if((len+1)%4 != 0)			
				temp_len = 4-(len+1)%4+(len+1);			
			else
				temp_len = len+1;		
			EPT(stderr, "hm_rmsg_proc: temp_len = %d\n", temp_len);
			
			hm_rmsg_rp_proc(temp_len, temp);    
			break;

		default:
			EPT(stderr, "hm_rmsg_proc: highmac receive unknown msg from netlayer, no=%ld\n", rmsg->mtype); 
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

int hm_management(U16 len, char *data, U8 type)
{
	LM_neighbor_map_t slottable;
	if(type = LH_ST_DATA)
	{
		memset(&slottable, 0, sizeof(slottable));
		memcpy(&slottable, data, len);
		/* 字序转换 11.18 */
		slottable.slotlen = ntohs(slottable.slotlen);
		printf("slottable.localBS = %d  slotnum = %d  slotlen = %d  dynamic_slot[0] = %d  fixed_slot[0] = %d\n", 
			slottable.localBS, slottable.slotnum, slottable.slotlen, slottable.dynamic_slot[0], slottable.fixed_slot[0]);
	}
}


