#include "../mr_common.h"
#include "hm_timer.h"
#include "hm_dslot.h"

//�ź���
extern sem_t empty[];

/* ͳ��һ���ڽڵ�REP�Ƿ�����ռ�� */
extern U8 REP_count[MAX_NODE_CNT+1];  //REP_count[0]Ϊ����ڵ��������1Ϊ����0Ϊ������

/* ͳ��REP֡Я���Ķ�̬ʱ϶���� */
extern U8 REP_dslot[MAX_DSLS_PCF+1];  //ͳ��REP֡�а�����ʱ϶����

/* ��̬ʱ϶ԤԼ/�ͷű�־��0��ʾ����ԤԼ/�ͷŹ����У�1��ʾ����ԤԼ��2��ʾ�����ͷ� */
extern U8 dslot_order_flag;  

/* �ڽڵ�������¼�� */
extern U8 hop_table[];  //hop_table[i]=0���ڵ�i����������Χ�ڣ�hop_table[i]=1���ڵ�iΪ1����hop_table[i]=2���ڵ�iΪ2�������ؽڵ���ʱ���ƣ�

/* ���ؽڵ�ID */
extern U8 localID;

/* �ڵ�ά���Ķ�̬ʱ϶�ܱ��������ؽڵ���������ʹ�� */
extern U8 dslot_map1[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];  //�������D1-D55��55����̬ʱ϶���������1-32���ڵ㣬dslot_map[0][i]��ʾi�ڵ�ռ�õ�ʱ϶������D[i][0]��ʾռ��iʱ϶�ڵ���ܸ�����ֵΪ0��ʾʱ϶δռ�ã�1��ʾԤռ�ã�2��ʾ��ռ��
extern U8 dslot_map2[MAX_DSLS_PCF+1][MAX_CFS_PSF+1];






/* ��ʱ��������־λ */
U8 timer_flag = 0;


/* ���Գ����ǹ̶�+��̬ʱ϶�����԰���һ��֡2100ms(ʵ��2048ms)��ƣ�ÿ��ʱ϶1ms */
/* slot.cʹ�õĶ�ʱ�� */
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
/* dslot.cʹ�õĶ�ʱ�� */
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

	memset(&nl_tsch.tmask, 0xff, sizeof(nl_tsch.tmask));  //��ʼ����ֹʹ�ö�ʱ��������λ����1
	memset(dslot_timer.tmask, 0xff, sizeof(dslot_timer.tmask));  //��ʼ����ֹʹ�ö�ʱ��������λ����1
	
	for (i = 0; i < nl_tfs; i++)
	{
		p = &nl_tsch.procs[i];
		if (p->period == 0)
		{
			ASSERT(nl_tsch.procs[i].pf == NULL);
			continue;
		}

		nl_tsch.tmap = nl_tsch.tmap|(1<<i);   //���¶�ʱ��map
		cnt = (p->period * 1000)/NL_TINTVL_US; //���ձ�׼ʱ��������ʱʱ��
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

	/* slot.cʹ�õĶ�ʱ������ */
	
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
			nl_tsch.procs[i].wait = nl_tsch.procs[i].period;  //��ʱ������ֹͣ�����¸���ֵ������ʱ���ֶ�ֹͣ������Ҫ��ֹͣ��λ�ø���ֵ
			//EPT(stderr, "i = %d\n", i);
			(*nl_tsch.procs[i].pf)(&i);
			
		}
	}	

	/* dslot.cʹ�õĶ�ʱ������ */
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
				dslot_timer.procs[i][j].wait = dslot_timer.procs[i][j].period;  //��ʱ������ֹͣ�����¸���ֵ������ʱ���ֶ�ֹͣ������Ҫ��ֹͣ��λ�ø���ֵ
				dslot_timer.procs[i][0].wait--;		
				(*dslot_timer.procs[i][j].pf)(&i, &j);  //���ǲ���
			}

		}
	}		
}

void hm_timer1(void *data)
{  
	int id = *(int *)data;
	EPT(stderr, "hm_timer1 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<0);
	timer_flag = 1;    /* ��ʾ��ʱ���ѵ��� */
	sem_post(&(empty[1]));
}

void hm_timer2(void *data)
{  
	int id = *(int *)data;
	EPT(stderr, "hm_timer2 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<1);
	timer_flag = 1;    /* ��ʾ��ʱ���ѵ��� */
	sem_post(&(empty[1]));
}  

void hm_timer3(void* data)
{
	int id = *(int *)data;
	//EPT(stderr, "hm_timer3 up\n");

	nl_tsch.tmask = nl_tsch.tmask | (1<<2);
	timer_flag = 1;    /* ��ʾ��ʱ���ѵ��� */
	sem_post(&(empty[1]));
}

void REQ_timer(void* data)
{
	int id = *(int *)data;
	EPT(stderr, "REQ_timer up\n");
	nl_tsch.tmask = nl_tsch.tmask | (1<<3);

	/* ���ͳ������ */
	memset(REP_count, 0, sizeof(REP_count));
	memset(REP_dslot, 0, sizeof(REP_dslot));
	dslot_order_flag = 0;  //�رձ��ؽڵ�ԤԼ���̱�־	
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

	/* ��ʱhop_table[j]�Ѿ�Ϊ0�����Բ���������Ϊ�ж���������Ϊֱ���ж�dslot_map1��dslot_map2 16.12.4 */
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
	else if(dslot_map2[i][j] != 0)  /* һ���ڵ㲻���ܼ�Ϊ1���ڵ���Ϊ2���ڵ� 16.12.4 */
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











