#include "maodv.h"


int quit(MADR m_addr)
{
	printf("try to quit %d ...\n",m_addr);
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

	mmsg_t snd_buff;
	memset(&snd_buff,0,sizeof(snd_buff));
	//为发送缓存赋初值
	snd_buff.mtype = MMSG_MAODV;
	snd_buff.node = LOCAL_ADDR;
	maodv_h* head_ptr = (maodv_h*)(snd_buff.data);
	mact_t* mact_ptr = (mact_t*)(snd_buff.data + sizeof(maodv_h));
	head_ptr->type = MACT;
	head_ptr->series = get_series();
	head_ptr->source_addr = LOCAL_ADDR;
	head_ptr->snd_addr = LOCAL_ADDR;
	mact_ptr->m_addr = m_addr;
	mact_ptr->flag = PRUNE;

	size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(mact_t);

    //非组长节点
    if(m_table.item[index].Ntype != LEADER)
    {
    	//非叶子节点，直接变为路由节点
    	if(m_table.item[index].low_num > 0)
    	{
			printf("not leave ,turn type ROUTE\n");
    		m_table.item[index].Ntype = ROUTE;
    		printf("%d quit %d already\n",LOCAL_ADDR,m_addr);
    		print_mtable();
    		return 0;
    	}
    	//叶子节点，删除表项并向上游单播MACT-P报文
    	else
    	{
    		printf("leave node , clean item and snd MACT-P up\n");

    		//向上游单播MACT
			head_ptr->r_addr = m_table.item[index].up_node;

			//删除表项
			memset(&m_table.item[index],0,sizeof(maodv_item));

			print_mtable();

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
					printf("maodv mact-p send error!\n");
					return -1;
				}
			}
			printf("%d quit %d already\n",LOCAL_ADDR,m_addr);
    	}
    }
	//如果是组长节点要退出
    else if(m_table.item[index].Ntype == LEADER)
    {
    	//如果只有一个单独组长，则直接删除表项即可
		if(m_table.item[index].low_num == 0)
		{
			printf("single leader , quit %d directly\n",m_addr);
			//删除表项
			memset(&m_table.item[index],0,sizeof(maodv_item));
			leader_list.Lhead = del(leader_list.Lhead,m_addr);
			leader_list.num--;
			if(0 == leader_list.num)
			{
				t_table_del("broadcast_grph");
			}
			printf("%d quit %d already\n",LOCAL_ADDR,m_addr);
			print_mtable();
			return 0;
		}
		//如果不是单独的组长节点
		else if(m_table.item[index].low_num >= 1)
		{
			//选中下游第一个（最左边节点）尝试为新的组长节点
			head_ptr->r_addr = m_table.item[index].Lhead->m_nValue;

			if(m_table.item[index].low_num == 1)
			{
				printf("leaf leader , quit %d and choose child as new leader\n",m_addr);
				//删除表项
				memset(&m_table.item[index],0,sizeof(maodv_item));
			}
			else if(m_table.item[index].low_num > 1)
			{
				printf("turn to route node of %d ,choose left child as new leader\n",m_addr);

				//标志位设为PG，从多个下游节点中选出新组长，并且本节点不退出，只设置为ROUTE
				mact_ptr->flag = PG;
				m_table.item[index].Ntype = ROUTE;
				//下游节点变为上游节点
				m_table.item[index].up_node = m_table.item[index].Lhead->m_nValue;
				//删除改变的下游节点
				m_table.item[index].Lhead = del(m_table.item[index].Lhead,m_table.item[index].Lhead->m_nValue);

			}

			leader_list.Lhead = del(leader_list.Lhead,m_addr);
			leader_list.num--;
			if(0 == leader_list.num)
			{
				t_table_del("broadcast_grph");
			}

			print_mtable();

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
					printf("maodv mact-p send error!\n");
					return -1;
				}
			}
			printf("%d quit %d already\n",LOCAL_ADDR,m_addr);

		}

    }
	return 0;
}
//收到MACT-P或者MACT-PG报文
int rcv_mact_p(mmsg_t * rcv_buff)
{
	maodv_h * head= (maodv_h *)(rcv_buff->data);
	mact_t* mact_ptr = (mact_t*)(rcv_buff->data + sizeof(maodv_h));

	printf("deal mact-p from %d\n",head->snd_addr);

	int index = -1;
	int i;
	for(i = 0;i < MAX_MAODV_ITEM;i++)
	{
		maodv_item item = m_table.item[i];
		//如果查到表项
		if(mact_ptr->m_addr == item.m_addr && item.is_alive == 1)
		{
			index = i;
			break;
		}
	}
	if(-1 == index)
	{
		printf("have no item of %d\n",mact_ptr->m_addr);
		return -1;
	}

	if(head->snd_addr == m_table.item[index].up_node)
	{
		printf("MACT-P from up_node\n");

		if(m_table.item[index].Ntype == ROUTE)
		{
			//下游节点变为上游节点,继续发送MACT-P或者MACT-PG
			m_table.item[index].up_node = m_table.item[index].Lhead->m_nValue;
			rcv_buff->node = LOCAL_ADDR;
			head->snd_addr = LOCAL_ADDR;
			head->r_addr = m_table.item[index].Lhead->m_nValue;
			//先取出下游节点作为接受节点，再添加上游节点为下游节点,前提是上游节点没有真正退出，否则不添加
			if(mact_ptr->flag == PG)
			{
				m_table.item[index].low_num++;
				m_table.item[index].Lhead = insert(m_table.item[index].Lhead,head->snd_addr);
			}

			if(m_table.item[index].low_num > 1)
			{
				mact_ptr->flag = PG;
				printf("snd mact-pg to down-node : %d\n",head->r_addr);
			}
			//如果本节点为ROUTE，且下游节点只有一个，则本节点的该表项清空，并且设置flag为PRUNE
			else
			{
				memset(&m_table.item[index],0,sizeof(maodv_item));
				mact_ptr->flag = PRUNE;
				printf("snd mact-p to down-node : %d\n",head->r_addr);
			}


			size_t len = sizeof(MADR) + sizeof(maodv_h) + sizeof(mact_t);
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
					printf("maodv mact-p send error!\n");
					return -1;
				}
			}
		}
		if(m_table.item[index].Ntype == MEMBER)
		{
			//成为新的组长节点
			m_table.item[index].Ntype = LEADER;
			m_table.item[index].up_node = 0;
			m_table.item[index].hop = 0;
			m_table.item[index].l_addr = LOCAL_ADDR;
			//添加上游节点为下游节点,前提是上游节点没有真正退出，否则不添加
			if(mact_ptr->flag == PG)
			{
				m_table.item[index].low_num++;
				m_table.item[index].Lhead = insert(m_table.item[index].Lhead,head->snd_addr);
			}

			//添加组长列表
			leader_list.Lhead = insert(leader_list.Lhead,mact_ptr->m_addr);
			leader_list.num++;
			//当第一次调用tree_init函数时开启定时器，广播通知GRPH
			if(1 == leader_list.num)
			{
				//将广播GRPH的函数加入定时器，周期为1，次数为无限次
				t_table_add("broadcast_grph",1,0,broadcast_grph);
			}
		}
	}
	//来自下游节点的剪枝请求
	else
	{
		printf("MACT-P from down_node\n");
		m_table.item[index].Lhead = del(m_table.item[index].Lhead,head->snd_addr);
		m_table.item[index].low_num--;
		//如果剪去下游节点后本节点变为叶子节点且本节点为路由节点，则继续剪枝
		if(m_table.item[index].low_num == 0 && m_table.item[index].Ntype == ROUTE)
		{
			printf("go on quit process\n");
			quit(mact_ptr->m_addr);
		}

	}
	return 0;
}








