#include "mr_common.h"

int main(int argc, char* argv[])
{
	mmsg_t snd_buff;
	snd_buff.mtype = MMSG_SMS_TEST;

	if(argc < 2)
		printf("please input local_addr\n");
	int local_addr = atoi(argv[1]);

	mr_queues_init("maodv");

	printf("please input sms:\n");
	char str[100];
	gets(str);
	printf("input :%s\n",str);


	printf("please input dest addr\n");
	int dest_addr;
	scanf("%d",&dest_addr);
	snd_buff.node = dest_addr;
	printf("dest addr :%d\n",snd_buff.node);

	memcpy(snd_buff.data,str,strlen(str)+1);
	printf("snd buff: %s\n",snd_buff.data);

	while(msgsnd(nl_qid, &snd_buff, 100, 0) < 0)
	{
		if (errno == EINTR)
			continue;
		else
			{
				printf("sms send error!\n");
				return -1;
			}
	}

	return 0;
}
