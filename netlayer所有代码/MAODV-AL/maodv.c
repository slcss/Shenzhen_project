#include "maodv.h"

#define TEST

static nl_tshare_t  share;
static m_table_shm *p_mt_shm;
int queue_test_init();

//定义重复表
repeat_table re_table;
//定义多播路由表
maodv_table m_table;
//本节点为组长的组长列表，用于广播GRPH
L_list leader_list;
//定义接收缓存
mmsg_t rcv_buff;
//定义发送缓存
//mmsg_t snd_buff;
//定义RREP缓存数组
int rrep_num;
rrep_t rrep_tmp[MAX_RREP_TMP_NUM];
//定义源序列号
int SERIES;
int M_SERIES;

MADR join_addr = 0;

#ifdef TEST
int qid_test = -1;
#endif

int LOCAL_ADDR;

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("too few parameter! parameter1: my_node\n");
		return -1;
	}

	LOCAL_ADDR = atoi(argv[1]);//本节点号;

	#ifdef TEST
    queue_test_init();
    #else
    mr_queues_init("maodv");
    #endif

    data_init();
    shm_init();

	start_timer();

	//接受外部信号SIGUSR1
	signal(SIGUSR1, signal_deal);

    pthread_t 	tid;
    if (pthread_create(&tid, NULL, rcv_thread, NULL) < 0)
    {
		printf("maodv rcv pthread create failed\n");
		return -1;
	}

	sleep(2);
	test();

    int stop = 0;
    pthread_mutex_lock(&share.mutex);
    while(0 == stop)	//此处条件设置正常情况下应该与子线程有联系，
    {
		printf("waiting stop of maodv rcv pthread\n");
		//条件变量用于阻塞当前线程，等待别的线程使用pthread_cond_signal()唤醒它，同时释放互斥锁！！
		//必须和互斥锁配合，防止多个线程同时申请pthread_cond_wait！
		pthread_cond_wait(&share.cond, &share.mutex);
	}
	pthread_mutex_unlock(&share.mutex);
	return 0;
}

int queue_test_init()
{
	int seq = 100 + LOCAL_ADDR;
    key_t key_test = ftok(PATH_CREATE_KEY, seq);
    if(key_test<0)
    {
    	printf("get key_test error!\n");
    	return -1;
    }
    qid_test = msgget(key_test, IPC_CREAT|QUEUE_MODE);
    if(qid_test == -1)
    {
    	printf("get qid_test error!\n");
    	return -1;
    }
    printf("qid_test of %d : %d\n",LOCAL_ADDR,qid_test);

    key_test = ftok(PATH_CREATE_KEY, 100);
    if(key_test<0)
    {
    	printf("get key_test error!\n");
    	return -1;
    }
    int qid_test_nl = msgget(key_test, IPC_CREAT|QUEUE_MODE);
    if(qid_test_nl == -1)
    {
    	printf("get qid_test_nl error!\n");
    	return -1;
    }
    nl_qid = qid_test_nl;
    printf("qid_test[0] : %d\n",nl_qid);
	return 0;
}
//接收并根据消息两队列类型区分路由报文，控制报文
void *rcv_thread(void *arg)
{
    DEBUG("maodv rcv_thread run\n");

	int size;

	while (1)
	{
		#ifdef TEST
		size=msgrcv(qid_test, &rcv_buff, sizeof(mmsg_t) - sizeof(long), 0, 0);
		#else
		size=msgrcv(ma_qid, &rcv_buff, sizeof(mmsg_t) - sizeof(long), 0, 0);
		#endif
        if (size < 0)
            break;

        switch(rcv_buff.mtype)
        {
            case MMSG_MAODV:
                manage_maodv_msg(&rcv_buff,size);
                break;
            case MMSG_MAODV_CONTROL:
                manage_control_msg(&rcv_buff);
                break;
            default:
                DEBUG("maodv rcv other\n");
                break;
        }
	}

	pthread_mutex_lock(&share.mutex);		//使用互斥锁，保证操作的原子性，不可中断
	pthread_cond_signal(&share.cond);		//唤醒主线程的退出阻塞
	pthread_mutex_unlock(&share.mutex);
}
//根据路由报文头部类型区分RREQ，RREP，MACT，GRPH等报文
int manage_maodv_msg(mmsg_t * rcv_buff,int size)
{
	//printf("rcv maodv msg\n");

    maodv_h * head= (maodv_h *)(rcv_buff->data);
    //先判断是否为正确的接受节点
    MADR r_addr = head->r_addr;
    if(r_addr == 0xff || r_addr == LOCAL_ADDR)
    {
    	//如果该广播报文的发起源是本节点，则直接返回（因为发出时并未计入重复表，所以可能会传回来）
    	if(r_addr == 0xff && head->source_addr == LOCAL_ADDR)
    	{
    		return -1;
    	}
        //再根据不同的类型进行区分处理
		maodv_t type = head->type;

		switch(type)
		{
			case RREQ:
				//广播需要判断重复
				printf("maodv rcv rreq_msg from %d\n",head->snd_addr);
				rcv_rreq(rcv_buff);
				break;
			case RREP:
				//单播不需要判断重复
				printf("maodv rcv rrep_msg from %d\n",head->snd_addr);
				rcv_rrep(rcv_buff);
				break;
			case MACT:
				printf("maodv rcv mact_msg from %d\n",head->snd_addr);
				rcv_mact(rcv_buff);
				break;
			case GRPH:
				rcv_grph(rcv_buff);
				break;
			case GRPH_AL:
				printf("maodv rcv grph-al_msg from %d\n",head->snd_addr);
				rcv_grph_al(rcv_buff);
				break;
			case RREQ_AL:
				printf("maodv rcv rreq-al_msg from %d\n",head->snd_addr);
				rcv_rreq_al(rcv_buff);
				break;
			case RREP_AL:
				printf("maodv rcv rrep-al_msg from %d\n",head->snd_addr);
				rcv_rrep_al(rcv_buff);
				break;
			case HELLO_AL:
				//printf("maodv rcv hello-al_msg from %d\n",head->snd_addr);
				rcv_hello_al(rcv_buff);
				break;
			case MACT_AL:
				printf("maodv rcv mact-al_msg from %d\n",head->snd_addr);
				rcv_mact_al(rcv_buff);
				break;
			case AL_DATA:
				printf("maodv rcv al_data_msg from %d\n",head->snd_addr);
				rcv_al_data(rcv_buff,size);
				break;
			case DATA:
				printf("maodv rcv data_msg from %d\n",head->snd_addr);
				rcv_data(rcv_buff,size);
				break;
			default:
				DEBUG("rcv other type\n");
				break;
		}
    }
    return 0;
}
//接受路由控制报文
int manage_control_msg(mmsg_t * rcv_buff)
{
    printf("maodv rcv control\n");

    control_t * control_ptr = (control_t *)(rcv_buff->data);

    if(0 == strncmp(control_ptr->type,"join",4))
		join(rcv_buff->node);

	else if(0 == strncmp(control_ptr->type,"quit",4))
		quit(rcv_buff->node);

	else if(0 == strncmp(control_ptr->type,"build",5))
		send_build(rcv_buff->node,control_ptr->AL_HOP);

	else if(0 == strncmp(control_ptr->type,"delete",6))
		send_delete(rcv_buff->node,control_ptr->AL_HOP);


	else
		print_mtable();
		print_link_table();

    return 0;
}

