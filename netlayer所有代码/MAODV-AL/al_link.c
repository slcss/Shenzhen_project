
#include "maodv.h"
#include "al_link.h"

al_link_table link_table;

//发送build指令报文到组播成员，此处直接组长发送，暂时不考虑其他节点发送到组长节点的过程
int send_build(int m_addr,int AL_HOP)
{
	printf("build AL link for multicast %d ,hop : %d \n",m_addr,AL_HOP);
	printf("send_build msg time :\n");
	struct  timeval    tv;
	gettimeofday(&tv,NULL);
	//精确到秒和微秒
	printf("tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);

	mmsg_t snd_buff;
    memset(&snd_buff,0,sizeof(snd_buff));

    snd_buff.mtype = MMSG_MAODV;
    snd_buff.node = LOCAL_ADDR;
    maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
    grph_al_t* grph_al_ptr = (grph_al_t*)(snd_buff.data + sizeof(maodv_h));

	head_ptr->m_addr = m_addr;
    head_ptr->type = GRPH_AL;
    head_ptr->series = get_series();
    head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = LOCAL_ADDR;

	grph_al_ptr->m_addr = m_addr;
	grph_al_ptr->flag = START;
	grph_al_ptr->TTL = AL_HOP;


	rcv_grph_al(&snd_buff);

	return 0;
}

int snd_rreq_al(MADR m_addr,int TTL)
{
	printf("broadcast rreq-al for m_addr:%d,TTL:%d\n",m_addr,TTL);

	mmsg_t snd_buff;
    memset(&snd_buff,0,sizeof(snd_buff));

    snd_buff.mtype = MMSG_MAODV;
    snd_buff.node = LOCAL_ADDR;
    maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
    rreq_al_t* rreq_al_ptr = (rreq_al_t*)(snd_buff.data + sizeof(maodv_h));

	head_ptr->m_addr = m_addr;
    head_ptr->type = RREQ_AL;
    head_ptr->series = get_series();
    head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = 0xff;

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


	rreq_al_ptr->m_addr = m_addr;
	rreq_al_ptr->m_series = m_table.item[index].m_series;
	rreq_al_ptr->TTL = TTL;
	rreq_al_ptr->hop = 0;
	rreq_al_ptr->link[0] = LOCAL_ADDR;
	memcpy(rreq_al_ptr->leader_link,m_table.item[index].leader_link,sizeof(MADR)*MAX_HOP);

	size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_al_t);

	rreq_al_ptr->hop += 1;

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
	return 0;
}

