#include "mr_common.h"
#include "netlayer.h"

extern qinfo_t qinfs[];

static pthread_t fq_tid = -1;



int main(int argc, char* argv[])
{
	int rval,count = 0;
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, argv[0]);

	if (rval != 0)
	{
		EPT(stderr, "%s: can not create getting queue init thread\n", argv[0]);
		rval = 3;
		
	}
	sleep(1);   /* 保证数据队列初始化完成 */

	
	char data[200];
	memset(data, 0, 200);
	mmsg_t msg;
	msg.mtype = MMSG_IP_DATA;
	msg.node = 0;
	memcpy(msg.data, data, 200);
	while(1)
	{
		count++;
		rval = msgsnd(qinfs[1].qid, (void *)(&msg), 200 + 1, 0);

		if (rval != 0)
		{
			EPT(stderr, "netlayer: snd msg failed,errno = %d[%s]\n", errno, strerror(errno));
			rval = 1;
		}
#if 0
		if(count<2000)
		 	usleep(10000);
		else if(count<5000)
			usleep(20000);
		else if(count<9500)
			usleep(7000);
		else
			usleep(10000);
#endif	
#if 0
		if(count<1500)
			usleep(10000);
		else
			usleep(16000);
#endif	
		usleep(10000);

		//sleep(1);
	}
	
}