//发送RREQ，加入组播m_addr,返回1说明已经是成员，返回0说明添加了路径
int join(MADR m_addr)
{
    //清空RREP_TMP表
    rrep_num = 0;
    join_addr = m_addr;

    memset(rrep_tmp,0,sizeof(rrep_tmp));
    mmsg_t snd_buff;
    memset(&snd_buff,0,sizeof(snd_buff));
	//为发送缓存赋初值
    snd_buff.mtype = MMSG_MAODV;
    snd_buff.node = LOCAL_ADDR;
    maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
    rreq_t* rreq_ptr = (rreq_t*)(snd_buff.data + sizeof(maodv_h));
    head_ptr->type = RREQ;
    head_ptr->series = get_series();
	head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = 0xff;
	rreq_ptr->m_addr = join_addr;
	rreq_ptr->m_series = 0;
	rreq_ptr->flag = JOIN;
	rreq_ptr->hop = 0;					//生成时为0.发送之前加一
	rreq_ptr->link[0] = LOCAL_ADDR;

	//内部结构都是再char类型数组成员内部分割的，所以不同结构之间邻接，只考虑结构体内部的内存对齐即可
	size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_t);

    int i;
    for(i = 0;i < MAX_MAODV_ITEM;i++)
    {
    	maodv_item item = m_table.item[i];
    	//如果查到表项
		if(m_addr == item.m_addr)
		{
			//激活状态，说明已经是组播成员，直接返回1
			if(item.is_alive == 1)
			{
				DEBUG("this node is already in the tree\n");
				m_table.item[i].Ntype = MEMBER;
				print_mtable();
				return 1;
			}

			//发起节点有表项但未激活，说明收到过组长GRPH通知，有到组播的路径（未被超时清空），则单播发送
			else
			{
				head_ptr->r_addr = item.up_node;		//下一跳为上游节点
				rreq_ptr->m_series = item.m_series;		//组播序列号大于该值的才能回复
			}
		}
    }
	//发送之前加一
	rreq_ptr->hop += 1;

    while(msgsnd(nl_qid, &snd_buff, len, IPC_NOWAIT) < 0)
	{
		//等待消息队列空间可用时被信号中断
		if(errno == EINTR)
			continue;
		//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
		if(errno == EAGAIN)
		{
			printf("snd queue full , blocked\n...clean this queue...\n");
			printf("notice!!! important data may loss !!\n");
			mmsg_t temp_buff;
			while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

		}
		else
			{
				printf("maodv rreq send error!\n");
				return -1;
			}
	}
	DEBUG("try join %d ,waiting for RREP ...\n",m_addr);

	//建立一个周期为3执行次数为1的定时器
	t_table_add("filt_rrep",3,1,filt_rrep);

    return 0;
}
//筛选最优RREP，并返回MACT
int filt_rrep()
{
	DEBUG("begin filt rrep\n");
	mmsg_t snd_buff;
	snd_buff.mtype = MMSG_MAODV;
    snd_buff.node = LOCAL_ADDR;
	maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
	rreq_t* rreq_ptr = (rreq_t*)(snd_buff.data + sizeof(maodv_h));

	head_ptr->type = RREQ;
    head_ptr->series = get_series();
	head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = 0xff;
	rreq_ptr->m_addr = join_addr;
	rreq_ptr->m_series = 0;
	rreq_ptr->flag = JOIN;
	rreq_ptr->hop = 0;					//生成时为0.发送之前加一
	rreq_ptr->link[0] = LOCAL_ADDR;
	MADR m_addr = rreq_ptr->m_addr;

	size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_t);
	//为静态局部变量赋初值是在编译时进行值的，即只赋初值一次，在程序运行时它已有初值。
	//以后每次调用函数时不再重新赋初值而只是保留上次函数调用结束时的值。
	static int no_res_times = 0;

	//如果没有收到回复(第一次)
	if(rrep_num == 0 && no_res_times == 0)
	{
		//第一次没有收到回复就广播再发送一次，因为第一次可能是单播
		printf("no rrep respond for once\n");
		head_ptr->r_addr = 0xff;
		while(msgsnd(nl_qid, &snd_buff, len, IPC_NOWAIT) < 0)
		{
			//等待消息队列空间可用时被信号中断
			if(errno == EINTR)
				continue;
			//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
			if(errno == EAGAIN)
			{
				printf("snd queue full , blocked\n...clean this queue...\n");
				printf("notice!!! important data may loss !!\n");
				mmsg_t temp_buff;
				while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

			}
			else
				{
					printf("maodv rreq send error!\n");
					return -1;
				}
		}

		no_res_times++;
		t_table_add("filt_rrep",3,1,filt_rrep);
		return -1;
	}

	if(rrep_num == 0 && no_res_times == 1)
	{
		//如果第二次还没收到回复，就发起新建一个多播树
		printf("no rrep respond for second\n");
		DEBUG("try join %d again...\n",m_addr);
		tree_init(m_addr);
		//初始化树，打印路由表
		print_mtable();
		no_res_times = 0;
		return 0;
	}

	//添加到请求树的路径表项，并返回MACT（根据选出的最优RREP下标index）
	int index = 0;
	int i;
	for(i = 0;i < rrep_num;i++)
	{
		if(rrep_tmp[i].m_series > rrep_tmp[index].m_series)
		{
			index = i;
		}
		else if(rrep_tmp[i].m_series == rrep_tmp[index].m_series)
		{
			if(rrep_tmp[i].hop < rrep_tmp[index].hop)
				index = i;
		}
	}

	printf("choose 1st from rrep tmp:\nleader list:");
	for(i = 0;i<=rrep_tmp[index].hop;i++)
	{
		printf("%d ",rrep_tmp[index].leader_link[i] );
	}
	printf("\n");


	DEBUG("choose a path ,and return MACT\n");

	//生成MACT报文snd_buff，直接拷贝rrep_tmp中存储的RREP报文即可，因为MACT和RREP相同
    mact_t* mact_ptr = (mact_t*)(snd_buff.data + sizeof(maodv_h));
    memcpy(mact_ptr,&rrep_tmp[index],sizeof(mact_t));
    mact_ptr->index = 0;
    //从节点数组中提取上游节点地址
    MADR up_node = mact_ptr->link[mact_ptr->index + 1];
    //修改MAODV头部报文
	head_ptr->type = MACT;
    head_ptr->series = get_series();
	head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = up_node;

	printf("r_addr:%d,mact leader:%d\n",up_node,mact_ptr->l_addr);
	printf("mact leader list:");
	for(i = 0;i<=mact_ptr->hop;i++)
	{
		printf("%d ",mact_ptr->leader_link[i] );
	}
	printf("\n");

	//根据MACT报文添加多播路由表
	if(-1 == m_table_add(mact_ptr))
	{
		fprintf(stderr,"m_table add error!\n");
		return -1;
	}
	//index要加一再发出去
	mact_ptr->index += 1;
	mact_ptr->hop -= 1;

	//发送MACT报文，单播

	len = sizeof(MADR) + sizeof(maodv_h) + sizeof(mact_t);
	while(msgsnd(nl_qid, &snd_buff, len, IPC_NOWAIT) < 0)
	{
		//等待消息队列空间可用时被信号中断
		if(errno == EINTR)
			continue;
		//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
		if(errno == EAGAIN)
		{
			printf("snd queue full , blocked\n...clean this queue...\n");
			printf("notice!!! important data may loss !!\n");
			mmsg_t temp_buff;
			while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

		}
		else
		{
			fprintf(stderr,"maodv rreq send error!\n");
			return -1;
		}
	}

	print_mtable();

	return 0;
}
//处理收到的RREQ报文，包括生成和返回RREP的情况
int rcv_rreq(mmsg_t * rcv_buff)
{
	//判断是否重复
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	MADR source_addr = head->source_addr;
	int series = head->series;
	//对RREQ，不论是不是广播发来的都检查重复，因为上一跳非广播不带表其他转发节点非广播
	if(check_repeat(source_addr,series) < 0)
	{
		DEBUG("repeat broadcast rreq from %d\n",head->snd_addr);
		return -1;
	}
	//判断是否为请求组播数成员
	rreq_t* rreq_ptr = (rreq_t*)(rcv_buff->data + sizeof(maodv_h));
	//返回-1，说明没有表项，否则，返回表项index
	int index = check_mtable(rreq_ptr->m_addr);
	if(index == -1)
	{
		//没有相应表项，直接广播转发
		head->snd_addr = LOCAL_ADDR;
		head->r_addr = 0xff;
		//RREQ发送之前hop已经进行加一操作，所以收到是rreq_ptr->hop其实是本节点的数组下标
		rreq_ptr->link[rreq_ptr->hop] = LOCAL_ADDR;
		//发送前hop加一
		rreq_ptr->hop += 1;
		size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_t);
		while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
		{
			//等待消息队列空间可用时被信号中断
			if(errno == EINTR)
				continue;
			//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
			if(errno == EAGAIN)
			{
				printf("snd queue full , blocked\n...clean this queue...\n");
				printf("notice!!! important data may loss !!\n");
				mmsg_t temp_buff;
				while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

			}
			else
				{
					printf("maodv rreq send error!\n");
					return -1;
				}
		}
		printf("broadcast rreq from %d again\n",source_addr);
	}
	else
	{
		if(m_table.item[index].Ntype == OTHER || m_table.item[index].m_series < rreq_ptr->m_series)
		{
			//非组成员或者组序列号小于携带的组序列号，单播转发
			head->r_addr = m_table.item[index].up_node;
			rreq_ptr->link[rreq_ptr->hop] = LOCAL_ADDR;
			rreq_ptr->hop += 1;
			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_t);
			while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
			{
				//等待消息队列空间可用时被信号中断
				if(errno == EINTR)
					continue;
				//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
				if(errno == EAGAIN)
				{
					printf("snd queue full , blocked\n...clean this queue...\n");
					printf("notice!!! important data may loss !!\n");
					mmsg_t temp_buff;
					while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

				}
				else
					{
						printf("maodv rreq send error!\n");
						return -1;
					}
			}
			printf("sigalcast rreq from %d again\n",source_addr);
		}
		else
		{
			//组成员，返回RREP
			mmsg_t snd_buff;
			memset(&snd_buff,0,sizeof(snd_buff));
			//为发送缓存赋初值
			snd_buff.mtype = MMSG_MAODV;
			snd_buff.node = LOCAL_ADDR;
			maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
			rrep_t* rrep_ptr = (rrep_t*)(snd_buff.data + sizeof(maodv_h));
			head_ptr->type = RREP;
			head_ptr->series = get_series();
			head_ptr->source_addr = LOCAL_ADDR;
			head_ptr->snd_addr = LOCAL_ADDR;
			head_ptr->r_addr = rreq_ptr->link[rreq_ptr->hop - 1];
			rrep_ptr->m_addr = rreq_ptr->m_addr;
			rrep_ptr->m_series = m_table.item[index].m_series;
			rrep_ptr->time = m_table.item[index].time;
			rrep_ptr->l_addr = m_table.item[index].l_addr;
			//发送之前加一,这里为了方便直接加上了
			rrep_ptr->hop = m_table.item[index].hop + 1;
			//当前下标和路径总节点数，当前下标发送之前减一，这里为了方便直接减去了
			rrep_ptr->index = rreq_ptr->hop - 1;
			rrep_ptr->node_num = rreq_ptr->hop;
			//将返回节点上游节点链路写入RREP链路
			printf("build rrep!!!! leader link :\n");
			int i;
			for(i = 0;i <= m_table.item[index].hop;i++)
			{
				printf("%d ",m_table.item[index].leader_link[i]);
				rrep_ptr->leader_link[i] = m_table.item[index].leader_link[i];
			}
			printf("\n");
			//拷贝RREQ记录的路径数组,注意要先把本节点加上
			rreq_ptr->link[rreq_ptr->hop] = LOCAL_ADDR;
			memcpy(rrep_ptr->link,rreq_ptr->link,sizeof(MADR)*MAX_HOP);

			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rrep_t);

			while(msgsnd(nl_qid, &snd_buff, len, IPC_NOWAIT) < 0)
			{
				//等待消息队列空间可用时被信号中断
				if(errno == EINTR)
					continue;
				//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
				if(errno == EAGAIN)
				{
					printf("snd queue full , blocked\n...clean this queue...\n");
					printf("notice!!! important data may loss !!\n");
					mmsg_t temp_buff;
					while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

				}
				else
					{
						printf("maodv rreq send error!\n");
						return -1;
					}
			}
			printf("return rrep for rreq from %d\n",source_addr);
		}
	}
    return 0;
}
//转发或接受RREP，并填充入RREP_TMP表
int rcv_rrep(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	rrep_t* rrep_ptr = (rrep_t*)(rcv_buff->data + sizeof(maodv_h));

	//先判断单播RREP是否为正确的接受节点
    MADR r_addr = head->r_addr;
    if(r_addr != LOCAL_ADDR)
    {
    	return -1;
    }

	//添加节点到组长链路
	rrep_ptr->leader_link[rrep_ptr->hop] = LOCAL_ADDR;

	printf("rcv rrep from %d!!!\nleader link :",head->source_addr);
	int i;
	for(i = 0;i<=rrep_ptr->hop;i++)
	{
		printf("%d ",rrep_ptr->leader_link[i] );
	}
	printf("\n");

	//如果是RREQ请求节点，则填充入RREP_TMP表
	if(rrep_ptr->index == 0)
	{
		if(rrep_num < MAX_RREP_TMP_NUM)
		{
			memcpy(&rrep_tmp[rrep_num],rrep_ptr,sizeof(rrep_t));
			//该值和该缓存数组会在join函数起始时归零
			rrep_num++;
		}
		printf("rrep tmp:\n");
		int j;
		for(j = 0;j<=rrep_ptr->hop;j++)
		{
			printf("%d ",rrep_tmp[rrep_num-1].leader_link[j] );
		}
		printf("\n");
	}


	//如果是路由节点，则继续向反向路径转发
	else if(rrep_ptr->index > 0)
	{
		head->snd_addr = LOCAL_ADDR;
		head->r_addr = rrep_ptr->link[rrep_ptr->index - 1];
		//节点地址数组下标减一
		rrep_ptr->index -= 1;
		//到组长节点跳数加一
		rrep_ptr->hop += 1;

		size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rrep_t);
		while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
		{
			//等待消息队列空间可用时被信号中断
			if(errno == EINTR)
				continue;
			//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
			if(errno == EAGAIN)
			{
				printf("snd queue full , blocked\n...clean this queue...\n");
				printf("notice!!! important data may loss !!\n");
				mmsg_t temp_buff;
				while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

			}
			else
				{
					printf("maodv rreq send error!\n");
					return -1;
				}
		}
	}

    return 0;
}
//添加多播路由表项和继续转发
int rcv_mact(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	mact_t* mact_ptr = (mact_t*)(rcv_buff->data + sizeof(maodv_h));

	//先判断单播MACT是否为正确的接受节点
    if(head->r_addr != LOCAL_ADDR)
    {
    	return -1;
    }
	//收到剪枝消息MACT-P
	if(mact_ptr->flag == PRUNE || mact_ptr->flag == PG)
	{
		rcv_mact_p(rcv_buff);
		//收到剪枝消息，打印路由表
		print_mtable();
		return 0;
	}

	//根据MACT报文添加多播路由表
	if(-1 == m_table_add(mact_ptr))
	{
		fprintf(stderr,"m_table add error!\n");
		return -1;
	}
	//添加路由表后，打印路由表
	print_mtable();


	//如果还没有返回到最后一个节点，即组成员节点，则继续转发MACT报文
	if(mact_ptr->index < mact_ptr->node_num)
	{
		//index要加一再发出去
		mact_ptr->index += 1;
		mact_ptr->hop -= 1;

		head->snd_addr = LOCAL_ADDR;
		head->r_addr = mact_ptr->link[mact_ptr->index];

		//发送MACT报文，单播
		size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(mact_t);
		while(msgsnd(nl_qid,rcv_buff, len, IPC_NOWAIT) < 0)
		{
			//等待消息队列空间可用时被信号中断
			if(errno == EINTR)
				continue;
			//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
			if(errno == EAGAIN)
			{
				printf("snd queue full , blocked\n...clean this queue...\n");
				printf("notice!!! important data may loss !!\n");
				mmsg_t temp_buff;
				while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

			}
			else
			{
				fprintf(stderr,"maodv rreq send error!\n");
				return -1;
			}
		}
	}
    return 0;
}
//接受组播通知报文GRPH，并更新组播路由表,注意会刷新超时！！
int rcv_grph(mmsg_t * rcv_buff)
{
	//判断是否重复
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	grph_t* grph_ptr = (grph_t*)(rcv_buff->data + sizeof(maodv_h));
	MADR source_addr = head->source_addr;
	int series = head->series;
	//DEBUG("rcv grph from %d\n",head->snd_addr);


	MADR m_addr = grph_ptr->m_addr;

	int index = check_mtable(m_addr);
	//没有表项，则找到第一个可用位置
	if(-1 == index)
	{
		//若不存在表项，则找到第一个可用位置，并将类型置为OTHER
		index = refresh_mtable();
		if(-1 == index)
		{
			fprintf(stderr,"cant add maodv item from GRPH\n");
			return -1;
		}
		m_table.item[index].Ntype = OTHER;

	}

	//若类型是组外节点，则覆盖式填写
	if(m_table.item[index].Ntype == OTHER)
	{

		if(check_repeat(source_addr,series) < 0)
		{
			//DEBUG("repeat broadcast grph\n");
			return -1;
		}

		//更新路由表项
		//DEBUG("refresh m_addr as out %d by grph from %d\n",m_addr,m_table.item[index].up_node);
		m_table.item[index].m_series = grph_ptr->m_series;
		m_table.item[index].is_alive = 0;
		m_table.item[index].time = grph_ptr->time;
		m_table.item[index].l_addr = grph_ptr->l_addr;
		m_table.item[index].hop = grph_ptr->hop;
		m_table.item[index].up_node = head->snd_addr;
		//标记出界
		grph_ptr->flag = OUT;

	}
	//如果是组内节点，但不是父节点发来，则直接丢弃，不转发，防止兄弟节点间广播风暴
	else if(head->snd_addr != m_table.item[index].up_node)
	{
		//注意这里不检查重复，因为检查重复就会默认添加，导致组长退出操作时出现异常情况：
		//如1，2，3节点两两相邻，1为组长，2，3为成员，当1退出时，2选为新组长（上下游互相改变），此时2节点会发送grph给1和3
		//此时3节点是组内节点，若收到2节点（非上游）的grph加入重复表的话，就无法再接受1节点转发自2的grph
		//从而造成3节点事实上无法处理（和更新）新组长（即2）的grph，导致3节点支路超时退出
		return -1;
	}

	//如果为组内节点且未跳出过组播树,并且是父节点发来
	else if(grph_ptr->flag != OUT && head->snd_addr == m_table.item[index].up_node)
	{
		if(check_repeat(source_addr,series) < 0)
		{
			//DEBUG("repeat broadcast grph\n");
			return -1;
		}

		//DEBUG("refresh item %d by grph from %d\n",m_addr,m_table.item[index].up_node);
		m_table.item[index].is_alive = 1;
		m_table.item[index].m_series = grph_ptr->m_series;
		m_table.item[index].time = grph_ptr->time;
		//当组长节点更换时可能会更新跳数
		m_table.item[index].hop = grph_ptr->hop;
		m_table.item[index].l_addr = grph_ptr->l_addr;
	}

	//转发GRPH报文，组内节点只转发沿树形链路发送下来的
	//DEBUG("transmit GRPH from %d\n",head->snd_addr);
	head->snd_addr = LOCAL_ADDR;
	grph_ptr->hop += 1;
	size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(grph_t);
	while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
	{
		//等待消息队列空间可用时被信号中断
		if(errno == EINTR)
			continue;
		//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
		if(errno == EAGAIN)
		{
			printf("snd queue full , blocked\n...clean this queue...\n");
			printf("notice!!! important data may loss !!\n");
			mmsg_t temp_buff;
			while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

		}
		else
		{
			fprintf(stderr,"maodv rreq send error!\n");
			return -1;
		}
	}

    return 0;
}
//收到data，测试用
int rcv_data(mmsg_t * rcv_buff,int size)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	char* data_ptr = (char*)(rcv_buff->data + sizeof(maodv_h));
	printf("%d rcv data\n",LOCAL_ADDR);

	MADR m_addr = head->m_addr;

	rcv_buff->node = LOCAL_ADDR;

	int index = -1;
	int i;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		maodv_item item = m_table.item[i];
		//如果查到表项
		if(m_addr == item.m_addr && item.is_alive == 1)
		{
			index = i;
			break;
		}
	}
	if(-1 == index)
	{
		printf("have no item of %d\n",m_addr);
		return -1;
	}

	size_t len = size;

	if(m_table.item[index].Ntype == LEADER || head->snd_addr == m_table.item[index].up_node)
	{
		if(head->snd_addr == m_addr)
		{
			printf("this is source data\n");
			head->source_addr = LOCAL_ADDR;
			head->series = get_series();
		}

		if(m_table.item[index].Ntype == MEMBER || m_table.item[index].Ntype == LEADER)
		{
			printf("data :%s\n",data_ptr);
			struct  timeval    tv;
			gettimeofday(&tv,NULL);
			//精确到秒和微秒
			printf("tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);
		}

		else if(m_table.item[index].Ntype == ROUTE)
		{
			printf("snd to down node as ROUTE\n");
		}

		if(m_table.item[index].low_num > 0)
		{
			head->snd_addr = LOCAL_ADDR;

			ListNode* L = m_table.item[index].Lhead;
			while(L != NULL)
			{
				head->r_addr = L->m_nValue;
				printf("snd data from %d to %d ,len:%lu\n",head->snd_addr,head->r_addr,len);
				while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
				{
					//等待消息队列空间可用时被信号中断
					if(errno == EINTR)
						continue;
					//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
					if(errno == EAGAIN)
					{
						printf("snd queue full , blocked\n...clean this queue...\n");
						printf("notice!!! important data may loss !!\n");
						mmsg_t temp_buff;
						while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

					}
					else
					{
						perror("ABC");
					}
				}
				L = L->m_pNext;
			}
		}

		return 0;
	}
	//如果是从下游发来的，则继续转发到上游
	else
	{
		if(head->snd_addr == m_addr)
		{
			printf("this is source data\n");
			head->source_addr = LOCAL_ADDR;
			head->series = get_series();
		}

		head->snd_addr = LOCAL_ADDR;
		head->r_addr = m_table.item[index].up_node;

		if(head->r_addr == 0)
		{
			printf("find no up_node ,return -1\n");
			return -1;
		}

		printf("snd data from %d to %d ,len:%lu\n",head->snd_addr,head->r_addr,len);

		while(msgsnd(nl_qid, rcv_buff, len, IPC_NOWAIT) < 0)
		{
			//等待消息队列空间可用时被信号中断
			if(errno == EINTR)
				continue;
			//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
			if(errno == EAGAIN)
			{
				printf("snd queue full , blocked\n...clean this queue...\n");
				printf("notice!!! important data may loss !!\n");
				mmsg_t temp_buff;
				while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );
			}
			else
			{
				printf("maodv data send error!\n");
				return -1;
			}
		}
	}

	return 0;
}
//通过报文的源地址和序列号检查报文是否重复，重复则返回-1，否则返回0
int check_repeat(MADR src_addr,int series)
{
    time_t 	ctime;
	ctime = time(NULL);

	int have_sign = 0;
	int ok_index = -1;
    int i;
    repeat_item * pitem;
    //检查重复，超时和可用位置，通过这个循环则认为不重复
    for(i=0;i<MAX_REPEAT_ITEM;i++)
    {
        pitem = &re_table.item[i];
        //先检查超时，每次查表之前都检查超时，所以不用额外定时器驱动
        if(ctime - pitem->time > re_table.timeout)
        {
            memset(pitem,0,sizeof(repeat_item));
        }
        //如果重复则直接返回-1
        if(pitem->is_alive && pitem->source_addr == src_addr && pitem->series == series)
        {
            //DEBUG("data repeat\n");
            return -1;
        }
        //记录第一个可用位置
        if(!have_sign)
        {
            if(pitem->time == 0 || pitem->is_alive == 0)
            {
                have_sign = 1;
                ok_index = i;
            }
        }
    }
    //如果不重复但没有找到可用位置,则查找存在最久的item，使之失效，并作为可用位置
    if(ok_index == -1)
    {
        time_t time_tmp = re_table.item[0].time;
        for(i=0;i<MAX_REPEAT_ITEM;i++)
        {
            pitem = &re_table.item[i];
            if(pitem->time > time_tmp)
            {
                time_tmp = pitem->time;
                ok_index = i;
            }
        }
    }
    //当不重复，就加入重复记录表
    re_table.item[ok_index].source_addr = src_addr;
    re_table.item[ok_index].series = series;
    re_table.item[ok_index].time = ctime;
    re_table.item[ok_index].is_alive = 1;

    return 0;
}
//作为跟节点发起新建一棵共享树，注意会刷新路由表超时！！
int tree_init(MADR m_addr)
{
	DEBUG("init tree %d\n",m_addr);
	int index = refresh_mtable();
	if(-1 == index)
	{
		fprintf(stderr,"cant build maodv tree, table full\n");
		return -1;
	}
	m_table.item[index].m_addr =  m_addr;
	m_table.item[index].m_series =  get_m_series();
	m_table.item[index].is_alive = 1;
	m_table.item[index].time =  time(NULL);
	m_table.item[index].l_addr =  LOCAL_ADDR;
	m_table.item[index].hop =  0;
	m_table.item[index].Ntype = LEADER;
	m_table.item[index].leader_link[0] = LOCAL_ADDR;
	//默认上游节点，下游节点个数，下游链表都为0

	//组长节点地址添加到组长列表
	leader_list.Lhead = insert(leader_list.Lhead,m_addr);
	leader_list.num += 1;
	//当第一次调用tree_init函数时开启定时器，广播通知GRPH
	if(1 == leader_list.num)
	{
		//将广播GRPH的函数加入定时器，周期为1，次数为无限次
		t_table_add("broadcast_grph",1,0,broadcast_grph);
	}
	return 0;
}
//定时器驱动广播GRPH报文, 一次驱动，广播全部所领导组的GRPH,同时更新相应组的时间
int broadcast_grph()
{
	//printf("broadcast\n");
	if(leader_list.num != 0 && leader_list.Lhead != NULL)
	{
		//生成GRPH报文
		mmsg_t snd_buff;
		snd_buff.mtype = MMSG_MAODV;
		snd_buff.node = LOCAL_ADDR;
		maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
		grph_t* grph_ptr = (grph_t*)(snd_buff.data + sizeof(maodv_h));
		head_ptr->type = GRPH;
		//head_ptr->series = get_series();
		head_ptr->source_addr = LOCAL_ADDR;
		head_ptr->snd_addr = LOCAL_ADDR;
		head_ptr->r_addr = 0xff;
		grph_ptr->m_series = get_m_series();
		grph_ptr->l_addr = LOCAL_ADDR;
		grph_ptr->time = time(NULL);
		grph_ptr->flag = NONE;
		grph_ptr->hop = 1;
		ListNode* L = leader_list.Lhead;
		MADR m_addr;
		while(L != NULL)
		{
			m_addr = L->m_nValue;
			int i;
			//在发出GRPH报文之前刷新所领导多播组的时间
			for(i = 0;i < MAX_MAODV_ITEM;i++)
			{
				if(m_table.item[i].m_addr == m_addr && m_table.item[i].Ntype == LEADER)
				{
					m_table.item[i].time = grph_ptr->time;
					m_table.item[i].m_series = grph_ptr->m_series;
					break;
				}
			}
			//广播发送GRPH
			grph_ptr->m_addr = m_addr;
			head_ptr->series = get_series();
			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(grph_t);
			while(msgsnd(nl_qid, &snd_buff, len, IPC_NOWAIT) < 0)
			{
				//等待消息队列空间可用时被信号中断
				if(errno == EINTR)
					continue;
				//由于消息队列的msg_qbytes的限制和msgflg中指定IPC_NOWAIT标志，消息不能被发送,即发送队列满，阻塞
				if(errno == EAGAIN)
				{
					printf("snd queue full , blocked\n...clean this queue...\n");
					printf("notice!!! important data may loss !!\n");
					mmsg_t temp_buff;
					while( msgrcv(nl_qid, &temp_buff, len,0,IPC_NOWAIT) != -1 );

				}
				else
				{
					printf("maodv grph send error!\n");
				}
			}
			L = L->m_pNext;
		}
	}
	return 0;
}
//根据MACT报文添加组播路由表项，会覆盖之前GRPH添加的表项
int m_table_add(mact_t* mact_ptr)
{
	//先检查是否已存在表项
	int index = check_mtable(mact_ptr->m_addr);
	if(-1 == index)
	{
		//若不存在表项，则找到第一个可用位置
		index = refresh_mtable();
		if(-1 == index)
		{
			fprintf(stderr,"cant add maodv item from MACT\n");
			return -1;
		}
	}

	//如果是最后一个节点（组成员），则只增加下游节点即可,非最后一个，则需要进入if内部添加
	if(mact_ptr->index < mact_ptr->node_num)
	{
		m_table.item[index].m_addr =  mact_ptr->m_addr;
		m_table.item[index].m_series =  mact_ptr->m_series;
		m_table.item[index].is_alive = 1;
		m_table.item[index].time =  mact_ptr->time;
		m_table.item[index].l_addr =  mact_ptr->l_addr;
		m_table.item[index].hop =  mact_ptr->hop;

		//添加到组长链路
		printf("add leader list by mact:\n");
		int i;
		for(i=0;i<=m_table.item[index].hop;i++)
		{
			m_table.item[index].leader_link[i] = mact_ptr->leader_link[i];
			printf("%d ",m_table.item[index].leader_link[i]);
		}
		printf("\n");

		//上游节点
		m_table.item[index].up_node = mact_ptr->link[mact_ptr->index + 1];
		//如果是第一个节点（请求节点），则不需要增加下游节点，直接返回
		if(mact_ptr->index == 0)
		{
			//第一个节点类型为组成员，中间节点类型为路由节点
			m_table.item[index].Ntype = MEMBER;
			return 0;
		}
		else
			m_table.item[index].Ntype = ROUTE;

	}

	//增加下游节点（已排除请求节点,index=0时直接返回）
	m_table.item[index].Lhead = insert(m_table.item[index].Lhead,mact_ptr->link[mact_ptr->index - 1]);
	m_table.item[index].low_num += 1;

	return 0;
}

