#include "mr_common.h"

#ifdef _HM_TEST
qinfo_t  qinfs[] = {
	{PNAME_NETLAYER, SN_NETPROC, -1, -1},      /* nl_qid */
	{PNAME_HIGHMAC,  SN_HIGHMAC, -1, -1},      /* hm_qid */
	{PNAME_ROUTINGP, SN_ROUTINGP, -1, -1},	   /* rp_qid */
	{PNAME_IF2TCPIP, SN_IF2TCPIP, -1, -1},	   /* vi_qid */
	{PNAME_HM_TEST,  SN_HMTEST, -1, -1},       /* ht_qid */
	{PNAME_H2T,      SN_HIGHMAC, -1, -1}       /* mac_qid */
};
#else
qinfo_t  qinfs[] = {
	{PNAME_NETLAYER, SN_NETPROC, -1, -1},
	{PNAME_HIGHMAC,  SN_HIGHMAC, -1, -1},
	{PNAME_ROUTINGP, SN_ROUTINGP, -1, -1},
	{PNAME_IF2TCPIP, SN_IF2TCPIP, -1, -1},
	{PNAME_HM_TEST,  SN_HMTEST, -1, -1}
};
#endif
const int cnt_p = sizeof(qinfs)/sizeof(qinfs[0]);

int qs = 0;                  //记录 qid 的数量
int re_qin= -1;             //记录当前进程所用数据队列的标号
int nl_qid = -1;
int hm_qid = -1;
int vi_qid = -1;
int rp_qid = -1;
int ht_qid = -1;
int mac_qid = -1;

#ifdef _HM_TEST
void* mr_queues_init(void *arg)
{
	int i,j;
	int qid, rval, stop;
	MADR node = *(MADR *)arg;

//	pthread_detach(pthread_self());

	for (i = 0; i < cnt_p; i++)
	{	
		if (NULL != strstr(qinfs[i].pname, PNAME_H2T)) 
		{
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub + SN_HMTEST + node);		
		}
		else if (NULL != strstr(qinfs[i].pname, PNAME_HM_TEST)) 
		{
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
		}
		
		else if (NULL != strstr(qinfs[i].pname, PNAME_NETLAYER)) 
		{
			//re_qin = i;
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
		}
		else if (NULL != strstr(qinfs[i].pname, PNAME_HIGHMAC)) 
		{
			//re_qin = i;
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
		}
		else if (NULL != strstr(qinfs[i].pname, PNAME_ROUTINGP)) 
		{
			//re_qin = i;
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
		}
		else if (NULL != strstr(qinfs[i].pname, PNAME_IF2TCPIP)) 
		{
			//re_qin = i;
			qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
		}
		re_qin = 1;
	}
	
	if (-1 == re_qin)
	{
		rval = 1;
		EPT(stderr, "%s: can not create the key of myself queue\n", qinfs[re_qin].pname);
		goto thread_return;
	}

	stop = 0;
	while (0 == stop)
	{
		qs = 0;
		for (i = 0; i < cnt_p; i++)
		{	
			if (-1 == qinfs[i].key_q)
				continue;
			qid = msgget(qinfs[i].key_q, IPC_CREAT|QUEUE_MODE);
			if (qid == -1)
				EPT(stderr, "%s: can not get queue for %s\n", qinfs[re_qin].pname, qinfs[i].pname);
			else 
			{
//				EPT(stderr, "process:%s, qid:%d\n", qinfs[i].pname, qid);
				qinfs[i].qid = qid;
				qs += 1;

				/* set the qid */
				if (NULL != strstr(PNAME_HM_TEST, qinfs[i].pname)) 
				{
					ht_qid = qid;
				}
				else if (NULL != strstr(PNAME_H2T, qinfs[i].pname)) 
				{
					mac_qid = qid;
				}
				else if (NULL != strstr(PNAME_IF2TCPIP, qinfs[i].pname)) 
				{
					vi_qid = qid;
				}
				else if (NULL != strstr(PNAME_ROUTINGP, qinfs[i].pname)) 
				{
					rp_qid = qid;
				}
				else if (NULL != strstr(PNAME_HIGHMAC, qinfs[i].pname)) 
				{
					hm_qid = qid;
				}
				else if (NULL != strstr(PNAME_NETLAYER, qinfs[i].pname)) 
				{
					nl_qid = qid;
				}
			}
		}

		if (qs == cnt_p) {
			rval = 0;
			stop = 1;
		}
		else {
			sleep(2);
		}
	}

	EPT(stderr, "exit from finding queue thread.\n");	

thread_return:
	pthread_exit(&rval);
}
#else
void* mr_queues_init(void *arg)
{
	int i,j;
	int qid, rval, stop;
	char *name;

//	pthread_detach(pthread_self());
	name = (char *)arg;

	// ***************************************** 生成key值 *****************************************************
	for (i = 0; i < cnt_p; i++)
	{
		qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);
//		EPT(stderr, "process:%s, key_q:%x\n", qinfs[i].pname, qinfs[i].key_q);
		if (NULL != strstr(name, qinfs[i].pname))
		{
			re_qin = i;    //记录当前进程所用数据队列的标号
		}
	}
	
	if (-1 == re_qin)
	{
		rval = 1;
		EPT(stderr, "%s: can not create the key of myself queue\n", name);
		goto thread_return;
	}

	//*************************************** 生成qid ********************************************************
	stop = 0;
	while (0 == stop)
	{
		qs = 0;    //记录 qid 的数量
		for (i = 0; i < cnt_p; i++)
		{
			qid = msgget(qinfs[i].key_q, IPC_CREAT|QUEUE_MODE);
			if (qid == -1)
				EPT(stderr, "%s: can not get queue for %s\n", name, qinfs[i].pname);
			else 
			{
//				EPT(stderr, "process:%s, qid:%d\n", qinfs[i].pname, qid);
				qinfs[i].qid = qid;
				qs += 1;

				/* set the qid */
				if (NULL != strstr(PNAME_NETLAYER, qinfs[i].pname)) {
					nl_qid = qid;
				}
				else if (NULL != strstr(PNAME_ROUTINGP, qinfs[i].pname)) {
					rp_qid = qid;
				}
				else if (NULL != strstr(PNAME_IF2TCPIP, qinfs[i].pname)) {
					vi_qid = qid;
				}
				else if (NULL != strstr(PNAME_HIGHMAC, qinfs[i].pname)) {
					hm_qid = qid;
				}
			}

			if (i == re_qin) {
				rval = 2;
				stop = 1;
			}
		}

		if (qs == cnt_p) {
			rval = 0;
			stop = 1;
		}
		else {
			sleep(2);
		}
	}

	EPT(stderr, "exit from finding queue thread.\n");	

thread_return:
	pthread_exit(&rval);
	
	//线程的返回值说明
	//没法创建自身的key值返回(void *)1	
	//没法创建4个数据队列返回(void *)2
	//情况正常返回(void *)0  
}
#endif

int mr_queues_delete()
{
	int i;

	for (i = 0; i < cnt_p; i++)
	{
		if (qinfs[i].qid != -1);
			msgctl(qinfs[i].qid, IPC_RMID, NULL);
	}

	return 0;
}

