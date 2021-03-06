#include "../mr_common.h"
#include "hm_timer.h"


//tsche_t nl_tsch = {
//	0,
//	0,
//	{
//		{"timer1", 0, 5000 /*ms*/, hm_timer_test1},
//		{"timer2", 0, 3000 /*ms*/, hm_timer_test2}
//	}
//};

//const int nl_tfs = sizeof(nl_tsch.procs)/sizeof(nl_tsch.procs[0]);



/* 定时器赋值结构体 */
struct itimerval new_value;

/* 定时器结束标志位 */
U8 timer_flag = 0;

//信号量
extern sem_t empty[];

/*
void hm_tsch_init()
{
	int i, cnt;
	tproc_t *p;

	ASSERT(nl_tsch.tmap == 0);
	ASSERT(nl_tsch.tmask == 0);

	for (i = 0; i < nl_tfs; i++)
	{
		p = &nl_tsch.procs[i];
		if (p->period == 0)
		{
			ASSERT(nl_tsch.procs[i].pf == NULL);
			continue;
		}

		nl_tsch.tmap = nl_tsch.tmap|(1<<i);
		cnt = (p->period * 1000)/NL_TINTVL_US;
		if (cnt < 1)
			p->period = 1;
		else
			p->period = cnt;

		p->wait = p->period;
	}
}
*/

/*
测试程序值考虑固定时隙，所以按照一超帧288ms设计
*/

int hm_start_timer1()
{
	int rval;

    //new_value.it_value.tv_sec = 0;
    //new_value.it_value.tv_usec = 600000;
	new_value.it_value.tv_sec = 4;
    new_value.it_value.tv_usec = 300000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;

	signal(SIGALRM, hm_timer_sche);
	rval = setitimer(ITIMER_REAL, &new_value, NULL);
	if (-1 == rval)
	{/* failure */
		EPT(stderr, "error occurs in setting timer1 %d[%s]\n", errno, strerror(errno));
		exit(1);
	}
	else
	{/* success */
		rval = 0;
	}

	return rval;
}

int hm_start_timer2()
{
	int rval;

    //new_value.it_value.tv_sec = 0;
    //new_value.it_value.tv_usec = 300000;
    new_value.it_value.tv_sec = 2;
    new_value.it_value.tv_usec = 200000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;

	signal(SIGALRM, hm_timer_sche);
	rval = setitimer(ITIMER_REAL, &new_value, NULL);
	if (-1 == rval)
	{/* failure */
		EPT(stderr, "error occurs in setting timer2 %d[%s]\n", errno, strerror(errno));
		exit(1);
	}
	else
	{/* success */
		rval = 0;
	}

	return rval;
}

int hm_start_timer3()
{
	int rval;

    //new_value.it_value.tv_sec = 0;
    //new_value.it_value.tv_usec = 600000;
    new_value.it_value.tv_sec = 4;
    new_value.it_value.tv_usec = 300000;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;

	signal(SIGALRM, hm_timer_sche);
	rval = setitimer(ITIMER_REAL, &new_value, NULL);
	if (-1 == rval)
	{/* failure */
		EPT(stderr, "error occurs in setting timer3 %d[%s]\n", errno, strerror(errno));
		exit(1);
	}
	else
	{/* success */
		rval = 0;
	}

	return rval;
}


void hm_timer_sche(int signo)
{
	timer_flag = 1;    /* 表示定时器已到达 */
	sem_post(&(empty[1]));
}













/*
void hm_timer_sche(int signo)
{
	int i;
	U32	emap;

	if (SIGALRM != signo)
	{
		EPT(stderr, "Caught the other signal %d\n", signo);
		return;
	}

//	EPT(stderr, "Caught the SIGALRM signal\n");

	emap = nl_tsch.tmap&(nl_tsch.tmap^nl_tsch.tmask);
	for (i = 0; i < nl_tfs; i++)
	{
		if (!((1<<i)&emap))
			continue;

		nl_tsch.procs[i].wait -= 1;
		if (nl_tsch.procs[i].wait <= 0)
		{
			nl_tsch.procs[i].wait = nl_tsch.procs[i].period;  //时间到后重新赋值
			(*nl_tsch.procs[i].pf)(&i);
		}
	}
}
*/

/*
void hm_timer_test1(void *data)
{
	int id = *(int *)data;

//	EPT(stderr, "Caught the SIGALRM signal for %s\n", nl_tsch.procs[id].name);
//	EPT(stderr, "timer in thread id = %ld\n", pthread_self());
}

void hm_timer_test2(void *data)
{
	int id = *(int *)data;

//	EPT(stderr, "Caught the SIGALRM signal for %s\n", nl_tsch.procs[id].name);
}

void hm_timer_self(void* data)
{
	int id = *(int *)data;
}
*/