int rcv_grph_al(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	grph_al_t* grph_al_ptr = (grph_al_t*)(rcv_buff->data + sizeof(maodv_h));
	printf("%d rcv grph_al\n",LOCAL_ADDR);

	MADR m_addr = head->m_addr;

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
	//只接受来自上游节点的报文
	if(m_table.item[index].Ntype == LEADER || head->snd_addr == m_table.item[index].up_node)
	{
		if(grph_al_ptr->flag == START)
		{
			printf("rcv AL_ATART msg for %d ,TTL %d ,from %d\n",m_addr,grph_al_ptr->TTL,head->snd_addr);
			if(m_table.item[index].Ntype != LEADER)
				snd_rreq_al(m_addr,grph_al_ptr->TTL);
		}

		//依次发送到下游节点
		size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(grph_al_t);
		if(m_table.item[index].low_num > 0)
		{
			head->snd_addr = LOCAL_ADDR;
			ListNode* L = m_table.item[index].Lhead;
			while(L != NULL)
			{
				head->r_addr = L->m_nValue;
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
	}

	return 0;
}

int rcv_rreq_al(mmsg_t * rcv_buff)
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

	MADR m_addr = head->m_addr;
	rreq_al_t* rreq_al_ptr = (rreq_al_t*)(rcv_buff->data + sizeof(maodv_h));

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
	//如果是外部节点，则根据TTL决定是否转发
	if(-1 == index || m_table.item[index].Ntype == OUT)
	{
		if(rreq_al_ptr->TTL == 1)
		{
			return -1;
		}
		else
		{
			//TTL减一继续转发
			rreq_al_ptr->TTL -= 1;
			head->snd_addr = LOCAL_ADDR;
			rreq_al_ptr->link[rreq_al_ptr->hop] = LOCAL_ADDR;
			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rreq_al_t);

			rreq_al_ptr->hop +=1;
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

			printf("node %d ,resend rreq-al from %d\n",LOCAL_ADDR,source_addr);

		}
	}
	//如果是组长节点直接返回，因为组长节点不参与辅助链路的建立
	else if(m_table.item[index].Ntype == LEADER)
	{
		return -1;
	}
	//如果是组成员节点则判断是否能回复RREP——AL
	else
	{
		//只有高地址的节点能够回复低地址的节点，所以如果不是高地址则不处理
		if(LOCAL_ADDR < source_addr)
		{
			return -1;
		}
		//判断辅助链路是否更短
		int i = 0,j = 0,hop_link = 0;
		while(m_table.item[index].leader_link[i] == rreq_al_ptr->leader_link[j])
		{
			i++;
			j++;
		}
		while(m_table.item[index].leader_link[i] != 0)
		{
			hop_link++;
			i++;
		}
		while(rreq_al_ptr->leader_link[j] != 0)
		{
			hop_link++;
			j++;
		}

		if(hop_link <= rreq_al_ptr->hop)
		{
			return -1;
		}

		//更优则回复RREP——AL
		mmsg_t snd_buff;
		memset(&snd_buff,0,sizeof(snd_buff));
		//为发送缓存赋初值
		snd_buff.mtype = MMSG_MAODV;
		snd_buff.node = LOCAL_ADDR;
		maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
		rrep_al_t* rrep_al_ptr = (rrep_al_t*)(snd_buff.data + sizeof(maodv_h));
		head_ptr->type = RREP_AL;
		head_ptr->series = get_series();
		head_ptr->source_addr = LOCAL_ADDR;
		head_ptr->snd_addr = LOCAL_ADDR;
		head_ptr->r_addr = rreq_al_ptr->link[rreq_al_ptr->hop - 1];
		rrep_al_ptr->m_addr = rreq_al_ptr->m_addr;
		//当前下标和路径总节点数，当前下标发送之前减一
		rrep_al_ptr->index = rreq_al_ptr->hop;
		rrep_al_ptr->node_num = rreq_al_ptr->hop;

		//拷贝RREQ记录的路径数组,注意要先把本节点加上
		rreq_al_ptr->link[rreq_al_ptr->hop] = LOCAL_ADDR;
		memcpy(rrep_al_ptr->link,rreq_al_ptr->link,sizeof(MADR)*MAX_HOP);

		//rrep-al添加到辅助链路表
		add_al(rrep_al_ptr);

		//发送rrep-al
		rrep_al_ptr->index -= 1;
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
		printf("return rrep-al for rreq-al from %d\n",source_addr);
	}

	return 0;
}

