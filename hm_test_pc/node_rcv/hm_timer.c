#include "../mr_common.h"
#include "hm_timer.h"
#include "hm_dslot.h"

//信号量
extern sem_t empty[];

/* 统计一跳邻节点REP是否允许占用 */
extern U8 REP_count[MAX_NODE_CNT+1];  //REP_count[0]为允许节点的总数，1为允许，0为不允许

/* 统计REP帧携带的动态时隙个数 */
extern U8 REP_dslot[MAX_DSLS_PCF+1];  //统计REP帧中包含的时隙个数

/* 动态时隙预约/释放标志，0表示不在预约/释放过程中，1表示正在预约，2表示正在释放 */
extern U8 dslot_order_flag;  

/* 邻节点跳数记录表 */
extern U8 hop_table[];  //hop_table[i]=0，节点i不在两跳范围内；hop_table[i]=1，节点i为1跳；hop_table[i]=2，节点i为2跳；本地节点暂时不计；

/* 本地节点ID */
extern U8 localID;

/* 节点维护的动态时隙总表，仅供本地节点加入的网络使用 */
extern U8 dslot_map1[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];  //纵向代表D1-D55共55个动态时隙，横向代表1-32个节点，dslot_map[0][i]表示i节点占用的时隙总数，D[i][0]表示占用i时隙节点的总个数，值为0表示时隙未占用，1表示预占用，2表示已占用
extern U8 dslot_map2[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];






/* 定时器结束标志位 */
U8 timer_flag = 0;


/* 测试程序考虑固定+动态时隙，所以按照一超帧2100ms(实际2048ms)设计，每个时隙1ms */
/* slot.c使用的定时器 */
tsche_t nl_tsch = {
	0,
	0,
	{
		{"timer1", 0, 4200 /*ms*/, hm_timer1},
		{"timer2", 0, 3000 /*ms*/, hm_timer2},
		{"timer3", 0, 4200 /*ms*/, hm_timer3},	
		{"REQ_timer", 0, T1/*ms*/, REQ_timer},
	}
};
/* dslot.c使用的定时器 */
tdslot_t dslot_timer;

const int nl_tfs = sizeof(nl_tsch.procs)/sizeof(nl_tsch.procs[0]);

struct itimerval new_value = {
{NL_TDELAY_S, NL_TDELAY_US}, 
{NL_TINTVL_S, NL_TINTVL_US}
};

void hm_tsch_init()
{
	int i, cnt;
	tproc_t *p;	

	memset(&nl_tsch.tmask, 0xff, sizeof(nl_tsch.tmask));  //初始化禁止使用定时器，所有位均置1
	memset(dslot_timer.tmask, 0xff, sizeof(dslot_timer.tmask));  //初始化禁止使用定时器，所有位均置1
	
	for (i = 0; i < nl_tfs; i++)
	{
		p = &nl_tsch.procs[i];
		if (p->period == 0)
		{
			ASSERT(nl_tsch.procs[i].pf == NULL);
			continue;
		}

		nl_tsch.tmap = nl_tsch.tmap|(1<<i);   //更新定时器map
		cnt = (p->period * 1000)/NL_TINTVL_US; //按照标准时间量化定时时间
		if (cnt < 1)
			p->period = 1;
		else
			p->period = cnt;
		
		p->wait = p->period;
		EPT(stderr, "timer %d:p->period = %d\n", i, p->period);
	}
}

