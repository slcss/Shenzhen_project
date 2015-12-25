#include "../mr_common.h"
#include "hm_test.h"

static key_t ht_qkey = -1;
static int   ht_qid = -1;

static mt_tshare_t  share = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER
};

static pthread_t mrx_tid = -1;
static pthread_t time_tid = -1;
static int rqs[MAX_NODE_CNT];   /* ��¼�ڵ����Ϣ����id ���ִ���ڵ�� */
static U8  top[MAX_NODE_CNT][MAX_NODE_CNT];

U8 BS[MAX_CFS_PSF][MAX_NODE_CNT+1];   /* ��¼ʱ϶ռ���������һ�����ֶ�Ӧʱ϶�ţ�ÿ��ʱ϶���Ա�����ڵ�ռ�ã��ڶ������ֶ�Ӧ�ڵ��,BS[][0]��¼��ʱ϶�ڵ��� 11.03 */
U8 node_BS_flag[MAX_NODE_CNT+1];   /* ��¼ÿ���ڵ��Ƿ��һ��ռ��ʱ϶ [1]��Ӧ1�Žڵ� [0]���� 0��Ӧûռ��*/
U8 node_BS_backup[MAX_NODE_CNT+1];   /* ��¼ÿ���ڵ�ռ�õ�ʱ϶ [1]��Ӧ1�Žڵ� [0]���� */

mmsg_t base[MAX_NODE_CNT];  /* ��¼����֡ ���ִ���ڵ�� */
int num[MAX_NODE_CNT];     /* ��¼msg���� ���ִ���ڵ�� */

struct itimerval new_value;
U8 flag = 0;	/* ��һ���յ�����֡�ı�־ */
U8 count = 0;	/* ʱ϶���� */

sem_t send_begin;
pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;    /* for BS[][] & node_num[] */
pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;    /* for base[] & num[] */
pthread_mutex_t c = PTHREAD_MUTEX_INITIALIZER;	  /* for mid_manage[] */

FILE* fp;

mid_manage_t *mid_manage[32];  /* �м̱��������ڹ���ṹ�� ָ������ */
U8  first[32];   /* ��עĳ�ڵ��Ӧ��״̬�ı�֡ά���߳��Ƿ���  0δ���� 1���� */
 
int main(int argc, char* argv[])
{
	int len;
	int j;
	int rval, stop;
	void *result = NULL;	
	
	fp = fopen("center", "w");

	memset(BS, 0, sizeof(BS));
	memset(node_BS_flag, 0, sizeof(node_BS_flag));
	memset(node_BS_backup, 0, sizeof(node_BS_backup));
	memset(base, 0, sizeof(base));
	memset(num, 0, sizeof(num));
	memset(first, 0, sizeof(first));
	sem_init(&send_begin, 0, 0);

	EPT(fp, "%s: main thread id = %lu\n", argv[0], pthread_self());

	if (argc != 2) {
		EPT(fp, "main: number of input paras is not correct\n");
		rval = 1;
		goto process_return;
	}

	/* this thread obtain all queues for rx and tx */
	rval = ht_queues_init();
	if (rval != 0)
	{
		EPT(fp, "main: creating msg queue fails");
		rval = 1;
		goto process_return;
	}

	/* initialize topology */
	rval = ht_tinit(argv[1]);
	if (rval != 0) {
		EPT(fp, "main: errors occur in topology initialization\n");
		rval = 2;
		goto process_return;
	}

	/* create receiving msg thread */
	rval = pthread_create(&time_tid, NULL, ht_timer_thread, NULL);
	if (rval != 0) {
		EPT(fp, "main: can not open create msg receiving thread\n");
		rval = 3;
		goto process_return;
	}

	/* create receiving msg thread */
	rval = pthread_create(&mrx_tid, NULL, ht_qrv_thread, &ht_qid);
	if (rval != 0) {
		EPT(fp, "main: can not open create msg receiving thread\n");
		rval = 4;
		goto process_return;
	}

	stop = 0;
	pthread_mutex_lock(&share.mutex);
	while(0 == stop)
	{
		EPT(fp, "main: waiting for the exit of sub threads\n");
		pthread_cond_wait(&share.cond, &share.mutex);

		/* do something */
	}
//	EPT(fp, "%s: certain thread quit\n", argv[0]);
	pthread_mutex_unlock(&share.mutex);

process_return:
	mr_queues_delete();
	exit(rval);
}

