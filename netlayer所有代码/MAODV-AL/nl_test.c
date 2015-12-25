#include "maodv.h"

int top_init();
void *rcv_thread(void *arg);
int msg_queues_init();



static int top[top_line][top_line];
static int qid_test[top_line];

mmsg_t rcv_buff;


int main(int argc, char* argv[])
{
	mr_queues_init("netlayer");

	top_init();
	msg_queues_init();

	pthread_t 	tid;
	if (pthread_create(&tid, NULL, rcv_thread, NULL) < 0)
    {
		printf("maodv rcv pthread create failed\n");
		return -1;
	}

	while(1);
	return 0;
}


void *rcv_thread(void *arg)
{
	int rcnt;
	while(1)
	{
	    //printf("===========nl_test waiting for data ==========\n");
		memset(&rcv_buff, sizeof(rcv_buff), 0);								//接收缓存每次使用前清空，因为只有这一个接收缓存
		rcnt = msgrcv(qid_test[0], &rcv_buff, sizeof(mmsg_t) - sizeof(long), 0, 0);	//成功返回拷贝到结构体数据部分的字节数,失败返回-1

		if (rcnt < 0)
		{
			perror("rcv error");
			//return ;
			continue;
		}

		maodv_h * head= (maodv_h *)(rcv_buff.data);
		MADR snd_addr = head->snd_addr;
		MADR r_addr = head->r_addr;

		switch(head->type)
		{
			case RREQ:
				//广播需要判断重复
				printf("rcv rreq_msg from %d\n",head->snd_addr);
				break;
			case RREP:
				//单播不需要判断重复
				printf("rcv rrep_msg from %d\n",head->snd_addr);
				break;
			case MACT:
				printf("rcv mact_msg from %d\n",head->snd_addr);
				break;
			case GRPH:
				//printf("rcv grph_msg from %d\n",head->snd_addr);
				break;
			case DATA:
				printf("rcv data_msg from %d\n",head->snd_addr);
				break;
			default:
				printf("rcv unknown type from %d\n",head->snd_addr);
				break;
		}

		if(r_addr == 0xff)
		{
			//printf("broadcast\n");
			int i;
			//注意第0列只是标号，要从第一列开始遍历
			for(i=1;i<top_line;i++)
			{
				if(top[snd_addr][i] == 1)
				{
					while(msgsnd(qid_test[i], &rcv_buff, rcnt, IPC_NOWAIT) < 0)
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
							while( msgrcv(qid_test[i], &temp_buff,sizeof(mmsg_t) - sizeof(long),0,IPC_NOWAIT) != -1 );

						}
						printf("control send to maodv error!\n");
					}
				}
			}
		}

		else
		{
			printf("try snd data from %d to %d\n",snd_addr,r_addr);
			if((snd_addr<1 || snd_addr > 9) || (r_addr<1 || r_addr > 9))
			{
				//return -1;
				printf("wrong addr\n");
				continue;
			}

			if(top[snd_addr][r_addr] == 1)
			{
				printf("find link , snd out \n");
				while(msgsnd(qid_test[r_addr], &rcv_buff, rcnt, IPC_NOWAIT) < 0)
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
						while( msgrcv(qid_test[r_addr], &temp_buff, sizeof(mmsg_t) - sizeof(long),0,IPC_NOWAIT) != -1 );
					}
					else
					{
						perror("snd error");
						return;
					}
				}
			}
			else
			{
				printf("snd data from %d to %d error : no link\n",snd_addr,r_addr);
			}
		}
	}
}

int top_init()
{
	FILE *fp = NULL;
	//a+方式打开，不清空源文件内容，只在末尾追加
	fp = fopen("top.txt", "a+");
	if (NULL == fp)
	{
		printf("open top.txt error !\n");
		return -1;
	}

	int i,j;
	//scanf用空白字符结尾，包括空格和换行
	for(i=0;i<top_line;i++)
	{
		for(j=0;j<top_line;j++)
		{
			fscanf(fp, "%d", &top[i][j]);
			printf("%d ",top[i][j]);
		}
		printf("\n\n");
	}

	return 0;
}
int msg_queues_init()
{

	int i;
	for(i=0;i<top_line;i++)
	{
		int seq = 100 + i;
		key_t key_test = ftok(PATH_CREATE_KEY, seq);
		if(key_test<0)
		{
			printf("get key_test error!\n");
			return -1;
		}
		qid_test[i] = msgget(key_test, IPC_CREAT|QUEUE_MODE);
		if(qid_test[i] == -1)
		{
			printf("get qid_test %d error!\n",i);
			return -1;
		}
		printf("qid_test of %d : %d\n",i,qid_test[i]);
	}
	return 0;
}



