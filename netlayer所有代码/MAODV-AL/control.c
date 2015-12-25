
#include "maodv.h"

mmsg_t snd_buff;

int data_test();
int msg_queues_init();

MADR addr1 = 0;
MADR addr2 = 0;

static int qid_test[top_line];


int test()
{
	snd_buff.mtype = MMSG_MAODV;
	snd_buff.node = 1;

	maodv_h * head = (maodv_h *)(snd_buff.data);
	char* data_ptr = (char*)(snd_buff.data + sizeof(maodv_h));

	head->snd_addr = 1;
	head->r_addr = 0;

	msg_queues_init();

	size_t len = 50;
	while(msgsnd(qid_test[0], &snd_buff, len, IPC_NOWAIT) < 0)
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
			return -1;
		}
	}
	printf("quit test\n");

}

//第一哥参数为控制节点地址，第二个参数为控制指令，第三个参数为组播地址，第四个参数为要添加或者删除的辅助链路跳数
int main(int argc, char *argv[])
{
	maodv_h * head;

    if (argc < 4)
	{
		printf("parameter1: control_addr\nparameter3: m_addr\n");
		snd_buff.mtype = MMSG_MAODV_CONTROL;
		addr1 = atoi(argv[1]);

		//return -1;
	}
	else
	{
		//要控制的节点地址
	    addr1 = atoi(argv[1]);

		//要加入的组播地址
	    addr2 = atoi(argv[3]);

	    if(0 == strncmp(argv[2],"al_data",7))
		{

			snd_buff.mtype = MMSG_MAODV;
			head = (maodv_h *)(snd_buff.data);
			char* data_ptr = (char*)(snd_buff.data + sizeof(maodv_h));
			head->m_addr = addr2;
			head->type = AL_DATA;
			head->snd_addr = addr2;
			head->r_addr = addr1;

			memcpy(data_ptr,"al_data_test",12);

			struct  timeval    tv;
			struct  timezone   tz;
			gettimeofday(&tv,&tz);
			//精确到秒和微秒
			printf("al_data snd time : tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);
		}

		else if(0 == strncmp(argv[2],"data",4))
		{

			snd_buff.mtype = MMSG_MAODV;
			head = (maodv_h *)(snd_buff.data);
			char* data_ptr = (char*)(snd_buff.data + sizeof(maodv_h));
			head->m_addr = addr2;
			head->type = DATA;
			head->snd_addr = addr2;
			head->r_addr = addr1;

			memcpy(data_ptr,"data test",9);

			struct  timeval    tv;
			struct  timezone   tz;
			gettimeofday(&tv,&tz);
			//精确到秒和微秒
			printf("tv_sec:%lu  ,  tv_usec:%lu\n",tv.tv_sec,tv.tv_usec);
			//printf("tz_minuteswest:%d\n",tz.tz_minuteswest);
			//printf("tz_dsttime:%d\n",tz.tz_dsttime);

			time_t now;    //实例化time_t结构
			struct tm  *timenow;    //实例化tm结构指针
			time(&now);//time函数读取现在的时间(国际标准时间非北京时间)，然后传值给now
			timenow = localtime(&now);//localtime函数把从time取得的时间now换算成你电脑中的时间(就是你设置的地区)
			printf("Local time is %s\n",asctime(timenow));//asctime函数把时间转换成字符，通过printf()函数输出

		}

		else
		{
			control_t * control_ptr = (control_t *)(snd_buff.data);

			snd_buff.mtype = MMSG_MAODV_CONTROL;

			memcpy(control_ptr->type,argv[2],6);

			if(0 == strncmp(argv[2],"build",5) || 0 == strncmp(argv[2],"delete",6))
			{
				control_ptr->AL_HOP =  atoi(argv[4]);
			}
			printf("control msg snd\n");
		}

	}

	snd_buff.node = addr2;

    msg_queues_init();

    printf("control addr:%d , %s  ,maddr :%d\n",addr1,snd_buff.data,addr2);

	size_t snd_len = 100;
    while(msgsnd(qid_test[addr1], &snd_buff,snd_len , IPC_NOWAIT) < 0)
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
			while( msgrcv(qid_test[addr1], &temp_buff, snd_len,0,IPC_NOWAIT) != -1 );
		}
		else
		{
			perror("ABC");
			return -1;
		}
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
		//printf("qid_test of %d : %d\n",i,qid_test[i]);
	}
	return 0;
}

int data_test()
{


	return 0;
}

int control_test()
{


	return 0;
}