//刷新清零已经超时的多播路由表项，返回第一个可用空位下标，若返回-1，则说明无可用位置
//在添加表项和查找表项之前调用
int refresh_mtable()
{
	time_t 	ctime;
	ctime = time(NULL);
	int have_sign = 0;
	int ok_index = -1;
	int i;
	maodv_item * pitem;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &m_table.item[i];

		if(pitem->time != 0 && ctime - pitem->time > m_table.timeout)
		{
			//释放下游节点链表
			if(pitem->Lhead != NULL)
			{
				del_all(pitem->Lhead);
			}
			memset(pitem,0,sizeof(maodv_item));
		}
		//记录第一个可用位置
        if(!have_sign)
        {
            if(pitem->time == 0 || pitem->is_alive == 0)
            {
                have_sign = 1;
                ok_index = i;
            }
        }
	}

	if(ok_index == -1)
	{
		fprintf(stderr,"m_table is already full\n");
	}

	return ok_index;
}
//返回-1，说明没有表项，否则，返回表项index
int check_mtable(MADR m_addr)
{
	//暂时不进行超时检查，则注释本句
	refresh_mtable();
	int i;
	maodv_item * pitem;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &m_table.item[i];
		if(pitem->m_addr == m_addr)
		{
			return i;
		}
	}
	return -1;
}
//打印多播路由表
int print_mtable()
{
	int i;
	maodv_item * pitem;
	printf("============= m_table & node %d ==============\n",LOCAL_ADDR);
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &m_table.item[i];

		if(pitem->time != 0 && pitem->is_alive != 0)
		{
			printf("item %d :m_addr = %d ;leader = %d ;hop = %d ;",i,pitem->m_addr,pitem->l_addr,pitem->hop);
			switch(pitem->Ntype)
			{
				case LEADER:
					printf("type : LEADER\n");
					break;
				case MEMBER:
					printf("type : MEMBER\n");
					break;
				case ROUTE:
					printf("type : ROUTE\n");
					break;
				case OTHER:
					printf("type : OTHER\n");
					break;
				default:
					printf("type : UNKNOWN\n");
					break;
			}
			printf("	up node: %d ; low node: ",pitem->up_node);
			ListNode* L = pitem->Lhead;
			while(L != NULL)
			{
				printf(" %d",L->m_nValue);
				L = L->m_pNext;
			}
			printf("\n");

			printf("leader link : ");
			int j;
			for(j=0;j<=pitem->hop;j++)
			{
				printf("%d ",pitem->leader_link[j]);
			}
			printf("\n");

			printf("m_series:%d\n",pitem->m_series);
		}
	}

	return 0;
}