int hm_start_timer()
{
	int rval;

	hm_tsch_init();
	
	signal(SIGALRM, hm_timer_sche);
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

void hm_timer_sche(int signo)
{
	int i,j;
	U32	emap;

	if (SIGALRM != signo)
	{
		EPT(stderr, "Caught the other signal %d\n", signo);
		return;
	}		

	/* slot.c使用的定时器处理 */
	
	emap = nl_tsch.tmap&(nl_tsch.tmap^nl_tsch.tmask);
	//EPT(stderr, "emap = %d\n", emap);
	for (i = 0; i < nl_tfs; i++)
	{		
		if (!((1<<i)&emap))
			continue;
		//EPT(stderr, "%d 222\n", i);
		nl_tsch.procs[i].wait -= 1;
		if (nl_tsch.procs[i].wait <= 0)
		{
			nl_tsch.procs[i].wait = nl_tsch.procs[i].period;  //定时器正常停止，重新赋初值，若定时器手动停止，则需要在停止的位置赋初值
			//EPT(stderr, "i = %d\n", i);
			(*nl_tsch.procs[i].pf)(&i);
			
		}
	}	

	/* dslot.c使用的定时器处理 */
	for(i = 1; i <= MAX_DSLS_PCF; i++)
	{		
		if(!dslot_timer.procs[i][0].wait)
			continue;
		//EPT(stderr, "333 %d\n", dslot_timer.procs[i][0].wait);
		
		emap = dslot_timer.tmap[i]&(dslot_timer.tmap[i]^dslot_timer.tmask[i]);
		for(j = 1; j <= MAX_NODE_CNT; j++)
		{
			if (!((1<<(j-1))&emap))
			continue;
			
			dslot_timer.procs[i][j].wait -= 1;
			if (dslot_timer.procs[i][j].wait <= 0)
			{
				EPT(stderr, "dslot_timer.procs[%d][%d].wait = %d\n", i, j, dslot_timer.procs[i][j].wait);
				dslot_timer.procs[i][j].wait = dslot_timer.procs[i][j].period;  //定时器正常停止，重新赋初值，若定时器手动停止，则需要在停止的位置赋初值
				dslot_timer.procs[i][0].wait--;		
				(*dslot_timer.procs[i][j].pf)(&i, &j);  //考虑参数
			}

		}
	}		
}

void hm_timer1(void *data)
{  
	int id = *(int *)data;
	EPT(stderr, "hm_timer1 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<0);
	timer_flag = 1;    /* 表示定时器已到达 */
	sem_post(&(empty[1]));
}

void hm_timer2(void *data)
{  
	int id = *(int *)data;
	EPT(stderr, "hm_timer2 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<1);
	timer_flag = 1;    /* 表示定时器已到达 */
	sem_post(&(empty[1]));
}  

void hm_timer3(void* data)
{
	int id = *(int *)data;
	//EPT(stderr, "hm_timer3 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<2);
	timer_flag = 1;    /* 表示定时器已到达 */
	sem_post(&(empty[1]));
}

void REQ_timer(void* data)
{
	int id = *(int *)data;
	EPT(stderr, "REQ_timer up\n");
	nl_tsch.tmask = nl_tsch.tmask | (1<<3);

	/* 清空统计数据 */
	memset(REP_count, 0, sizeof(REP_count));
	memset(REP_dslot, 0, sizeof(REP_dslot));
	dslot_order_flag = 0;  //关闭本地节点预约过程标志	
}

void ds_timer(void* a, void* b)
{
	int i = *(int *)a;
	int j = *(int *)b;
	EPT(stderr, "ds_timer_D%dN%d up\n", i, j);
	//dslot_timer.procs[i][0].wait--;							
	dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(j-1));

	if(hop_table[j] == 1 || j == localID)
	{
		if(dslot_map1[i][j] != 2)
		{
			dslot_map1[i][j] = 0;
			dslot_map1[i][0]--;
			EPT(stderr, "dslot_map1[%d][0] = %d\n", i, dslot_map1[i][0]);
		}
	}
	else if(hop_table[j] == 2)
	{
		if(dslot_map2[i][j] != 2)
		{
			dslot_map2[i][j] = 0;
			dslot_map2[i][0]--;
			EPT(stderr, "dslot_map2[%d][0] = %d\n", i, dslot_map2[i][0]);
		}
	}
}

void ds_sf_timer(void* a, void* b)
{
	int i = *(int *)a;
	int j = *(int *)b;
	EPT(stderr, "ds_sf_timer_D%dN%d up\n", i, j);
	//dslot_timer.procs[i][0].wait--;							
	dslot_timer.tmask[i] = dslot_timer.tmask[i] | (1<<(j-1));

	/* 此时hop_table[j]已经为0，所以不能用来作为判断条件，改为直接判断dslot_map1、dslot_map2 16.12.4 */
	if(dslot_map1[i][j] != 0)
	{
		if(dslot_map1[i][j] == 2)
		{			
			dslot_map1[0][j]--;
		}
		dslot_map1[i][j] = 0;
		dslot_map1[i][0]--;	
		EPT(stderr, "dslot_map1[0][%d] = %d dslot_map1[%d][0] = %d\n", j, dslot_map1[0][j], i, dslot_map1[i][0]);
	}
	else if(dslot_map2[i][j] != 0)  /* 一个节点不可能即为1跳节点又为2跳节点 16.12.4 */
	{	
		if(dslot_map2[i][j] == 2)
		{
			dslot_map2[0][j]--;
		}
		dslot_map2[i][j] = 0;
		dslot_map2[i][0]--;
		EPT(stderr, "dslot_map2[0][%d] = %d dslot_map2[%d][0] = %d\n", j, dslot_map2[0][j], i, dslot_map2[i][0]);
	}
}











