#include "nl_rcv.h"


void* nl_qrv_thread(void *arg)		  //����Ϊ�����߳�id
{
	int qid, rcnt;

	int rval = 0;

	nl_buf_pool = (nl_buff_pool_t *)malloc(nl_buff_num * sizeof(nl_buff_pool_t));	//Ϊ����pkt�Ŀɸ��û����������ռ�

	pthread_detach(pthread_self());	  //�߳���Ϊ������������������

	qid = *(int *)arg;				  //��ý����߳�id
	ASSERT(qinfs[re_qin].qid == qid);
	if (qid < 0)
	{
		EPT(stdout, "%s: wrong receive queue id %d", qinfs[re_qin].pname, qid);
		rval = 1;
		goto thread_return;
	}

	//����pkt�Ĵ�ȡ�͵�ַ����
	/*nl_package_t * pkt;
	pkt = (nl_package_t *)malloc(sizeof(nl_package_t));
	set_PR(pkt,0);
	set_TYPE(pkt,0);
	set_SubT(pkt,3);
	set_src_addr(pkt,SRC_ADDR);
	set_dst_addr(pkt,4);
	set_rcv_addr(pkt,5);
	set_snd_addr(pkt,SRC_ADDR);
	set_SEQ(pkt);
	set_H(pkt,1);
	set_SN(pkt,4);
	set_TTL(pkt,7);
	set_CoS(pkt,0);
	set_ACK(pkt,0);
	strcpy(pkt->data,"hello");
	set_CRC(pkt,99);
	printf("PR = %d \n",get_PR(pkt));
	printf("addr of pkt is : %p\n",pkt);
	printf("TYPE = %d \n",get_TYPE(pkt));
	printf("SubT = %d \n",get_SubT(pkt));
	printf("src_addr = %d \n",get_src_addr(pkt));
	printf("addr of src_addr is : %p\n",&(pkt->src_addr));
	printf("dst_addr = %d \n",get_dst_addr(pkt));
	printf("addr of dst_addr is : %p\n",&(pkt->dst_addr));
	printf("rcv_addr = %d \n",get_rcv_addr(pkt));
	printf("addr of rcv_addr is : %p\n",&(pkt->rcv_addr));
	printf("snd_addr = %d \n",get_snd_addr(pkt));
	printf("addr of snd_addr is : %p\n",&(pkt->snd_addr));
	printf("SEQ = %d \n",get_SEQ(pkt));
	printf("H = %d \n",get_H(pkt));
	printf("SN = %d \n",get_SN(pkt));
	printf("TTL = %d \n",get_TTL(pkt));
	printf("CoS = %d \n",get_CoS(pkt));
	printf("ACK = %d \n",get_ACK(pkt));
	printf("data = %s \n",pkt->data);
	printf("addr of data is : %p\n",pkt->data);
	printf("size of data is : %d\n",sizeof(pkt->data));
	printf("length of data is : %d\n",strlen(pkt->data));
	printf("CRC = %d \n",get_CRC(pkt));
	printf("addr of CRC is : %p\n",&(pkt->CRC));*/
	//����mmsg_t���ڴ����
	/*mmsg_t *test;
	test = (mmsg_t *)malloc(sizeof(mmsg_t));
	test->mtype = 3;
	test->mdest = 5;
	strcpy(test->data,"hello,mmsg_t test");
	printf("addr of mtype is : %p\n",&(test->mtype));
	printf("addr of mdest is : %p\n",&(test->mdest));
	printf("addr of data is : %p\n",test->data);
	printf("data = %s\n",test->data);
	printf("size of data is : %d\n",sizeof(test->data));
	printf("length of data is : %d\n",strlen(test->data));*/
	
	mmsg_t rcv_msg;						  //�ö���������Ϣ�������ݵ�ֱ�Ӵ洢��ֻ��Ҫһ��rcvbuff
	while(1)
	{
	//    printf("===========nl_layer waiting for data from nl_layer==========\n");
		memset(&rcv_msg, 0, sizeof(rcv_msg));								//���ջ���ÿ��ʹ��ǰ��գ���Ϊֻ����һ�����ջ���
		rcnt = msgrcv(qid, &rcv_msg, sizeof(mmsg_t) - sizeof(long), 0, 0);	//�ɹ����ؿ������ṹ�����ݲ��ֵ��ֽ���,ʧ�ܷ���-1
		if (rcnt < 0)
		{
			EPT(stderr, "!!!NL rcv < 0 error\n");
			if (errno == EINTR)												//ϵͳ��������ʱ,���ź��ն�,�������¿�ʼ����
				continue;
			if(errno == E2BIG)
				EPT(stderr, "%s:E2BIG!!\n", qinfs[re_qin].pname);
			if(errno == EACCES)
				EPT(stderr, "%s:EACCES!!\n", qinfs[re_qin].pname);
			if(errno == EAGAIN)
				EPT(stderr, "%s:EAGAIN!!\n", qinfs[re_qin].pname);
			if(errno == EFAULT)
				EPT(stderr, "%s:EFAULT!!\n", qinfs[re_qin].pname);
			if(errno == EIDRM)
				EPT(stderr, "%s:EIDRM!!\n", qinfs[re_qin].pname);
			if(errno == EINVAL)
				EPT(stderr, "%s:EINVAL!!\n", qinfs[re_qin].pname);
			if(errno == ENOMSG)
				EPT(stderr, "%s:ENOMSG!!\n", qinfs[re_qin].pname);
			EPT(stderr, "%s:rcv wrong!!\n", qinfs[re_qin].pname);
			rval = 2;
			goto thread_return;
		}
	//	EPT(stdout, "%s:---- reveived---- %d \n", qinfs[re_qin].pname, rcnt);
	//	printf("nl_layer rcv mtype: %ld\n",rcv_msg.mtype);
	//	printf("nl_layer rcv dest: %d\n",rcv_msg.node);

		
	//4.4	EPT(stderr, "#nl rcnt mtype: %ld \n",rcv_msg.mtype);
	//4.4	EPT(stderr, "#nl rcnt: %d ",rcnt);

		if(MMSG_HM_DATA == rcv_msg.mtype)		        //�����HMAC������,�ͽ������鴦��,waiting for add MMSG_FT_REP
		{
		
		//4.4		EPT(stderr, "from HM\n");
			
			nl_package_t * pkt = (nl_package_t *)rcv_msg.data;
			combine_send_pkt(pkt, rcnt - sizeof(MADR));
		}
		else
		{
//2.26		    printf("ready to send to hihmac \n");
		
		//4.4		EPT(stderr, "to HM\n");
			
			nl_send_to_himac(&rcv_msg, rcnt - sizeof(MADR));
		}
	}

thread_return:
	EPT(stderr, "!!!NL over...\n");
	pthread_mutex_lock(&share.mutex);		//ʹ�û���������֤������ԭ���ԣ������ж�
	free(nl_buf_pool);
	nl_buf_pool = NULL;
	share.qr_run = 0;
	pthread_cond_signal(&share.cond);		//�������̵߳��˳����������߳��˳���ͨ��qr_run = 0��֪�ǽ����̳߳���
	pthread_mutex_unlock(&share.mutex);
	sleep(1);
	pthread_exit((void *)&rval);
}




