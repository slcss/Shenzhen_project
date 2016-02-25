#include "mr_common.h"
#include "rp_common.h"
#include <sys/shm.h>

#ifdef _MR_TEST
#pragma message("mr_test enbale, only routingp can turn on this MACRO.")
#endif

extern qinfo_t qinfs[];
extern const int cnt_p;

extern int  qs, re_qin, nl_qid, hm_qid, vi_qid, rp_qid, mt_qid;

static rp_tshare_t  share = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER
};

static pthread_t fq_tid = -1;
static pthread_t mrx_tid = -1;

/* routing table, neighbour table */
rtable_t	rt;
ntable_t	nt;
MADR		*sa = &rt.self;

static int		ft_sid = -1;

static fwt_t *pft = NULL;
//���������ڷ��ͼ򻯰��ת����
char fwt_send[MAX_NODE_CNT];

int main(int argc, char* argv[])
{

	int len;
	int j;
	int rval, stop;
	void *result = NULL;

	EPT(stderr, "%s: main thread id = %ld\n", argv[0], pthread_self());
//	EPT(stderr, "sizeof(MADR) = %d\n", sizeof(MADR));

	if (argc < 2)
	{
		EPT(stderr, "run %s: must privide the address of itself node\n", argv[0]);
		rval = 1;
		goto process_return;
	}
	else
	{
		j = atoi(argv[1]);
	}

	//mr_queues_init("routingp");

	if (j >= MADR_UNI_MIN && j <= MADR_UNI_MAX)
	{
		/* initialize routing table, neighbour table, and so on */
		rp_init(j);
	}
	else
	{
		rval = 2;
		EPT(stderr, "run %s: must privide the correct node address\n", argv[0]);
		goto process_return;
	}

	/* this thread obtain all queues for rx and tx */
#ifdef _MR_TEST
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, sa);
#else
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, argv[0]);
#endif

	if (rval != 0)
	{
		EPT(stderr, "process %s: create get queue thread fails", argv[0]);
		rval = 3;
		goto process_return;
	}
	while (qs == 0)
	{
		sleep(1);
	}

#ifdef _MR_TEST
	/* send self qid to mr_test process */
	mmsg_t tx_msg;
	tx_msg.mtype = MMSG_MT_RQID;
	tx_msg.node = *sa;
	len = sizeof(tx_msg.node);

	*(int *)tx_msg.data = rp_qid;
	len += sizeof(int);
	msgsnd(mt_qid, (void *)&tx_msg, len, 0);
#endif
	/* get shared memory id */
	//key_t shm_k = qinfs[re_qin].key_q;
	key_t shm_k = ftok(PATH_CREATE_KEY, 10010);
    ft_sid = shmget(shm_k, sizeof(fwt_t), 0640|IPC_CREAT);
    if(ft_sid == -1)
    {
        EPT(stderr, "routingp: shmget error\n");
		rval = 2;
        goto process_return;
    }

    void	*shmaddr = (void*) -1;
    shmaddr = shmat(ft_sid, NULL, 0);
    if (shmaddr == (void*)-1 )
    {
		EPT(stderr, "routingp: can not attach shared memory.\n");
		rval = 2;
        goto process_return;
	}

    pft = (fwt_t *)shmaddr;
	memset(pft,0,sizeof(pft));
	//��ʼ��ת����
	pft->self = j;
    int i;
    for(i = 0; i < MAX_NODE_CNT; i++)
    {
        pft->ft[i].dest = i+1;
    }

	/* start timer */
	rval = rp_start_timer();
	if (0 != rval) {
		EPT(stderr, "%s: can not open start timer function\n", argv[0]);
		rval = 4;
		goto process_return;
	}

	/* create receiving msg thread */
	rval = pthread_create(&mrx_tid, NULL, rp_qrv_thread, &(qinfs[re_qin].qid));
	if (rval != 0) {
		EPT(stderr, "%s: can not open create msg receiving thread\n", argv[0]);
		rval = 3;
		goto process_return;
	}

	//�����ⲿ�źŴ�ӡ·�ɱ�
	signal(SIGUSR1, signal_show);

	stop = 0;
	pthread_mutex_lock(&share.mutex);
	while(0 == stop)
	{
		pthread_cond_wait(&share.cond, &share.mutex);

		/* do something */
	}
//	EPT(stderr, "%s: certain thread quit\n", argv[0]);
	pthread_mutex_unlock(&share.mutex);