int rcv_rrep_al(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	rrep_al_t* rrep_al_ptr = (rrep_al_t*)(rcv_buff->data + sizeof(maodv_h));
	//先判断单播RREP-AL是否为正确的接受节点
    MADR r_addr = head->r_addr;
    if(r_addr != LOCAL_ADDR)
    {
    	return -1;
    }

    //rrep-al添加到辅助链路表
	add_al(rrep_al_ptr);

    if(rrep_al_ptr->index > 0)
    {
    	head->snd_addr = LOCAL_ADDR;
		head->r_addr = rrep_al_ptr->link[rrep_al_ptr->index - 1];
		//节点地址数组下标减一
		rrep_al_ptr->index -= 1;

		size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(rrep_al_t);
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

int rcv_hello_al(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	hello_al_t* ptr = (hello_al_t*)(rcv_buff->data + sizeof(maodv_h));
	//先判断单播RREP-AL是否为正确的接受节点
    MADR r_addr = head->r_addr;
    if(r_addr != LOCAL_ADDR)
    {
    	return -1;
    }

	//printf("node %d rcv hello-al msg form %d\n",LOCAL_ADDR,head->source_addr);

    MADR m_addr = ptr->m_addr;
	MADR first_addr = ptr->first_addr;
	MADR end_addr = ptr->end_addr;

    int index = check_link_table(m_addr, first_addr, end_addr);

    if(index == -1)
    {
    	return -1;
    }

    else if(index != -1)
    {

    	link_table.item[index].time = time(NULL);

    	if(link_table.item[index].right_addr != 0)
    	{
    		head->snd_addr = LOCAL_ADDR;
			head->r_addr = link_table.item[index].right_addr;

			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(hello_al_t);
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
    }

	return 0;
}

int add_al(rrep_al_t * ptr)
{
	MADR m_addr = ptr->m_addr;
	MADR first_addr = ptr->link[0];
	MADR end_addr = ptr->link[ptr->node_num];
	MADR left_addr = 0,right_addr = 0;
	int hop = ptr->node_num;

	if(ptr->index != 0)
	{
		left_addr = ptr->link[ptr->index - 1];
	}
	if(ptr->index != hop)
	{
		right_addr = ptr->link[ptr->index + 1];
	}


	int index = check_link_table(m_addr, first_addr, end_addr);
	if(-1 == index)
	{
		//若不存在表项，则找到第一个可用位置
		index = refresh_link_table(m_addr, first_addr, end_addr);
		if(-1 == index)
		{
			printf("add al_link item error!!!!\n");
			return -1;
		}
	}

	link_table.item[index].m_addr = m_addr;
	link_table.item[index].first_addr = first_addr;
	link_table.item[index].end_addr = end_addr;
	link_table.item[index].left_addr = left_addr;
	link_table.item[index].right_addr = right_addr;
	link_table.item[index].hop = hop;
	link_table.item[index].time = time(NULL);

	if(ptr->index == 0)
	{
		//定时器每秒发送hello
		t_table_add("snd_hello_al",1,0,snd_hello_al);
	}

	print_link_table();

	return 0;
}

int print_link_table()
{
	refresh_link_table();

	int i;
	link_item * pitem;
	printf("============= link_table & node %d ==============\n",LOCAL_ADDR);
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &link_table.item[i];
		if(pitem->time != 0 && pitem->m_addr != 0)
		{

			printf("item:%d ,m_addr=%d ,first_addr=%d ,end_addr=%d\n",i,pitem->m_addr,pitem->first_addr,pitem->end_addr);
			printf("left_addr=%d ,right_addr=%d ,hop=%d\n",pitem->left_addr,pitem->right_addr,pitem->hop);
			printf("TIME : %lu\n",pitem->time);
		}

	}

	return 0;
}

int snd_hello_al()
{
	mmsg_t snd_buff;
    memset(&snd_buff,0,sizeof(snd_buff));
    snd_buff.mtype = MMSG_MAODV;
    snd_buff.node = LOCAL_ADDR;
    maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
    hello_al_t* hello_al_ptr = (hello_al_t*)(snd_buff.data + sizeof(maodv_h));

	head_ptr->m_addr = 0;
    head_ptr->type = HELLO_AL;
    head_ptr->series = get_series();
    head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = 0xff;

	int i;
	link_item * pitem;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &link_table.item[i];
		if(pitem->first_addr == LOCAL_ADDR)
		{
			if(pitem->right_addr != 0)
			{
				pitem->time = time(NULL);

				head_ptr->m_addr = pitem->m_addr;
				head_ptr->r_addr = pitem->right_addr;
				hello_al_ptr->m_addr = pitem->m_addr;
				hello_al_ptr->first_addr = pitem->first_addr;
				hello_al_ptr->end_addr = pitem->end_addr;

				size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(hello_al_t);
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
			}
		}
	}

	return 0;
}

int check_link_table(MADR m_addr,MADR first_addr,MADR end_addr)
{
	refresh_mtable();
	int i;
	link_item * pitem;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &link_table.item[i];
		if(pitem->m_addr == m_addr && pitem->first_addr == first_addr && pitem->end_addr == end_addr)
		{
			return i;
		}
	}
	return -1;
}

