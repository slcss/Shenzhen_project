#include "../mr_common.h"
#include "hm_test.h"
#define QUEUE_NUM 7

static key_t ht_qkey = -1;
static int   ht_qid = -1;

static mt_tshare_t  share = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER
};

static pthread_t mrx_tid = -1;
static pthread_t time_tid = -1;
static int rqs[MAX_NODE_CNT];   /* 记录节点的消息队列id 数字代表节点号 */
static U8  top[MAX_NODE_CNT][MAX_NODE_CNT];

U8 BS[MAX_CFS_PSF][MAX_NODE_CNT+1];   /* 记录时隙占用情况，第一个数字对应时隙号，每个时隙可以被多个节点占用，第二个数字对应节点号,BS[][0]记录此时隙节点数 11.03 */
U8 node_BS_flag[MAX_NODE_CNT+1];   /* 记录每个节点是否第一次占用时隙 [1]对应1号节点 [0]空闲 0对应没占用*/
U8 node_BS_backup[MAX_NODE_CNT+1];   /* 记录每个节点占用的时隙 [1]对应1号节点 [0]空闲 */

mmsg_t base[MAX_NODE_CNT];  /* 记录勤务帧 数字+1代表节点号 */
int num[MAX_NODE_CNT];     /* 记录勤务帧msg长度 数字+1代表节点号 */

mmsg_t MAC_frame[MAX_NODE_CNT];  /* 记录MAC帧 数字+1代表节点号 */
int num_MAC[MAX_NODE_CNT]; /* 记录MACmsg长度 数字+1代表节点号 */

struct itimerval new_value;
U8 flag = 0;	/* 第一次收到勤务帧的标志 */
U8 count = 0;	/* 时隙计数 */

sem_t send_begin;
pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;    /* for BS[][] & node_num[] */
pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;    /* for base[] & num[] */
pthread_mutex_t c = PTHREAD_MUTEX_INITIALIZER;	  /* for mid_manage[] */

FILE* fp;

mid_manage_t *mid_manage[32];  /* 中继表项生存期管理结构体 指针数组 */
U8  first[32];   /* 标注某节点对应的状态改变帧维护线程是否开启  0未开启 1开启 */





