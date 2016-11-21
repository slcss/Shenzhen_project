#include "mr_common.h"
#include "netlayer.h"

extern qinfo_t qinfs[];

static pthread_t fq_tid = -1;



int main(int argc, char* argv[])
{
	int rval;
	rval = pthread_create(&fq_tid, NULL, mr_queues_init, argv[0]);

	if (rval != 0)
	{
		EPT(stderr, "%s: can not create getting queue init thread\n", argv[0]);
		rval = 3;
		
	}
	sleep(1);   /* 保证数据队列初始化完成 */

	while(1)
	{
		char data[200];
		mmsg_t msg;
		msg.mtype = MMSG_RPM;
		msg.node = 0;
		memcpy(msg.data, data, 200);
		rval = msgsnd(qinfs[1].qid, (void *)(&msg), 200 + 1, 0);

		if (rval != 0)
		{
			EPT(stderr, "netlayer: snd msg failed,errno = %d[%s]\n", errno, strerror(errno));
			rval = 1;
		}
		usleep(2000);
		//sleep(1);
	}
	
}