//初始化数据
int data_init()
{
	DEBUG("data init\n");
    //初始化重复记录表
    re_table.timeout = REPEAT_TIMEOUT;
    memset(re_table.item,0,sizeof(re_table.item));
	//初始化多播路由表
    m_table.timeout = MAODV_TIMEOUT;
    memset(m_table.item,0,sizeof(m_table.item));
	//初始化RREP缓存数组
	memset(rrep_tmp,0,sizeof(rrep_tmp));
	//初始化组长列表
	memset(&leader_list,0,sizeof(leader_list));
	//初始化源序列号
	SERIES = 0;
	M_SERIES = 0;

	return 0;

}

int shm_init()
{
	key_t 	shm_key;
	void	*shmaddr = NULL;

	//shm_key = qinfs[4].key_q;		//实际上用的是maodv进程消息队列的key
	shm_key = ftok(PATH_CREATE_KEY, 10086);
	DEBUG("maodv: shm_key = %d\n",shm_key);
	if (shm_key<0)
	{
		EPT(stderr,"failed to get m_table shm_key\n");
		return -1;
	}
	int shm_id = -1;
	shm_id = shmget(shm_key, sizeof(m_table_shm), 0640|IPC_CREAT); //创建共享内存
	perror("shmget");
	if(shm_id == -1) {
		EPT(stderr, "maodv: get shmget id error\n");
		return -1;
	}

	//要包含<sys/shm.h>头文件，否则shmat会默认返回int型，造成错误
	shmaddr = shmat(shm_id, NULL, 0);
	if (shmaddr == NULL)
	{
		EPT(stderr, "maodv: can not attach shared memory.\n");
		return -1;
	}

	p_mt_shm =(m_table_shm*)shmaddr;
	memset(p_mt_shm,0,sizeof(m_table_shm));
	DEBUG("addr of shm maodv table : %p\n",p_mt_shm);
	return 0;
}
//获得原节点地址
int get_series()
{
	if(SERIES > MAX_SERIES)
		SERIES = 0;

	return SERIES++;
}
//获得组播地址
int get_m_series()
{
	if(M_SERIES > MAX_SERIES)
		SERIES = 0;

	return M_SERIES++;
}

int test()
{
	//join(5);
	//print_mtable();

	return 0;
}