/**************************************** 用于队列测试 ********************************************/
int hopeHSN[QUEUE_NUM-3];  /* 4个队列期待的hsn */
lm_flow_ctrl_t queue[QUEUE_NUM-3];  /* LowMAC上传的流控反馈帧 */
lm_packet_t data;     /* LowMAC上传的mac帧 */
mmsg_t msg_test;     /* LowMAC上传的消息队列 */
int lm_count[QUEUE_NUM-3];  /* LowMAC队列计数 */
#define SUCCESS_RATE 9   /* 模拟收到HighMAC数据帧和流控帧上传成功的概率 */

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
	//H2L_MAC_frame_t frame;
	//U8 node_count;  /* 用于退网时候的时隙查找 */

	node = msg->node;
	if (msg->mtype == MMSG_MT_RQID)  /* 记录节点的消息队列号 */
	{
		//EPT(fp, "ht_rmsg_proc: rcv msg queue id from node %d\n", node);
		qid = *(int*)msg->data;

		if (MR_ISUNI(node)) 
		{
			rqs[node-1] = qid;
			EPT(stderr, "ht_rmsg_proc: rqs[%d] = %d\n", node-1, qid);
		}
		else 
		{
			EPT(fp, "ht_rmsg_proc: node error\n");
			rval = 1;
		}
	}
	else if(msg->mtype == 4)  /* 更新总时隙表 */
	{		
		memset(&package, 0, sizeof(package));
		memcpy(&package, msg->data, cnt-1);
		if(package.type == 0xff)
		{
			//EPT(fp, "ht_rmsg_proc: package.type = 0xff\n");

			/* 当节点变为SCN状态时，需要清空时隙记录(此时需要开始侦听，不应该占用时隙发送勤务帧) 1.13 */
			if(((H2L_MAC_frame_t *)package.data)->state == 0)
			{
				if(node_BS_flag[node])
				{
					BS[node_BS_backup[node]][node] = 0;
					BS[node_BS_backup[node]][0]--;
				
					node_BS_backup[node] = 0;
					node_BS_flag[node] = 0;
					EPT(stderr, "ht_rmsg_proc: node %d is SCNing\n", node);	
				}
			}

#if 0
			/* 根据收到的状态改变帧做相应处理 加锁 */
			if(first[node-1] == 0)
			{
				first[node-1] = 1;

				//pthread_mutex_lock(&c);
				mid_manage[node-1] = malloc(sizeof(mid_manage_t));	/* 给 中继表项生存期管理结构体 分配指针 */
				memset(mid_manage[node-1], 0, sizeof(mid_manage_t));	/* 初始化 中继表项生存期管理结构体 */
				
				sem_init(&(mid_manage[node-1]->sem), 0, 0);   /* 初始化相应信号量，开启对应的维护线程 */
				mid_manage[node-1]->node = node;                   /* 记录时隙号 */
				pthread_create(&(mid_manage[node-1]->id), NULL, ht_mid_manage_thread, (void *)&mid_manage[node-1]->node);	
				
				
				//pthread_mutex_unlock(&c);
				EPT(stderr, "ht_rmsg_proc: ht_mid_manage_thread begin\n");
			}
			
			sem_post(&(mid_manage[node-1]->sem));   /* 不能放在上面的if函数里 */
#endif
			
			return rval;
		}
		//EPT(fp, "ht_rmsg_proc: rcv slot table from node %d\n", node);
		//EPT(stderr, "ht_rmsg_proc: queue 4 package.Hsn from node %d = %d\n", node, package.Hsn);

		memset(&slot, 0, sizeof(slot));
		memcpy(&slot, package.data, package.len-4);
		i = slot.localBS;		

		/* 记录节点的BS占用，支持不同节点占用同一个时隙 11.04/优化修改 11.05 */
		pthread_mutex_lock(&a);  /* 加互斥量 */
		if(!node_BS_flag[node])  //查看node节点是否第一次占用时隙
		{
			node_BS_flag[node] = 1;
			node_BS_backup[node] = i;
				
			BS[i][node] = 1;
			BS[i][0]++;
			EPT(stderr, "ht_rmsg_proc: BS[%d][%d] = %d\n", i, node, node);			
		}
		else if(node_BS_backup[node] != i)  //查看已占用的时隙是否与现在相符
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
	else if(msg->mtype == 5)  /* 更新勤务帧数组 */
	{
		//EPT(fp, "ht_rmsg_proc: rcv service frame from node %d\n", node);
		//EPT(stderr, "ht_rmsg_proc: queue 5 package.Hsn from node %d = %d\n", node, ((lm_packet_t *)msg->data)->Hsn);
		
		pthread_mutex_lock(&b);       /* 加互斥量 */
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
	else if(msg->mtype == 1)
	{
		EPT(stderr, "ht_rmsg_proc: rcv MAC frame from node %d\n", node);
		pthread_mutex_lock(&b);       /* 加互斥量 */
		memset(&MAC_frame[node-1], 0, sizeof(mmsg_t));  //暂定每个节点发来的MAC帧不断覆盖
		memcpy(&MAC_frame[node-1], msg, sizeof(long)+cnt);  
		num_MAC[node-1] = cnt;
		pthread_mutex_unlock(&b);	
	}	
	else if(msg->mtype == 10)
	{
		if(rand()%10 < SUCCESS_RATE) //模拟一定概率接收到
		{
			memset(&package, 0, sizeof(package));
			memcpy(&package, msg->data, cnt-1);
			EPT(stderr, "hmHSN = %d\n", package.Hsn);
			if(package.Hsn == hopeHSN[0])
			{			
				hopeHSN[0]++;
				if(hopeHSN[0]==16)
					hopeHSN[0]=0;
				EPT(stderr, "hopeHSN = %d\n", hopeHSN[0]);
				//lm_count[0]++; 
				//EPT(stderr, "count = %d\n", lm_count[0]);
				memset(queue, 0, sizeof(queue));
				queue[0].HSN = hopeHSN[0];
				queue[0].HSN_flag = 2;
				queue[0].q_flag = 0;
			}
			else
			{	
				EPT(stderr, "hopeHSN = %d\n", hopeHSN[0]);
				memset(queue, 0, sizeof(queue));
				queue[0].HSN = hopeHSN[0];
				queue[0].HSN_flag = 1;
				queue[0].q_flag = 0;
			}

			/* 构造流控反馈帧 */
			memset(&data, 0, sizeof(data));
			data.len = 4+sizeof(queue);
			data.type = 0x0A;
			data.Hsn = 0;
			data.Lsn = 0;
			memcpy(data.data, queue, sizeof(queue));
			msg_test.mtype = 1;  //必须大于0!!!!!!!!
			memcpy(msg_test.data, &data, sizeof(data));

			if(rand()%10 < SUCCESS_RATE)  //模拟一定概率流控帧上传成功
			{				
				rval = msgsnd(rqs[node-1], (void *)&msg_test, data.len+1, 0);
				if ( rval < 0 )
				{
					EPT(fp, "timer_proc: msgsnd() write msg failed,errno=%d[%s]\n", errno, strerror(errno));
				}	
				EPT(stderr, "LowMAC send flow_ctrl\n");
			}
			//EPT(stderr, "ht_rmsg_proc2: rqs[%d] = %d\n", node-1, rqs[node-1]);
		}
		
	}
	
	else if(msg->mtype == 11)
	{}
	else if(msg->mtype == 12)
	{}
	else if(msg->mtype == 13)
	{}
	else if(msg->mtype == 100)  /* 退网通知 */
	{
		EPT(stderr, "ht_rmsg_proc: rcv quit frame from node %d\n", node);

		rqs[node-1] = -1;   /* 是按照-1来判断的 */
		
		pthread_mutex_lock(&b);       /* 加互斥量 */
		memset(&base[node-1], 0, sizeof(mmsg_t));
		num[node-1] = 0;
		memset(&MAC_frame[node-1], 0, sizeof(mmsg_t));
		num_MAC[node-1] = 0;
		pthread_mutex_unlock(&b);	

		/* 退网的修改 11.04/优化 11.05 */		
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
	U8 BS_count = 0;  /* 用于发送时候的节点查找 */

	new_value.it_value.tv_sec = 0;
	new_value.it_value.tv_usec = 64000;
	new_value.it_interval.tv_sec = 0;
	new_value.it_interval.tv_usec = 64000;

	sem_wait(&send_begin); /* 第一次收到勤务帧 */

	/* 先发送一次，即BS0 */
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
				if (j == node-1 || top[node-1][j] == 0)   /* 只考虑发送节点那一行的拓扑 */
					continue;
				//EPT(fp, "timer_proc: rqs[%d] = %d\n", j, rqs[j]);
				if (rqs[j] == -1)   /* 这里会不会有互斥的问题 ******************************************************/
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

			/* 更新节点数 */
			BS_count++;			
		}	
	}

	/* 应该加1 1.24 */
	count++;
	
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

	/* 暂停!! */
	while(1)
		pause();
	EPT(stderr, "timer_thread: pause fail!\n");

thread_return:
	sleep(1);
	pthread_exit((void *)&rval);
}