int refresh_link_table()
{
	time_t 	ctime;
	ctime = time(NULL);
	int have_sign = 0;
	int ok_index = -1;
	int i;
	link_item * pitem;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		pitem = &link_table.item[i];
		if(pitem->time != 0 && ctime - pitem->time > 5)
		{
			//printf("link table item time :%lu\n",link_table.item[i].time);
			printf("ctime: %lu , item time : %lu\n",ctime,pitem->time);
			printf("clean link item: maddr:%d,node1:%d,node2:%d\n",pitem->m_addr,pitem->first_addr,pitem->end_addr);
			memset(pitem,0,sizeof(link_item));
		}
		//记录第一个可用位置
        if(!have_sign)
        {
            if(pitem->time == 0)
            {
                have_sign = 1;
                ok_index = i;
            }
        }
	}

	if(ok_index == -1)
	{
		printf("link table is already full\n");
	}

	return ok_index;
}

int rcv_al_data(mmsg_t * rcv_buff,int size)
{
	maodv_h * head_ptr= (maodv_h *)(rcv_buff->data);
	char* data_ptr = (char*)(rcv_buff->data + sizeof(maodv_h));
	MADR m_addr = head_ptr->m_addr;

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
	}

	int index_al = -1;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		link_item * pitem = &link_table.item[i];
		if(pitem->m_addr == m_addr)
		{
			index_al = i;
			break;
		}
	}
	if(-1 == index_al)
	{
		printf("have no al-link of %d\n",m_addr);
	}

	if(-1 == index && -1 == index_al)
	{
		return -1;
	}


	struct  timeval    tv;
	struct  timezone   tz;
	gettimeofday(&tv,&tz);

	if(m_addr == head_ptr->snd_addr)
	{
		printf("node : %d send al_data as source addr , at time :\n",LOCAL_ADDR);
		printf("tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);

		if(-1 == index)
		{
			printf("but node : %d is not in the tree,stop.\n",LOCAL_ADDR);
			return -1;
		}
		else if(m_table.item[index].up_node != 0 || m_table.item[index].low_num != 0)
		{
			printf("broadcast from node : %d again\n",LOCAL_ADDR);
		}
		else
		{
			printf("stop broadcast at node : %d\n",LOCAL_ADDR);
			return 0;
		}

		head_ptr->series = get_series();
		head_ptr->source_addr = LOCAL_ADDR;
		head_ptr->m_addr = m_addr;
		head_ptr->type = AL_DATA;
	}
	else
	{
		if(check_repeat(head_ptr->source_addr,head_ptr->series) < 0)
		{
			DEBUG("repeat al_data from %d\n",head_ptr->snd_addr);
			return -1;
		}
		printf("node : %d rcv al_data at sys time:\n",LOCAL_ADDR);
		printf("tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);

		if(index != -1 && (m_table.item[index].up_node != 0 || m_table.item[index].low_num != 0))
		{
			printf("broadcast from node : %d by tree link again\n",LOCAL_ADDR);
		}

		if(index_al != -1)
		{
			printf("broadcast from node : %d by al link again\n",LOCAL_ADDR);
		}

		else
		{
			printf("stop broadcast at node : %d\n",LOCAL_ADDR);
			return 0;
		}

	}

	head_ptr->snd_addr = LOCAL_ADDR;
	head_ptr->r_addr = 0xff;

	while(msgsnd(nl_qid, rcv_buff, size, IPC_NOWAIT) < 0)
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
			while( msgrcv(nl_qid, &temp_buff, size,0,IPC_NOWAIT) != -1 );
		}
		else
		{
			printf("maodv data send error!\n");
			return -1;
		}
	}

	return 0;
}

int rcv_mact_al(mmsg_t * rcv_buff)
{
	return 0;
}

int send_delete(int m_addr,int AL_HOP)
{
	printf("delete AL link hop : %d \n",AL_HOP);
	return 0;
}