int ht_queues_init()
{
	int rval = 0;

	ht_qkey = ftok(PATH_CREATE_KEY, SN_HMTEST);
	ht_qid = msgget(ht_qkey, IPC_CREAT|QUEUE_MODE);
	if (ht_qid == -1) {
		EPT(fp, "ht_queues_init: can not get queue\n");
		rval = 1;
	}

	return rval;
}

int ht_queues_delete()
{
	if (ht_qid != -1);
		msgctl(ht_qid, IPC_RMID, NULL);
	return 0;
}

void* ht_qrv_thread(void *arg)
{
	int qid, rcnt;
	mmsg_t rx_msg;
	int rval, stop;

	pthread_detach(pthread_self());

	qid = *(int *)arg;
	EPT(fp, "ht_qrv_thread: msg receiving thread id = %lu\n", pthread_self());
//	EPT(fp, "hm_test: enter queue receiving thread, rqueue %d\n", qid);

	if (qid < 0) {
		EPT(fp, "ht_qrv_thread: wrong receive queue id %d\n", qid);
		rval = 1;
		goto thread_return;
	}

	rval = 0;
	stop = 0;
	while(0 == stop)
	{
		memset(&rx_msg, 0, sizeof(rx_msg));
//		EPT(fp, "hm_test: reveive msg queue at qid %d\n", qid);
		rcnt = msgrcv(qid, &rx_msg, MAX_DATA_LENGTH, 0, 0);
		if (rcnt < 0) {
			if (EIDRM != errno) {
				EPT(fp, "ht_qrv_thread: error in receiving msg, no:%d, meaning:%s\n", errno, strerror(errno));
			}
			else {
				EPT(fp, "ht_qrv_thread: quit msg receiving thread\n");
			}
			rval = 2;
			break;
		}

		rval = ht_rmsg_proc(&rx_msg, rcnt);
		if (rval != 0) {
			/* report error */
			EPT(fp, "ht_qrv_thread: error occurs, the node of rx_msg  = %d\n", rx_msg.node);
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

int ht_rmsg_proc(mmsg_t *msg, int cnt)
{
	MADR node;
	int qid;
	int i,j, rval = 0;
	lm_packet_t package;
	LM_neighbor_map_t slot;
	//U8 node_count;  /* ��������ʱ���ʱ϶���� */

	node = msg->node;
	if (msg->mtype == MMSG_MT_RQID)  /* ��¼�ڵ����Ϣ���к� */
	{
		//EPT(fp, "ht_rmsg_proc: rcv msg queue id from node %d\n", node);
		qid = *(int*)msg->data;

		if (MR_ISUNI(node)) 
		{
			rqs[node-1] = qid;
			//EPT(fp, "ht_rmsg_proc: rqs[%d] = %d\n", node-1, qid);
		}
		else 
		{
			EPT(fp, "ht_rmsg_proc: node error\n");
			rval = 1;
		}
	}
	else if(msg->mtype == 4)  /* ������ʱ϶�� */
	{		
		memset(&package, 0, sizeof(package));
		memcpy(&package, msg->data, cnt-1);
		if(package.type == 0xff)
		{
			//EPT(fp, "ht_rmsg_proc: package.type = 0xff\n");

#if 0
			/* �����յ���״̬�ı�֡����Ӧ���� ���� */
			if(first[node-1] == 0)
			{
				first[node-1] = 1;

				//pthread_mutex_lock(&c);
				mid_manage[node-1] = malloc(sizeof(mid_manage_t));	/* �� �м̱��������ڹ���ṹ�� ����ָ�� */
				memset(mid_manage[node-1], 0, sizeof(mid_manage_t));	/* ��ʼ�� �м̱��������ڹ���ṹ�� */
				
				sem_init(&(mid_manage[node-1]->sem), 0, 0);   /* ��ʼ����Ӧ�ź�����������Ӧ��ά���߳� */
				mid_manage[node-1]->node = node;                   /* ��¼ʱ϶�� */
				pthread_create(&(mid_manage[node-1]->id), NULL, ht_mid_manage_thread, (void *)&mid_manage[node-1]->node);	
				
				
				//pthread_mutex_unlock(&c);
				EPT(stderr, "ht_rmsg_proc: ht_mid_manage_thread begin\n");
			}
			
			sem_post(&(mid_manage[node-1]->sem));   /* ���ܷ��������if������ */
#endif
			
			return rval;
		}
		//EPT(fp, "ht_rmsg_proc: rcv slot table from node %d\n", node);
		//EPT(stderr, "ht_rmsg_proc: queue 4 package.Hsn from node %d = %d\n", node, package.Hsn);

		memset(&slot, 0, sizeof(slot));
		memcpy(&slot, package.data, package.len-4);
		i = slot.localBS;

		/* ��¼�ڵ��BSռ�ã�֧�ֲ�ͬ�ڵ�ռ��ͬһ��ʱ϶ 11.04/�Ż��޸� 11.05 */
		pthread_mutex_lock(&a);  /* �ӻ����� */
		if(!node_BS_flag[node])
		{
			node_BS_flag[node] = 1;
			node_BS_backup[node] = i;
				
			BS[i][node] = 1;
			BS[i][0]++;
			EPT(stderr, "ht_rmsg_proc: BS[%d][%d] = %d\n", i, node, node);			
		}
		else if(node_BS_backup[node] != i)
		{			
			BS[node_BS_backup[node]][node] = 0;
			BS[node_BS_backup[node]][0]--;
		
			node_BS_backup[node] = i;
		
			BS[i][node] = 1;
			BS[i][0]++;
			EPT(stderr, "ht_rmsg_proc: BS[%d][%d] = %d\n", i, node, node);		
		}
		else
		{			
			EPT(stderr, "ht_rmsg_proc: BS[%d][%d] = %d\n", i, node, node);	
		}
		pthread_mutex_unlock(&a);			
	}
	else if(msg->mtype == 5)  /* ��������֡���� */
	{
		//EPT(fp, "ht_rmsg_proc: rcv service frame from node %d\n", node);
		//EPT(stderr, "ht_rmsg_proc: queue 5 package.Hsn from node %d = %d\n", node, ((lm_packet_t *)msg->data)->Hsn);
		
		pthread_mutex_lock(&b);       /* �ӻ����� */
		memset(&base[node-1], 0, sizeof(mmsg_t));
		memcpy(&base[node-1], msg, sizeof(long)+cnt);  
		num[node-1] = cnt;

#if 0
		for(i=0;i<((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->num;i++)
		{
			EPT(fp, "node %d : BS[%d].flag = %d\nBS_ID = %d\nstate = %d\nclock_lv = %d\n", node, i, 
				((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].flag,
				((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].BS_ID,
				((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].state,
				((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].clock_lv);
		}
#endif	
		pthread_mutex_unlock(&b);	

		if(flag == 0)
		{
			sem_post(&send_begin);
			flag = 1;
		}
	}
	else if(msg->mtype == 10)  /* ����֪ͨ */
	{
		EPT(stderr, "ht_rmsg_proc: rcv quit frame from node %d\n", node);

		rqs[node-1] = -1;   /* �ǰ���-1���жϵ� */
		
		pthread_mutex_lock(&b);       /* �ӻ����� */
		memset(&base[node-1], 0, sizeof(mmsg_t));
		num[node-1] = 0;
		pthread_mutex_unlock(&b);	

		/* �������޸� 11.04/�Ż� 11.05 */		
		pthread_mutex_lock(&a);
		BS[node_BS_backup[node]][node] = 0;
		EPT(stderr, "BS[%d][%d] = %d\n", node_BS_backup[node], node, BS[node_BS_backup[node]][node]);
		BS[node_BS_backup[node]][0]--;
		node_BS_backup[node] = 0;
		node_BS_flag[node] = 0;				
		pthread_mutex_unlock(&a);		
	}
	else
		EPT(stderr, "ht_rmsg_proc: msg->mtype = %ld\n", msg->mtype);
	return rval;
}

int ht_tinit(char *name)
{
	int i, j;
	int rval = 0;
	FILE *fp = NULL;

	fp = fopen(name, "a+");
	if (NULL == fp) {
		rval = 1;
		goto fexit;
	}

	/* clear */
	for (i = 0; i < MAX_NODE_CNT; i++) {
		rqs[i] = -1;
		for (j = 0; j < MAX_NODE_CNT; j++)
			top[i][j] = 0;
	}

	while(!feof(fp)) {
		fscanf(fp, "%d %d", &i, &j);
		if (MR_ISUNI(i) && MR_ISUNI(j))
			top[i-1][j-1] = 1;
	}

	ht_show_top();
fexit:
	if (NULL != fp)
		fclose(fp);
	return rval;
}

void ht_show_top()
{
	int i,j;
	int vld = 0;

	for (i = 0; i < MAX_NODE_CNT; i++)
	{
		vld = 0;
		for (j = 0; j < MAX_NODE_CNT; j++)
		{
			if (0 == top[i][j])
				continue;
			vld = 1;
			EPT(stderr, "t[%d][%d]=%d  ", i+1, j+1, top[i][j]);
		}
		if (1 == vld)
			EPT(stderr, "\n");
	}
}

void ht_show_rqs()
{
	int i;
	for (i = 0; i < MAX_NODE_CNT; i++) 
	{
		EPT(fp, "%3d %d\n", i+1, rqs[i]);
	}
}

void* ht_timer_thread(void *arg)
{
	int i,j,rval;
	U8 node;
	U8 BS_count = 0;  /* ���ڷ���ʱ��Ľڵ���� */

	new_value.it_value.tv_sec = 0;
	new_value.it_value.tv_usec = 9000;
	new_value.it_interval.tv_sec = 0;
	new_value.it_interval.tv_usec = 9000;

	sem_wait(&send_begin); /* ��һ���յ�����֡ */

	/* �ȷ���һ�Σ���BS0 */
	for(i = 1; i <= MAX_NODE_CNT; i++)
	{
		pthread_mutex_lock(&a);	
		if(BS_count == BS[count][0])
		{
			pthread_mutex_unlock(&a);
			break;
		}
		node = i;
		pthread_mutex_unlock(&a);

		if (BS[count][i])
		{
			EPT(fp, "timer_proc: BS[%d][%d] = %d\n", count, i, node);
			
			for (j = 0; j < MAX_NODE_CNT; j++)
			{
				if (j == node-1 || top[node-1][j] == 0)   /* ֻ���Ƿ��ͽڵ���һ�е����� */
					continue;
				//EPT(fp, "timer_proc: rqs[%d] = %d\n", j, rqs[j]);
				if (rqs[j] == -1)   /* ����᲻���л�������� ******************************************************/
				{
					//EPT(fp, "timer_proc: can not get the qid of rx node %d\n", j+1);
					continue;
				}

				pthread_mutex_lock(&b);	
				rval = msgsnd(rqs[j], (void *)&base[node-1], num[node-1], 0);				
				pthread_mutex_unlock(&b);
				EPT(fp, "timer_proc: msgsnd() write msg at qid %d of node %d\n", rqs[j], j+1);
				if ( rval < 0 )
				{
					EPT(fp, "timer_proc: msgsnd() write msg failed,errno=%d[%s]\n", errno, strerror(errno));
				}
			}

			/* ���½ڵ��� */
			BS_count++;			
		}	
	}
	
	signal(SIGALRM, ht_timer_proc);
	rval = setitimer(ITIMER_REAL, &new_value, NULL);
	if (-1 == rval)
	{/* failure */
		EPT(fp, "timer_thread: error occurs in setting timer %d[%s]\n", errno, strerror(errno));
		goto thread_return;
	}
	else
	{/* success */
		rval = 0;
	}

	/* ��ͣ!! */
	while(1)
		pause();
	EPT(stderr, "timer_thread: pause fail!\n");

thread_return:
	sleep(1);
	pthread_exit((void *)&rval);
}

void ht_timer_proc(int signo)
{
	int i,j;
	int	rval = 0;
	U8 node;
	U8 BS_count = 0;  /* ���ڷ���ʱ��Ľڵ���� */

	for(i = 1; i <= MAX_NODE_CNT; i++)
	{
		pthread_mutex_lock(&a);	
		if(BS_count == BS[count][0])
		{
			pthread_mutex_unlock(&a);
			break;
		}
		node = i;
		pthread_mutex_unlock(&a);

		if (BS[count][i])
		{
			EPT(fp, "timer_proc: BS[%d][%d] = %d\n", count, i, node);
			
			for (j = 0; j < MAX_NODE_CNT; j++)
			{
				if (j == node-1 || top[node-1][j] == 0)   /* ֻ���Ƿ��ͽڵ���һ�е����� */
					continue;
				//EPT(fp, "timer_proc: rqs[%d] = %d\n", j, rqs[j]);
				if (rqs[j] == -1)   /* ����᲻���л�������� ******************************************************/
				{
					//EPT(fp, "timer_proc: can not get the qid of rx node %d\n", j+1);
					continue;
				}

				pthread_mutex_lock(&b);
#if 0
				for(i=0;i<((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->num;i++)
				{
					EPT(fp, "BS[%d].flag = %d\nBS_ID = %d\nstate = %d\nclock_lv = %d\n", i, 
						((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].flag,
						((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].BS_ID,
						((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].state,
						((service_frame_t *)((mac_packet_t *)((lm_packet_t *)((mmsg_t *)&base[node-1])->data)->data)->data)->BS[i].clock_lv);
				}
#endif		
				rval = msgsnd(rqs[j], (void *)&base[node-1], num[node-1], 0);				
				pthread_mutex_unlock(&b);
				EPT(fp, "timer_proc: msgsnd() write msg at qid %d of node %d\n", rqs[j], j+1);
				if ( rval < 0 )
				{
					EPT(fp, "timer_proc: msgsnd() write msg failed,errno=%d[%s]\n", errno, strerror(errno));
				}
			}

			/* ���½ڵ��� */
			BS_count++;			
		}	
	}
	
	count++;
	if(count == 32)
		count = 0;
	//EPT(fp, "timer_proc: count = %d\n", count);
}



#if 0
void *ht_mid_manage_thread(void *arg)
{
	U8 node = *(U8 *)arg;
	U8 i;
	
	while(1)
	{
		sem_wait(&(mid_manage[node-1]->sem));		

		if(mid_manage[node-1]->timeup == 1)   /* ��ʱ������ */
		{
			rqs[node-1] = -1;   /* �ǰ���-1���жϵ� */

			pthread_mutex_lock(&b);
			memset(&base[node-1], 0, sizeof(mmsg_t));
			num[node-1] = 0;
			pthread_mutex_unlock(&b);
			
			for(i = 0; i<32; i++)
			{
				pthread_mutex_lock(&a);
				//EPT(stderr, "BS[%d] = %d\n", i, BS[i]);
				if(BS[i] == node)
				{
					BS[i] = 0;
					EPT(stderr, "BS[%d] = %d\n", i, BS[i]);
					pthread_mutex_unlock(&a);
					break;
				}
				pthread_mutex_unlock(&a);				
			}
			
			mid_manage[node-1]->timeup = 0;   /* ��ʱ�������ı������ */
			mid_manage[node-1]->node = 0;
			sem_destroy(&(mid_manage[node-1]->sem));

			first[node-1] = 0;  /* �˽ڵ��Ӧ��״̬�ı�֡ά���̹߳ر� */

			//EPT(stderr, "node %d 111\n", node);
			
			break;
		}

		else
		{
			if(mid_manage[node-1]->timeup == 0)
				pthread_cancel(mid_manage[node-1]->timer);    /* �������ڶ�ʱ������رն�ʱ�� */

			pthread_create(&mid_manage[node-1]->timer, NULL, ht_mid_life_thread, arg);   /* �޸���Ͽ�����ʱ */
			
			//EPT(stderr, "node %d 222\n", node);
		}

	}
}

void *ht_mid_life_thread(void *arg)
{
	U8 node = *(U8 *)arg;

	struct timeval tv;
	tv.tv_sec = 5;     /* �д����� */
	tv.tv_usec = 0;
    select(0,NULL,NULL,NULL,&tv);   /* ��ʱ����ʱ */

	mid_manage[node-1]->timeup = 1;      /* ��ʱ������Ҫ��� */

	sem_post(&(mid_manage[node-1]->sem));

	EPT(stderr, "ht_mid_life_thread: timeup!\n");
}
#endif