#if 0
void ht_timer_proc(int signo)
{
	int i,j;
	int	rval = 0;
	U8 node;
	U8 BS_count = 0;  /* 用于发送时候的节点查找 */

	for(i = 1; i <= MAX_NODE_CNT; i++)
	{
		pthread_mutex_lock(&a);	
		if(BS_count == BS[count][0])
		{
			pthread_mutex_unlock(&a);
			break;
		}		
		pthread_mutex_unlock(&a);
		
		node = i;
		if (BS[count][i])
		{
			EPT(fp, "timer_proc: BS[%d][%d] = %d\n", count, i, node);

			/* 查询node节点在拓扑中对应的可达节点 1.25 */
			for (j = 0; j < MAX_NODE_CNT; j++)
			{
				if (j == node-1 || top[node-1][j] == 0)   /* 只考虑发送节点那一行的拓扑 */
					continue;
				//EPT(fp, "timer_proc: rqs[%d] = %d\n", j, rqs[j]);
				if (rqs[j] == -1)   /* 这里会不会有互斥的问题 ******************************************************/
				{
					//EPT(fp, "timer_proc: can not get the qid of rx node %d\n", j+1);
					continue;
				}

				/* 找到了合适的j 1.21 */
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

			/* 更新节点数 */
			BS_count++;			
		}	
	}
	
	count++;
	if(count == 32)
		count = 0;
	//EPT(fp, "timer_proc: count = %d\n", count);
}
#endif

void ht_timer_proc(int signo)
{
	int i,j;
	int	rval = 0;
	U8 node;
	U8 BS_count = 0;  /* 用于发送时候的节点查找计数 */

	/* 源、目的节点关系表，纵向表示源节点，横向表示目的节点，BS[X][0]=1表示此源节点启用 1.26 */
	U8 BS_top[MAX_NODE_CNT+1][MAX_NODE_CNT+1] = {0};  
	U8 BS_samecount[MAX_NODE_CNT+1] = {0};  /* 目的节点计数，防止占用同一时隙的节点有相同的目的节点 1.26 */

	/* 查找占用count时隙的源节点及目的节点并记录 1.26 */
	for(i = 1; i <= MAX_NODE_CNT; i++)
	{
		pthread_mutex_lock(&a);	
		if(BS_count == BS[count][0])
		{
			pthread_mutex_unlock(&a);
			break;
		}		
		pthread_mutex_unlock(&a);
		
		node = i;
		if (BS[count][i])
		{
			EPT(fp, "timer_proc: BS[%d][%d] = %d\n", count, i, node);
			BS_top[node][0] = 1;

			/* 查询node节点在拓扑中对应的可达节点 1.25 */
			for (j = 0; j < MAX_NODE_CNT; j++)
			{
				if (j == node-1 || top[node-1][j] == 0)   /* 只考虑发送节点那一行的拓扑 */
					continue;

				/* 找到了合适的目的节点j+1，记录到下面的两个表格中 1.26 */
				else
				{
					BS_top[node][j+1] = 1;
					BS_samecount[j+1]++;
				}
			}

			/* 更新节点数 */
			BS_count++;			
		}	
	}
	BS_count = 0;

	/* 在发送时候判断有没有冲突的目的节点 1.26 */
	for(i = 1; i <= MAX_NODE_CNT; i++)
	{
		pthread_mutex_lock(&a);	
		if(BS_count == BS[count][0])
		{
			pthread_mutex_unlock(&a);
			break;
		}		
		pthread_mutex_unlock(&a);

		if(BS_top[i][0])
		{
			for(j = 1; j <= MAX_NODE_CNT; j++)
			{
				/* 筛选出合适的目的节点j 1.27 */
				if(BS_top[i][j] == 1 && BS_samecount[j] == 1 && rqs[j-1] != -1 && BS_top[j][0] != 1)  //j只能作为目的节点，不能为原节点
				{					
					pthread_mutex_lock(&b);
					rval = msgsnd(rqs[j-1], (void *)&base[i-1], num[i-1], 0);	
					if (rval < 0)
					{
						EPT(fp, "timer_proc: msgsnd() write msg failed1,errno=%d[%s]\n", errno, strerror(errno));
					}
					else						
						EPT(fp, "timer_proc: msgsnd() write msg at qid %d of node %d\n", rqs[j-1], j);
					
					if(num_MAC[i-1] != 0)
					{
						rval = msgsnd(rqs[j-1], (void *)&MAC_frame[i-1], num_MAC[i-1], 0);
						if (rval < 0)
						{
							EPT(fp, "timer_proc: msgsnd() write msg failed2,errno=%d[%s]\n", errno, strerror(errno));
						}
						else						
							EPT(fp, "timer_proc: msgsnd() write msg at qid %d of node %d\n", rqs[j-1], j);	
						//num_MAC[i-1] = 0;
					}
					pthread_mutex_unlock(&b);					
				}
			}
			num_MAC[i-1] = 0; //保证本次发送的同时又不重复发送
			BS_count++;
		}
		
	}
	
	count++;
	if(count == 32)
		count = 0;
	//EPT(fp, "timer_proc: count = %d\n", count);
}