process_return:
	sleep(1);

    if((pft != (void*)-1)&&(shmdt(pft) == -1))
		EPT(stderr, "routingp: detach shared memory error\n");
    if ((ft_sid != -1)&&(shmctl(ft_sid, IPC_RMID, NULL)) == -1)
		EPT(stderr, "routingp: delete shared memory error\n");

	mr_queues_delete();
	exit(rval);
}

void* rp_qrv_thread(void *arg)
{
	int qid, rcnt;
	mmsg_t rx_msg;
	int rval, stop;

	pthread_detach(pthread_self());

	qid = *(int *)arg;
	ASSERT(qinfs[re_qin].qid == qid);
	EPT(stderr, "%s: msg receiving thread id = %ld\n", qinfs[re_qin].pname, pthread_self());
//	EPT(stderr, "%s: enter queue receiving thread, rqueue %d.\n", qinfs[re_qin].pname, qid);

	if (qid < 0) {
		EPT(stdout, "%s: wrong receive queue id %d", qinfs[re_qin].pname, qid);
		rval = 1;
		goto thread_return;
	}

	rval = 0;
	stop = 0;
	while(0 == stop) {
		memset(&rx_msg, 0, sizeof(rx_msg));
		rcnt = msgrcv(qid, &rx_msg, MAX_DATA_LENGTH, 0, 0);
//		EPT(stdout, "%s: reveive msg queue at qid %d\n", qinfs[re_qin].pname, qid);
		if (rcnt < 0) {
			if (EIDRM != errno)
			{
				EPT(stderr, "%s: error in receiving msg, no:%d, meaning:%s\n", qinfs[re_qin].pname, errno, strerror(errno));
			}
			else
			{
				EPT(stderr, "%s: quit msg receiving thread\n", qinfs[re_qin].pname);
			}
			rval = 2;
			break;
		}

		if (rcnt < MMSG_FIXLEN) {
			//EPT(stderr, "%s: the len of rx_msg  = %d\n", qinfs[re_qin].pname, rcnt);
			continue;
		}

		switch(rx_msg.mtype) {
			case MMSG_RPM:
				rval = rp_rpm_proc(rx_msg.node, rcnt - sizeof(MADR), &rx_msg.data);
				if (rval != 0) {
					/* report error */
				}
				break;

			default:
				/* report error */
				EPT(stderr, "%s: the node of rx_msg  = %d", qinfs[re_qin].pname, rx_msg.node);
				break;
		}
	}

thread_return:
/*	pthread_mutex_lock(&share.mutex);
	share.qr_run = 0;
	pthread_cond_signal(&share.cond);
	pthread_mutex_unlock(&share.mutex);
*/
	sleep(1);
	pthread_exit((void *)&rval);
}
//�յ����ݺ���øú���,������Ϣ����data���ֵ�ͷ��
//�����len����Ϣ����data���ֳ��ȣ�������������data������ʼ��ַ
int rp_rpm_proc(MADR node, int len, void *data)
{
	mmhd_t *pmhd;
	int pos = 0;

	/* the code here is related to rp message definition */
	pmhd = (mmhd_t*)data;
	pos += MMHD_LEN;

//	EPT(stdout, "node[%d]: reveive rp message, type=%ld\n", *sa, mt);
	switch(pmhd->type)
	{
		case RPM_FHR_SOP:
		case RPM_FHR_RII:
		case RPM_FHR_RIR:
            //�����������Ͷ���������溯������Ϊû��break
			rp_fhrmsg_disp(node, pmhd->type, pmhd->len, data + pos);
			break;

		default:
			/* report errors */
			EPT(stdout, "node[%d]: reveive unkown rp message, type=%d\n", *sa, pmhd->type);
			break;
	}

	return 0;
}
//·��·�����㣬״̬��ΪIS_NULL
void rpath_clear(rpath_t *prp)
{
	prp->hop = 0;
	memset(prp->node, 0, sizeof(prp->node));
	prp->status = IS_NULL;
	prp->flag = 0;
}
//ˢ�²��ҷ���ת����
//�ֱ����յ�sop���ͼ�ʱ�������Լ��·��ʱ����
void update_fwt()
{
    int change = 0;
    int i;
    for(i=0;i<MAX_NODE_CNT;i++)
    {
        if(pft->ft[i].fnd != rt.item[i].pfst.node[0])
        {
            pft->ft[i].fnd = rt.item[i].pfst.node[0];
            //�򻯰��ת����Ŀ�ĵ�ַ��i+1����һ����ַ����ֵ
            fwt_send[i] = rt.item[i].pfst.node[0];
            change =1;
        }
        if(pft->ft[i].snd != rt.item[i].psnd.node[0])
        {
            pft->ft[i].snd = rt.item[i].psnd.node[0];
            change =1;
        }
    }
    //printf("check fwt!!!,length of pft is %zu��lengthof fwt_send is %zu\n",sizeof(pft->ft),sizeof(fwt_send));

    if(1 == change)
    {
        mmsg_t msg_fwt;
        //��Ϣ��������Ϊת��������
        msg_fwt.mtype = MMSG_FT_DATA;
        //�ڵ������Ϊ���ڵ㣬��Ϊֵ֪ͨ���صĽ��̣�����Ҫ����Ŀ�Ľڵ�
        msg_fwt.node = pft->self;
        //memcpy(msg_fwt.data,pft->ft,sizeof(pft->ft));
        //rp_tmsg_2nl(sizeof(pft->ft) + sizeof(MADR), &msg_fwt);
        memcpy(msg_fwt.data,fwt_send,sizeof(char)*MAX_NODE_CNT);
        rp_tmsg_2nl(sizeof(char)*MAX_NODE_CNT + sizeof(MADR), &msg_fwt);
    }
}

