#include "nl_timer.h"

//����ֻ��������������㶨ʱ��timer1��timer2�������Զ���32��
tsche_t nl_tsch = {
	0,											    //��һ��Ԫ���Ƕ�ʱ��map
	0,												//�ڶ���Ԫ���Ƕ�ʱ��mask
	{
		{"timer1", 0, 5000 /*ms*/, nl_timer_test1},//�ĸ������ֱ�Ϊ��ʱ�����������ȴ�ʱ�䣬ѭ�����������ָ��
		{"timer2", 0, 3000 /*ms*/, nl_timer_test2}
	}
};

const int nl_tfs = sizeof(nl_tsch.procs)/sizeof(nl_tsch.procs[0]); //��ʱ����������32��

//1s���������500ms
//4.18 wanghao itimerval����Ӧ���Ƕ�ʱ�����������Ӧ�����״��������ӳ�
struct itimerval new_value = { 
{NL_TINTVL_S, NL_TINTVL_US},
{NL_TDELAY_S, NL_TDELAY_US}
};

void nl_tsch_init()
{
	int i, cnt;
	tproc_t *p;
	
	for (i = 0; i < nl_tfs; i++)
	{
		p = &nl_tsch.procs[i];
		if (p->period == 0)
		{
			ASSERT(nl_tsch.procs[i].pf == NULL);		//����ü�ʱ��ѭ������Ϊ0������Ըü�ʱ��ָ����Ϊ��
			continue;
		}
		nl_tsch.tmap = nl_tsch.tmap|(1<<i);				//�Ѹü�������Ӧ��map��־λ��1
		cnt = (p->period * 1000)/NL_TINTVL_US;			//���Ի�׼��ʱ���ļ��500ms���õ��ö�ʱ���Ļ�׼����
		if (cnt < 1)
			p->period = 1;
		else
			p->period = cnt;
		
		p->wait = p->period;
	}
}

int nl_start_timer()
{
	int rval;

	nl_tsch_init();
	
	signal(SIGALRM, nl_timer_sche);
	rval = setitimer(ITIMER_REAL, &new_value, NULL);
	if (-1 == rval)
	{/* failure */
		EPT(stderr, "error occurs in setting timer %d[%s]\n", errno, strerror(errno));
	}
	else
	{/* success */
		rval = 0;
	}
	return rval;
}

void nl_timer_sche(int signo)
{
	int i;
	U32	emap;

	if (SIGALRM != signo)
	{
		EPT(stderr, "Caught the other signal %d\n", signo);
		return;
	}
		
//	EPT(stderr, "Caught the SIGALRM signal\n");	

	emap = nl_tsch.tmap&(nl_tsch.tmap^nl_tsch.tmask);  //^Ϊ��λ���������������˴��������㣬ʹ��nl_tsch.tmask�����������
	for (i = 0; i < nl_tfs; i++)
	{
		if (!((1<<i)&emap))
			continue;
		
		nl_tsch.procs[i].wait -= 1;
		if (nl_tsch.procs[i].wait <= 0)
		{
			nl_tsch.procs[i].wait = nl_tsch.procs[i].period;
			(*nl_tsch.procs[i].pf)(&i);
		}
	}
}

void nl_timer_test1(void *data)
{  
	int id = *(int *)data;

	EPT(stderr, "Caught the SIGALRM signal for %s\n", nl_tsch.procs[id].name);
//	EPT(stderr, "timer in thread id = %ld\n", pthread_self());
}

void nl_timer_test2(void *data)
{  
	int id = *(int *)data;

	EPT(stderr, "Caught the SIGALRM signal for %s\n", nl_tsch.procs[id].name);
}  

void nl_timer_self(void* data)
{
	int id = *(int *)data;
}
