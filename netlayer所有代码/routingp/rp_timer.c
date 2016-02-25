#include "mr_common.h"
#include "rp_common.h"
#include "rp_timer.h"
#include "rp_fhr.h"

tsche_t rp_tsch = {
	0,              //map
	0,              //mask
	{
		{"sop generate", 0, RT_SOP_INTV,  rp_sop_gen},	//RT_SOP_INTV1000ms��5��ϵͳ��ʱ�����ڵ���һ��rp_sop_gen
		{"route expire", 0, RT_ITEM_EXPI, rp_rt_check},//RT_ITEM_EXPI 5000ms
		{"link maintain", 0, RT_LINK_CHK, rp_lk_check} //RT_LINK_CHK 5000ms
		//name,wait,period,function
	}
};

const int rp_tfs = sizeof(rp_tsch.procs)/sizeof(rp_tsch.procs[0]);//numbers of timer
//itimerval����Ӧ���Ƕ�ʱ�����������Ӧ�����״��������ӳ�
struct itimerval new_value = {
{RP_TINTVL_S, RP_TINTVL_US},  //0s,200000us
{RP_TDELAY_S, RP_TDELAY_US}   //1s,0us
};

extern rtable_t rt;
extern ntable_t nt;
extern MADR*	sa;

void rp_tsch_init()
{
	int i, cnt, sopcnt = 0;;
	tproc_t *p;//��ʱ���ṹ��ָ�룬�������֣��ȴ�����������ú����ĸ���Ա

	ASSERT(rp_tsch.tmap == 0);
	ASSERT(rp_tsch.tmask == 0);
    //�û�������rp_tfs��ӳ�䶨ʱ��
	for (i = 0; i < rp_tfs; i++)
	{
		p = &rp_tsch.procs[i];
		if (p->period == 0)
		{
			ASSERT(rp_tsch.procs[i].pf == NULL);
			continue;
		}

		rp_tsch.tmap = rp_tsch.tmap|(1<<i);
		//�ȴ�cnt��ϵͳ��ʱ�����ڼ���һ�ζ�ʱ��rp_tsch.procs[i],��ĸ��200000us
		//��ʱ���ĳ�ֵperiod��1000ms������5000ms�Ȳ�����˵��������1000ms������Ҫ�����ֵ�����������
		//����������㣬rp_sop_gen��rp_rt_check��rp_lk_check��period�ֱ�Ϊ1,5,5
		cnt = (p->period * 1000)/RP_TINTVL_US;
		if (cnt < 1)
			p->period = 1;
		else
			p->period = cnt;
#if 0
        //ȷ������ӳ�䶨ʱ���ĵ�������,#if 1 ����������ƺ�û���õ���
        //sop generate��ʱ��Ϊÿһ�����һ��rp_sop_gen��������sop��
		if (1 == strcmp("sop generate", p->name))
		{
			sopcnt = p->period;
		}
		//route expire��ʱ��Ϊÿ���ɴζ�ʱ������һ��rp_rt_check�������·��
		else if (1 == strcmp("route expire", p->name))
		{
			if (0 != sopcnt)
			{
				p->period = sopcnt*(RT_ITEM_EXPI/RT_SOP_INTV) + 1;
			}
		}
		//route expire��ʱ��Ϊÿ���ɴζ�ʱ������һ��rp_lk_check���������·
		else if (1 == strcmp("link maintain", p->name))
		{
			if (0 != sopcnt)
			{
				p->period = sopcnt*(RT_LINK_CHK/RT_SOP_INTV) + 1;
			}
		}
#endif
		p->wait = p->period;
	}
}
//��main��������
int rp_start_timer()
{
	int rval;

	rp_tsch_init();//ȷ������ӳ�䶨ʱ���ĵ������ڣ�����ϵͳ��ʱ����

	signal(SIGALRM, rp_timer_sche);//rp_timer_sche  wait for SIGALRM
	rval = setitimer(ITIMER_REAL, &new_value, NULL);//��ʱ���ｫ����SIGVTALRM�źŸ�����
	if (-1 == rval)	{
		/* failure */
		EPT(stderr, "error occurs in setting timer %d[%s]\n", errno, strerror(errno));
	}
	else {
		/* success */
		rval = 0;
	}
	return rval;
}
//�յ�SIGALRM�źţ�����øú�����������Ϊ1s
void rp_timer_sche(int signo)
{
   // printf("rcv timer\n");
	int i;
	U32	emap;

	if (SIGALRM != signo)
	{
		EPT(stderr, "Caught the other signal %d\n", signo);
		return;
	}

//	EPT(stderr, "Caught the SIGALRM signal\n");
    //&�����㣬^�������
	emap = rp_tsch.tmap&(rp_tsch.tmap^rp_tsch.tmask);
	for (i = 0; i < rp_tfs; i++)
	{
		if (!((1<<i)&emap))
			continue;
        //wait��ֵ����period��ÿһ�ξ���ϵͳ��ʱ����wait��һ
		rp_tsch.procs[i].wait -= 1;
		if (rp_tsch.procs[i].wait <= 0)
		{
		    //��wait����0�����³�ʼ��Ϊperiod
			rp_tsch.procs[i].wait = rp_tsch.procs[i].period;
			//���øö�ʱ���ĺ���������ΪʲôΪi��
			(*rp_tsch.procs[i].pf)(&i);
		}
	}
}
//ÿ�յ�1�ζ�ʱ���źŵ���һ�Σ�������Ϊ1s
void rp_sop_gen(void *data)
{
    //int id = *(int *)data;
    //EPT(stderr, "Caught the SIGALRM signal for %s\n", rp_tsch.procs[id].name);
	int rval, i;
	int len = 0;
    //��Ϣ����=mtyp + node + (mmhd)pkg_headd + sop_head + true_data
	mmsg_t tmsg;
	mmhd_t *phd = (mmhd_t *)tmsg.data;
	sop_hd *psh = (sop_hd *)(tmsg.data + MMHD_LEN);
	//sop_head = node + icnt����ַ������
	U8 *items = &psh->icnt;
    //·��Э����Ϣ����
	tmsg.mtype = MMSG_RPM;
#ifdef _MR_TEST
    //���ڵ�ŵĵ�ַ
	tmsg.node = *sa;
#else
	tmsg.node = MADR_BRDCAST;
#endif
	/*rp message header */
	phd->type = RPM_FHR_SOP;
	phd->len = MMHD_LEN + sizeof(sop_hd);
	//����phd�ĳ���
	len += MMHD_LEN;
	/* sop header */
	psh->node = *sa;
	*items = 0;
	//����psh�ĳ���
	len += sizeof(sop_hd);
//	EPT(stderr, "psh->icnt=%d\n", psh->icnt);
	for(i = 0; i < MAX_NODE_CNT; i++)
	{
	    //�������ƹ�ϵ�����ʽ���±�ͱ����Ŀ�ĵ�ַӳ���Ӧ
		ASSERT(MR_IN2AD(i) == rt.item[i].dest);
		//�����ڵ�ı�������
		if (MR_IN2AD(i) == *sa)
			continue;
		rval = 0;
		//��Ŀ�ĵ�ַ��������ÿ���ڵ�һ������tmsg.data + len��ʼ�ĵ�ַ��������䳤�ȣ���������֤�Ƿ�Խ��
		rval = ritem_sopget(&rt.item[i], tmsg.data + len, MAX_DATA_LENGTH - len);
		if (rval == -1)
			EPT(stderr, "error occurs in ritem_sogget()\n");
		else
		{
			if (rval > 0)
			{
				*items += 1;
				len += rval;
			}
		}
	}
//	EPT(stderr, "psh->icnt=%d\n", psh->icnt);
    //�������psh����Ϣĩβ�ĳ��ȣ�ע�⣬len����Ϣ���е�data�ܳ��ȣ�
	phd->len = len - MMHD_LEN;
	//ͨ����Ϣ���з���tmsg����һ�������Ƿ��ͳ���
	rp_tmsg_2nl(len + sizeof(MADR), &tmsg);//msg_snd
}
//ÿ�յ�5�ζ�ʱ���źŵ���һ�Σ�������Ϊ5s
void rp_rt_check(void *data)
{
	//int id = *(int *)data;
	//EPT(stderr, "Caught the SIGALRM signal for %s\n", rp_tsch.procs[id].name);
	int i;
	int comp, sn;

	//rt_show();
	for(i = 0; i < MAX_NODE_CNT; i++) {
		ASSERT(MR_IN2AD(i) == rt.item[i].dest);
        //ritem_fsm(&rt.item[i], 1);
		if (IS_NULL == rt.item[i].pfst.status)
			continue;

		ritem_fsm(&rt.item[i], 1);
	}
	//���͸���ת����
	update_fwt();
}
//ÿ�յ�5�ζ�ʱ���źŵ���һ�Σ�������Ϊ5s
void rp_lk_check(void *data)
{
	//int id = *(int *)data;
	//EPT(stderr, "Caught the SIGALRM signal for %s\n", rp_tsch.procs[id].name);
	int i, j, nn, change;
	rpath_t *path;
	rlink_t *lk;

	//nt_show();

	for (i = 0; i < MAX_NODE_CNT; i++)
	{
		lk = &nt.fl[i];
		//����յ�����Ϊ0������·״̬ΪLQ_NULL������������·
		if (LQ_NULL == lk->lstatus && 0 == lk->rcnt) {
			continue;
		}
        //EPT(stderr, "node[%d]: enter to check the link to node %d\n", *sa, MR_IN2AD(i));
		//�����յ��İ���rcnt������·״̬��ת�ƣ�ת�Ƴɹ��򷵻�1
		change = rlink_fsm(MR_IN2AD(i), 1);

		if (change == 0)
		{
			continue;
		}
        //Ϊ�δ˴����ԣ�
		ASSERT(LQ_ACTIVE != lk->lstatus);
		nn = MR_IN2AD(i);
		EPT(stderr, "node[%d]: find link to %d changed, status=%d\n", *sa, nn, lk->lstatus);

		/* update route */
		for (j = 0; j < MAX_NODE_CNT; j++)
		{
			/* second path */
			path = &rt.item[j].psnd;
			//��·��·������һ��������check����·�ڽӵ�
			if ((IS_NULL !=path->status)&&(nn == path->node[0]))
			{
			    //���·��·��״̬������·״̬��������·״̬�滻·��·��״̬
				if (path->status > lk->lstatus)
				{
					path->status = lk->lstatus;
					if (IS_NULL == path->status)
						rpath_clear(path);
				}
			}
			/* primary path */
			path = &rt.item[j].pfst;
			if ((IS_NULL !=path->status)&&(nn == path->node[0]))
			{
				if (path->status > lk->lstatus)
				{
					path->status = lk->lstatus;
					if (IS_NULL == path->status)
						rpath_clear(path);
				}
				int sn;
				if (IS_NULL != rt.item[j].psnd.status)
				{
				    //·��·���ȽϷ��ط�2˵���μ�·�����ã���sn��0˵����һ���ڵ㲻ͬ
					if (2 != rpath_up(MR_IN2AD(j), &rt.item[j].psnd, path, &sn))
						ASSERT(0 == sn);
				}
			}
		}
		nt_show();
		rt_show();
	}
}