void ritem_clear(int addr, ritem_t *pri)
{
	pri->dest = addr;
	rpath_clear(&pri->pfst);
	rpath_clear(&pri->psnd);
}

void rpath_copy(rpath_t* dest, rpath_t* src)
{
	dest->hop = src->hop;
	dest->flag = src->flag;
	dest->status = src->status;
	memset(dest->node, 0, sizeof(dest->node));
	memcpy(dest->node, src->node, src->hop*sizeof(MADR));
}
//Ϊһ��·��·����ֵ������������·�������ڵ�����ĵ�ַ
void rpath_set(rpath_t* path, int hop, MADR* pn, istat_t status, int flag)
{
	path->hop = hop;
	memcpy(path->node, pn, hop*sizeof(MADR));
	path->status = status;
	path->flag = flag;
}

int rp_init(int myaddr)
{
	int i;

	rt.self = myaddr;
	for(i = 0; i < MAX_NODE_CNT; i++)
	{
		ritem_clear(MR_IN2AD(i), &rt.item[i]);
		rlink_clear(&nt.fl[i]);
		rlink_clear(&nt.rl[i]);
	}
}

//route to neighbour node
//��ֵһ����src��·�ɲ���ԭ�����ڵ�src��·�ɱȽϣ������������֮
void ritem_nup(MADR src, U8* pn, int hop)
{
	int id = MR_AD2IN(src);
	//ָ�����nl���ڽӵ���е�һ����·
	rlink_t *nl = &nt.fl[id];

	ASSERT(NULL == pn && hop == 0);
	ASSERT(src = rt.item[id].dest);

	int sn;
	rpath_t path;
	//Ϊ���ڽӵ�src��·����path��ֵ��hop=1����һ���ڵ���src��״̬��nl->lstatus��flag=1���յ�����
	rpath_set(&path, 1, &src, nl->lstatus, 1);
	ritem_t *ri = &rt.item[id];
#if 0
	EPT(stderr, "node[%d]: enter to rapth_nup\n", *sa);
	rpath_show(ri->dest, &path);
	rpath_show(ri->dest, &ri->pfst);
	rpath_show(ri->dest, &ri->psnd);
#endif
    //rpath_up�����ȽϺ͸��µ�dest������·��������ֵΪ0˵��path���ţ�ri->pfst��path�滻
	if (0 == rpath_up(ri->dest, &path, &ri->pfst, &sn))
	{
	    //sn=0˵������·����һ���ڵ㲻ͬ
		if (0 == sn)
		{
#if 1
			if (path.node[0] == ri->pfst.node[0])
			{
				EPT(stderr, "node[%d]: ritem_nup the same next hop error\n", *sa);
			}
#endif
			if (1 == rpath_up(ri->dest, &path, &ri->psnd, &sn))
			{
				ASSERT(0);
			}
		}
	}
	else
	{
		if (0 == sn)
			rpath_up(ri->dest, &path, &ri->psnd, &sn);
	}
#if 0
	rpath_show(ri->dest, &ri->pfst);
	rpath_show(ri->dest, &ri->psnd);
#endif
}

