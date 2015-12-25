#include "mr_common.h"

qinfo_t  qinfs[] = {
	{PNAME_NETLAYER, SN_NETPROC, -1, -1},
	{PNAME_HIGHMAC,  SN_HIGHMAC, -1, -1},
	{PNAME_ROUTINGP, SN_ROUTINGP, -1, -1},
	{PNAME_IF2TCPIP, SN_IF2TCPIP, -1, -1},
	{PNAME_MAODV,    SN_MAODV,    -1, -1}
};//������������key�����ĸ���������Ϣ����id
const int cnt_p = sizeof(qinfs)/sizeof(qinfs[0]);

int qs = 0;								//��¼�������߻�õ���Ϣ��������
int re_qin= -1;							//��¼��������Ԫ���ж�Ӧ������������е��±�
int nl_qid = -1;
int hm_qid = -1;
int vi_qid = -1;
int rp_qid = -1;
int ma_qid = -1;

int mr_queues_init(void *arg)		   //��ȡ������Ϣ���е�id�������Լ��ģ�������Ϣ���в�������͵ش���
{
	int i;
	int qid, rval, stop;				//rval��return_value
	char *name;

	name = (char *)arg;

	for (i = 0; i < cnt_p; i++)			//���������Ϣ���е�key,ʹ�õ�·����Ҫ����
	{
		qinfs[i].key_q = ftok(PATH_CREATE_KEY, qinfs[i].sub);

		if(qinfs[i].key_q < 0)
		{
			EPT(stderr,"get key of queue :%d error\n",i);
		}

		if (NULL != strstr(name, qinfs[i].pname))//�����������Ͷ�������Ӧ
		{
			re_qin = i;					//��¼��������Ԫ���ж�Ӧ������������е��±�
		}
	}

	if (-1 == re_qin)//�������������κζ�����������Ӧ����ֱ�ӽ�����Ϣ���г�ʼ������
	{
		rval = 1;			//�������1
		EPT(stderr, "%s: can not create the key of myself queue\n", name);
		return -1;
	}

	stop = 0;
	while (0 == stop)
	{
		qs = 0;
		for (i = 0; i < cnt_p; i++)
		{
			qid = msgget(qinfs[i].key_q, IPC_CREAT|QUEUE_MODE);
			if (qid == -1)
				EPT(stderr, "%s: can not get queue for %s\n", name, qinfs[i].pname);
			else
			{
				EPT(stderr, "process:%s, qid:%d\n", qinfs[i].pname, qid);
				qinfs[i].qid = qid;
				qs += 1;

				/* set the qid */
				if (NULL != strstr(PNAME_NETLAYER, qinfs[i].pname))
				{
					nl_qid = qid;
				}
				else if (NULL != strstr(PNAME_ROUTINGP, qinfs[i].pname))
				{
					rp_qid = qid;
				}
				else if (NULL != strstr(PNAME_IF2TCPIP, qinfs[i].pname))
				{
					vi_qid = qid;
				}
				else if (NULL != strstr(PNAME_HIGHMAC, qinfs[i].pname))
				{
					hm_qid = qid;
				}
				else if (NULL != strstr(PNAME_MAODV, qinfs[i].pname))
				{
					ma_qid = qid;
				}

			}
		}

		if (qs == cnt_p)   //��ȡid�Ķ���������ƵĶ�����������������˳�
		{
			rval = 0;
			stop = 1;
		}
		else
		{
			sleep(2);
		}
	}

	EPT(stdout, "exit from finding queue thread.\n");

	return 0;
}


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
