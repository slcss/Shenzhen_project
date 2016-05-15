#include "nl_common.h"
#include "nl_timer.h"
#include "nl_rcv.h"


static pthread_t mrx_tid = -1;		//�����߳�ID


MADR SRC_ADDR;




int main(int argc, char* argv[])  //�����Ǳ��ڵ��ַ
{
	if(argc < 2)
	{
		EPT(stderr, "!!! too less parameter in process netlayer! you need input my node id!\n");
		return -1;
	}

	int rval = 0, stop = 0;

	//��Ϣ���г�ʼ��
	mr_queues_init("netlayer");

	shm_init();

	SRC_ADDR = atoi(argv[1]);
	if (SRC_ADDR < 1 || SRC_ADDR > MAX_NODE_CNT)
	{
		EPT(stderr, "!!! please input righrt SRC_ADDR!\n");
		return -1;
	}


	/* create receiving msg thread */
	rval = pthread_create(&mrx_tid, NULL, nl_qrv_thread, &(qinfs[re_qin].qid));
	if (rval != 0)
	{
		EPT(stderr, "!!! netlayer: can not open create msg receiving thread\n");
		rval = 2;
		goto process_return;
	}

	rval = init_nl_hash();
	if(rval == 0)
	{
		EPT(stderr, "!!!doesn't have nl file\n");
	}

	/* start periodical timer */
	/*rval = nl_start_timer();
	if (0 != rval)
	{
		EPT(stderr, "%s: can not open start timer function\n", argv[1]);
		rval = 3;
		goto process_return;
	}*/

	pthread_mutex_lock(&share.mutex);		//�������̳߳��������£����߳̽���������ʼ�˳�
	while(0 == stop) {
		EPT(stderr, "netlayer: waiting for the exit of sub threads\n");
		pthread_cond_wait(&share.cond, &share.mutex);		//����������ǰ�̣߳��ȴ�����߳�ʹ��pthread_cond_signal()��������ͬʱ�ͷŻ���������
															//����ͻ�������ϣ���ֹ����߳�ͬʱ����pthread_cond_wait��
		EPT(stderr, "netlayer: share.qr_run = %d, share.gi_run = %d\n",  share.qr_run, share.gi_run);
		if (share.qr_run == 0 || share.gi_run == 0) {
			if (share.qr_run == 0) EPT(stderr, "msg receiving thread quit\n");
			if (share.gi_run == 0) EPT(stderr, "input ctching thread quit\n");
			stop = 1;
			continue;
		}
	}
	EPT(stderr, "layer: certain thread quit\n");
	pthread_mutex_unlock(&share.mutex);

process_return:
	sleep(1);
	mr_queues_delete();			//ɾ��������Ϣ����
	exit(rval);					//�˳�����
}