/* route to nodes except neighbour */
void ritem_up(ritem_t *ri, MADR src, U8 hop, MADR* pn)
{
	if (hop == RP_INHOPS)
	{
		/* indicate that the src can not provide route to dest */
		EPT(stderr, "node[%d]: indicating no route to dest is processed in ritem_del()\n", *sa);
		return;
	}

	/* illegal parameters */
	if (hop > 0)
	{
		if ((NULL == pn || hop > MAX_HOPS))
		{
			EPT(stderr, "update routing item:illegal parameter\n");
			return;
		}
		else if (hop == MAX_HOPS)
		{
			EPT(stderr, "update routing item: too many hops\n");
			return;
		}
	}

	//��һ����ַ����Ŀ�ĵ�ַ���Լ�������ԣ�������
	if (pn[0] == *sa || ri->dest == *sa)
		return;

	/* loop in path */
	//�����ڵ�sa��·��pn�Ͻڵ㣬��˵�����º����ڻ�·��ֱ�ӷ���
	if (0 != rpath_cklp(*sa, hop, pn))
	{
		EPT(stderr, "node[%d]: find loop, dest=%d, src=%d, hop=%d, next=%d\n", *sa, ri->dest, src, hop, pn[0]);
		return;
	}
    //��sop��Я����item��ʼ��һ��path����
	rpath_t path;
	int sn;
	path.node[0] = src;
	memcpy(&path.node[1], pn, sizeof(MADR)*hop);
	path.hop = hop + 1;
	path.status = (LQ_ACTIVE == nt.fl[MR_AD2IN(src)].lstatus)? IS_ACTIVE : IS_UNSTABLE;
	path.flag = 1;
    //�Ƚ�path��pfst����path·�����ţ���ri-->pfst���Ϊpath
	if (0 == rpath_up(ri->dest, &path, &ri->pfst, &sn))
	{
	    //����һ���ڵ㲻ͬ
		if (0 == sn)
		{
#if 1
			if (path.node[0] == ri->pfst.node[0])
				EPT(stderr, "node[%d], ritem_up the same next hop error\n", *sa);
#endif
			if (1 == rpath_up(ri->dest, &path, &ri->psnd, &sn))
			{
				ASSERT(0);
			}
		}
	}
	//��path·�������Ѵ�������·������path��μ�·���Ƚϸ���
	else
	{
		if (0 == sn)
			rpath_up(ri->dest, &path, &ri->psnd, &sn);
	}

}

//��һ��������·��item���ڶ���������Я����item��֪ͨ�ڵ�
//���յ�sop�������ִﵽ��������ŵ���ִ�дκ�����
void ritem_del(ritem_t *ri, MADR src)
{
	int sn;
	rpath_t *path;

	EPT(stderr, "node[%d]: receive no route indication to %d from %d\n", *sa, ri->dest, src);
	/* second path */
	path = &ri->psnd;
	//·��·��path��״̬�ǿգ���src�ڵ���path�ϵĽڵ�
	//˵������·�ɻ�·��path�ǻ���src�ӽǵģ���Ӧ�ð���src�ڵ�
	if ((IS_NULL != path->status)&&(0 != rpath_cklp(src, path->hop, path->node)))
	{
	    //��·�����㣬·��״̬��ΪIS_NULL
		rpath_clear(path);
	}

	/* primary path */
	path = &ri->pfst;
	if ((IS_NULL != path->status)&&(0 != rpath_cklp(src, path->hop, path->node)))
	{
		rpath_clear(path);
		//����ʱ����·��û����գ��򽫱���·�ɱ��Ϊ����·��
		if (IS_NULL != ri->psnd.status)
		{
			if (2 != rpath_up(ri->dest, &ri->psnd, &ri->pfst, &sn))
			ASSERT(0 == sn);
		}
	}

	//update_fwt(ri);
}

#ifndef _MR_TEST
int rp_tmsg_2nl(int len, mmsg_t *msg)
{
	int rval = 0;
	if (nl_qid != -1)  {
		rval = msgsnd(nl_qid, (void *)msg, len, 0);
		//EPT(stderr, "%s: msgsnd() write msg at qid %d\n", qinfs[re_qin].pname, nl_qid);
		if ( rval < 0 ) {
			EPT(stderr, "%s: msgsnd() write msg failed,errno=%d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
		}
	}
	else {
		rval = -1;
		EPT(stderr, "can not get netlayer qid\n");
	}
	return rval;
}
#else
//����sop��
int rp_tmsg_2nl(int len, mmsg_t *msg)
{
	int rval = 0;
	if (mt_qid != -1)  {
		rval = msgsnd(mt_qid, (void *)msg, len, 0);
//		EPT(stderr, "%s: msgsnd() write msg at qid %d\n", qinfs[re_qin].pname, mt_qid);
		if ( rval < 0 ) {
			EPT(stderr, "%s: msgsnd() write msg failed,errno=%d[%s]\n", qinfs[re_qin].pname, errno, strerror(errno));
		}
	}
	else {
		rval = -1;
		EPT(stderr, "can not get mrtest qid\n");
	}
	return rval;
}
#endif


//if the return value is 0, it means that this item is not filled in sop message
//��itemд����Ϣ����
int ritem_sopget(ritem_t *ri, U8 *buf, int buflen)
{
	rpath_t *rp = &ri->pfst;
	int len = 0;

	if (WH_RP_VALD(rp->status))
	{
        //IS_ACTIVE == status || IS_UNSTABLE == status
		*(MADR *)buf = ri->dest;
		len += sizeof(MADR);

		ASSERT(rp->hop > 0 && rp->hop < MAX_HOPS);
		*(buf + len++) = rp->hop;
		memcpy(buf+len, rp->node, rp->hop*sizeof(MADR));
		len += rp->hop*sizeof(MADR);
	}
	else if (IS_EXPIRE == rp->status) {//time dead
		*(MADR *)buf = ri->dest;
		len += sizeof(MADR);
		/* setting this value means no route to the dest */
		*(buf + len++) = RP_INHOPS;
	}
	else {
		/* do nothing */
	}

	if (len > buflen) {
		EPT(stderr, "ritem_sopget: less message buffer\n");
		len = -1;
	}
	return len;
}


//check_loop,���src�Ƿ�Ϊ·��pn�ϵĽڵ㣬����0���ǣ�˵���ǻ�·��������1�ǣ�˵�����ڻ�·��
int rpath_cklp(MADR src, U8 hop, MADR *pn)
{
	int i;
	for (i = 0; i < hop; i++)
	{
		if (src == pn[i])
			break;
	}
	if (i >= hop)
		return 0;
	else
		return 1;
}

/*
 check path joint type
 return value:
	0:  node disjoint
	1:  node joint, but link disjoint
	2:  link joint
 */
int rpath_ckjt(rpath_t* p1, rpath_t *p2)
{
	int i, j;
	MADR n1;
	int rval = 0;

	for (i = 0; i < p1->hop-1; i++) {
		n1 = p1->node[i];
		for (j = 0; j < p2->hop-1; j++) {
			if (n1 == p2->node[j])
				break;
		}
		if (j < p2->hop-1) {
			break;
		}
	}

	if ( j >= p2->hop-1) {
		ASSERT(i >= p1->hop-1);
		ASSERT(rval == 0);
	}
	else {
		ASSERT(i < p1->hop-1);
		if (p1->node[i+1] == p2->node[j+1])
			rval = 2;
		else
			rval = 1;
	}
	return rval;
}

void ritem_show(ritem_t *ri)
{
	if (ri->pfst.status == IS_NULL) {
		//EPT(stderr, "node[%d]:no route to %d\n", *sa, ri->dest);
		return;
	}
	else {
		EPT(stderr, "node[%d]: 1st route to %d, status=%d, h=%d, n=%d\n", *sa, ri->dest, ri->pfst.status, ri->pfst.hop, ri->pfst.node[0]);
	}

	if (ri->psnd.status != IS_NULL) {
		EPT(stderr, "node[%d]: 2nd route to %d, status=%d, h=%d, n=%d\n", *sa, ri->dest, ri->psnd.status, ri->psnd.hop, ri->psnd.node[0]);
	}
}

void rpath_show(MADR dest, rpath_t* rp)
{
	EPT(stderr, "node[%d]: route to %d, s=%d, h=%d, n=%d\n", *sa, dest, rp->status, rp->hop, rp->node[0]);
}

void rt_show()
{
	int i;
	ritem_t *ri;
	printf("route table : =========================\n");
	for (i = 0; i < MAX_NODE_CNT; i++) {
		ri = &rt.item[i];
		ritem_show(ri);
	}
}

void nt_show()
{
	int i;
	printf("neighbour table : =========================\n");
	for (i = 0; i < MAX_NODE_CNT; i++) {
		if (nt.fl[i].lstatus == LQ_NULL)
			continue;

		EPT(stderr, "node[%d]: link to node %d, status=%d, cnt=%d\n", *sa, MR_IN2AD(i), nt.fl[i].lstatus, nt.fl[i].rcnt);
	}

}

void rlink_clear(rlink_t *lk)
{
	lk->lstatus = LQ_NULL;
	lk->lstatus = LQ_NULL;
	lk->rcnt = 0;
}

void rlink_inc(MADR nb)
{
	nt.fl[MR_AD2IN(nb)].rcnt++;//�ھӱ����·�յ��İ���+1
}

void rlink_dec(MADR nb)
{
	nt.fl[MR_AD2IN(nb)].rcnt--;
}

/*
 up=0, message drive, remain rcnt
 up=1, timer drive, clear rcnt
 */
//�����յ��İ���������·״̬�ĸ���
int rlink_fsm(MADR nb, int up)
{
	int change = 0;
	//ni���ھӱ�nt�е�nb�ڵ�ĳ���·
	rlink_t *ni = &nt.fl[MR_AD2IN(nb)];
	//��·��ǰ״̬��Ϊ��״̬lold��������
	lstat_t lold =  ni->lstatus;

	//��������·״̬ת�ƣ�ֻ��������rcnt�����յ�����

	//����ʱ�����Ϊ����·
	if (LQ_NULL == ni->lstatus)
	{
	    //�յ�����·�����ڵ���5��״̬������·��Ϊ��Ծ��·
		if (ni->rcnt >= LM_NUL2ACT) {//LM_NUL2ACT = 5
			ni->lstatus = LQ_ACTIVE;//active link to a node
			change = 1;
		}
		//�յ�����·�����ڵ���1��״̬������·��Ϊ���ȶ���·
		else if (ni->rcnt >= LM_NUL2UNS) {
			ni->lstatus = LQ_UNSTABLE;
			change = 1;
		}
		//������������յ�����·��Ϊ0
		else {
			ASSERT(LM_NUL2NUL == ni->rcnt);
		}
	}
	//����ʱ���Ϊ�Ѿ����ڻ�Ծ��·
	else if (LQ_ACTIVE == ni->lstatus) {
	    //���յ�����·��С�ڵ���0����·״̬�ɻ�Ծ��Ϊ��ʱ
		if (ni->rcnt <= LM_ACT2EXP) {
			ni->lstatus = LQ_EXPIRE;
			change = 1;
		}
		//���յ�����·��С��4������·״̬�ɻ�Ծ��Ϊ���ȶ�
		else if (ni->rcnt < LM_ACT2ACT) {
			ni->lstatus = LQ_UNSTABLE;
			change = 1;
		}
		//������������յ�����·�����ڵ���4
		else {
			ASSERT(LM_ACT2ACT <= ni->rcnt);
		}
	}
	//����ʱ�����Ϊ��·���ȶ�
	else if (LQ_UNSTABLE == ni->lstatus) {
	    //���յ�����·�����ڵ���4�����ɲ��ȶ���Ϊ��Ծ
		if (ni->rcnt >= LM_UNS2ACT) {
			ni->lstatus = LQ_ACTIVE;
			change = 1;
		}
		//���յ�����·�����ڵ���0�����ɲ��ȶ���Ϊ��ʱ
		else if (ni->rcnt <= LM_UNS2EXP) {
			ni->lstatus = LQ_EXPIRE;
			change = 1;
		}
		//����������Դ���0С��4
		else {
			ASSERT(LM_UNS2EXP < ni->rcnt && LM_UNS2ACT > ni->rcnt);
		}
	}
	//����ʱ�����Ϊ��ʱ
	else if (LQ_EXPIRE == ni->lstatus) {
	    //���յ�����·�����ڵ���5�� ���ɳ�ʱ��Ϊ��Ծ
		if (ni->rcnt >= LM_EXP2ACT) {
			ni->lstatus = LQ_ACTIVE;
			change = 1;
		}
		//���յ�����·�����ڵ���1�����ɳ�ʱ��Ϊ���ȶ�
		else if (ni->rcnt >= LM_EXP2UNS) {
			ni->lstatus = LQ_UNSTABLE;
			change = 1;
		}
		//������������յ�Ϊ0���Ҵӳ�ʱ��Ϊ����·
		else {
			ASSERT(ni->rcnt == LM_EXP2NUL);
			ni->lstatus = LQ_NULL;
			change = 1;
		}
	}
	else {
		EPT(stderr,"rlink_fsm():error status\n");
	}
    //�������·״̬ת�ƹ����з����˸ı�
	if (1 == change)
	{
	    //����sop������ĸ��£��Ҹ��º��״̬����ԭ״̬��lold������ԭԭ״̬
		if (0 == up && lold > ni->lstatus)
		{
			ni->lstatus = lold;
			change = 0;
		}
		//����ԭ״̬��ֵ����·��״̬������ǰ״̬��Ϊ���º��״̬��
		else
		{
			ni->lold = lold;
			EPT(stderr, "node[%d]: link to neighbour %d changed org=%d, now=%d , cnt=%d\n", *sa, nb, lold, ni->lstatus, ni->rcnt);
		}
	}
	//����Ƕ�ʱ������ĸ��£����յ��������㣨ÿ��6s��
	if (1 == up)
		ni->rcnt = 0;
	return change;
}
//��鲢�Ƚϸ���һ��·�ɱ�
void ritem_fsm(ritem_t *ri, int up)
{
	int sn;
    //�ֱ�������·��·����״̬������״̬ת��
    //rpath_fsm������1˵��״̬�ı䣨ֻ�ܱ�Ϊ����״̬��������˵��״̬����
    //ֻҪ����·����һ�����£�����1��������ºͷ���ת����
    //if(rpath_fsm(&ri->psnd, up) || rpath_fsm(&ri->pfst, up))
       // update_fwt(ri);
	rpath_fsm(&ri->psnd, up);
	rpath_fsm(&ri->pfst, up);
    //���μ�·����Ծ���߲��ȶ�,��Ƚ�������·�ʹμ���·�������Ƿ��滻
	if (WH_RP_VALD(ri->psnd.status))
	{
		rpath_up(ri->dest, &ri->psnd, &ri->pfst, &sn);
		if (sn == 1)
		{
			EPT(stderr, "node[%d]: error occurs in routing table maintaining\n", *sa);
			rpath_clear(&ri->psnd);
		}
	}
}

//compare path, p1 must be valid path
//return value:
//0: equal, 1:p1 is better, 2: p2 is better
//sn:
//0: another next hop, 1:same next hop
//�Ƚ�����·��·�����ȱȽ�״̬���ٱȽ�����������1˵��p1���ţ�����2˵��p2����
//��������·��һ���ڵ���ͬ����sn=1������sn=0
int	rpath_comp(rpath_t *p1, rpath_t *p2, int *sn)
{
	int rval;

	if (p1->status == p2->status)
	{
		ASSERT(p2->hop > 0 && p2->hop <= MAX_HOPS);
		if (p1->hop < p2->hop)
		{
			rval = 1;
		}
		else if (p1->hop > p2->hop)
		{
			rval = 2;
		}
		else
		{
			rval = 0;
		}
	}
	else if (p1->status > p2->status)
	{
		rval = 1;
	}
	else
	{
		rval = 2;
	}
#if 1
	if (p1->hop > 0 && p2->hop > 0)
	{
		if (p1->node[0] == p2->node[0])
			*sn = 1;
		else
			*sn = 0;
	}
	else
	{
		*sn = 1;
	}
#else

#endif
	return rval;
}
//compare new and original route, and update if better
//	node	next hop
//return value:
//	0:		new is better(update), the original route is copied to new
//	1:		org is better, do nothing
//	2:      error
//�Ƚ�����·��·��������
int rpath_up(MADR node, rpath_t *new_p, rpath_t *org, int *sn)
{
	int comp = -1;
	int rval = 0;
	rpath_t path;
    //���μ�·��·����״̬���ǲ��ȶ����߼�����ã���ֱ�ӷ���2������Ҫ���бȽ�
	if (!WH_RP_VALD(new_p->status))
	{
		EPT(stderr, "node[%d]: new route to %d must be active or unstable, s=%d\n", *sa, node, new_p->status);
		rval = 2;
		return rval;
	}
    //���path��״̬��ΪIS_NULL
	rpath_clear(&path);
    //�Ƚ�����·��·�����ȱȽ�״̬���ٱȽ�����������1˵��new���ţ�����2˵��org����
    //��������·��һ���ڵ���ͬ����sn=1������sn=0
	comp = rpath_comp(new_p, org, sn);
    //��һ���ڵ㲻ͬ����Ӧԭ�����ڵ�node��һ�����������ѡ�����·����״̬����
	if (0 == *sn)
	{
	    //��������·new_p���ţ���������·��������·����
		if (1 == comp)
		{
			if (WH_RP_VALD(org->status))
			{
				rpath_copy(&path, org);
			}
			rpath_copy(org, new_p);
			rpath_copy(new_p, &path);
			ASSERT(rval == 0);
		}
		//��������·���ţ��򷵻�ֵ��Ϊ1��������
		else if (2 == comp)
		{
			rval = 1;
		}
		else
		 {
			ASSERT(0 == comp && 0 == rval);
			/* another route to dest with equal hops */
		}
	}
	//����һ���ڵ���ͬ����Ӧԭ��Ҳ��һ�����������·ֱ�ӱ��Ϊ������·���൱�����·��״̬
	else
	{
		new_p->flag = org->flag + 1;
		rpath_copy(org, new_p);
	}
	return rval;
}

//up=1, timer drive, clear flag
//up=0, message drive, remain flag
//����flag����·״̬����·��·��״̬��ת��
//ֻ�������״̬ת�ƣ����������״̬ת��
int rpath_fsm(rpath_t *rp, int up)
{
    //��·����·����һ���ڽӵ��ַ
	MADR next = rp->node[0];
	//�����ڽӵ���ڽӣ�������·
	rlink_t *lk = &nt.fl[MR_AD2IN(next)];
	//�ɵ�·��·��״̬
	istat_t iold = rp->status;
    //��·��·��״̬Ϊ��
	if (IS_NULL == rp->status)
	{
	    //����·��flag���ڵ���1�����յ���sop��
		if (rp->flag >= IM_NUL2ACT)
		{
		    //����Ӧ���ڽ���·״̬Ϊ��Ծ��������·��·��״̬��Ϊ��Ծ
			if (LQ_ACTIVE == lk->lstatus)
				rp->status = IS_ACTIVE;
            //����Ӧ���ڽ���·״̬Ϊ���ȶ���������·��·��״̬��Ϊ���ȶ�
			else
			{
				ASSERT(lk->lstatus == LQ_UNSTABLE);
				rp->status = IS_UNSTABLE;
			}
		}
	}
	//��·��·��״̬Ϊ��ʱ������յ���sop������ͬ�ϴ�������·��״̬��Ϊ��
	else if (IS_EXPIRE == rp->status)
	{
		if (rp->flag >= IM_EXP2ACT)
		{
			if (LQ_ACTIVE == lk->lstatus)
				rp->status = IS_ACTIVE;
			else
			{
				ASSERT(lk->lstatus == LQ_UNSTABLE);
				rp->status = IS_UNSTABLE;
			}
		}
		else {
			rp->status = IS_NULL;
		}
	}
	//��·��״̬Ϊ���ȶ�
	else if (IS_UNSTABLE == rp->status)
	{
	    //���û���յ�sop������·��״̬��Ϊ��ʱ
		if (rp->flag < IM_ACT2EXP)
		{
			rp->status = IS_EXPIRE;
		}
		//����Ӧ���ڽ���·״̬Ϊ��Ծ��������·��·��״̬��Ϊ��Ծ
		else if (LQ_ACTIVE == lk->lstatus)
		{
			rp->status = IS_ACTIVE;
		}
	}
	//�������������·��״̬��Ծ��
	else
	{
		ASSERT(rp->status == IS_ACTIVE);
		//���û���յ�sop��������Ϊ��ʱ
		if (rp->flag < IM_ACT2EXP)
		{
			rp->status = IS_EXPIRE;
		}
		//���յ�sop��������·״̬Ϊ���ȶ�����·��״̬��Ϊ���ȶ�
		else if (LQ_UNSTABLE== lk->lstatus)
		{
			rp->status = IS_UNSTABLE;
		}
	}

	int change = 0;
	if (iold != rp->status)
	{
	    //����״̬������״̬�ұ��θ�����msg���µģ���timer���£����򱣳־�״̬
	    //���磬sop������check��up=0���������µ�״̬���ھ�״̬���򲻸���
		if (iold > rp->status && 0 == up)
			rp->status = iold;
		//��������ȷ������״̬���£�������change=1
		else
			change = 1;
	}
	//���upΪ1��˵����timer����ĸ��£���flag��0����ʾ�µ�check�׶�û���յ�sop��
	if (1 == up)
		rp->flag = 0;

	return change;
}

void signal_show(int signal)
{
    printf("Signal = %d\n",signal);
    nt_show();
    rt_show();
    return;
}


