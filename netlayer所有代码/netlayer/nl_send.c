#include "nl_send.h"

static int lock = 0;								  //c��û��bool���ͣ����ﶨ���int��, nl_send_to_others������lock
static int lock_of_himac = 0;						  //nl_send_to_himac������lock

nl_buff_pool_t  *nl_buf_pool;						  //��̬�������himac�Ľ��ջ��棬������Ϊnl_buff_num��ѭ��ʹ�ã���������pkt��
int nl_buff_num = 5;
static int nl_buff_timeout = 5;

int combine_send_pkt(nl_package_t * pkt)
{
	int H, SN;
	H = get_H(pkt);
	SN = get_SN(pkt);
	/*************************************************��������ֵİ���ֱ��ת��******************************************************/
	if(H == 1 && SN == 0)
	{
		printf("sigle pkt\n");
		mmsg_t * snd_buf;
		snd_buf = (mmsg_t *)malloc(sizeof(mmsg_t));
		if (snd_buf == NULL)
			EPT(stderr, "malloc error when deal sigle pkt\n");
		memset(&snd_buf, sizeof(snd_buf), 0);
		snd_buf->mtype = get_data_type(pkt);
		snd_buf->node = get_src_addr(pkt);
		memcpy(&snd_buf->data, pkt->data + sizeof(mmhd_t), get_data_length(pkt));
		nl_send_to_others(snd_buf,get_data_length(pkt));
		free(snd_buf);
		snd_buf == NULL;
		return 0;
	}
	/*************************************************ԤԼ���߻�ȡpkt������******************************************************/
	nl_buff_t * pkt_buf;
	int pool_id ;
	int key = get_data_type(pkt) + 10*get_src_addr(pkt);
	while(pool_id = manage_nl_buf(key) == -1)
	{
		EPT(stderr, "order a nl_buf error!!maybe full used!!\n");
		sleep(1);
	}
	pkt_buf =&((&nl_buf_pool[pool_id])->nl_buf);
	/******************************************************����pkt��************************************************************/
	if (H == 1)
	{
		pkt_buf->number = SN;
		pkt_buf->count = 1;
		memcpy(pkt_buf->package[0], pkt, sizeof(nl_package_t));					//����Խ��û�����⣬��Ϊ����Ŀռ仹���ڽ���buff��ֵΪ0
	}
	else
	{
		pkt_buf->count++;
		memcpy(pkt_buf->package[SN-1], pkt, sizeof(nl_package_t));
	}
	//if (pkt_buf->number == SN && pkt_buf->count == SN)
	//if(pkt_buf->number == SN)
	if (pkt_buf->number == pkt_buf->count)
	{
		int i,len = 0;
		U16 data_len;
		nl_package_t *tmp_ptr;
		mmsg_t * snd_buf;
		snd_buf = (mmsg_t *)malloc(sizeof(mmsg_t));
		if (snd_buf == NULL)
			return;
		snd_buf->mtype = get_data_type(pkt);
		snd_buf->node = get_src_addr(pkt);
		char * ptr = snd_buf->data;
		for (i=0; i<pkt_buf->number; i++)
		{
			tmp_ptr = (nl_package_t*)(pkt_buf->package[i]);
			data_len = get_data_length(tmp_ptr);
			memcpy(ptr, tmp_ptr->data + sizeof(mmhd_t), data_len);
			ptr += data_len;
			len += data_len;
		}

		nl_send_to_others(snd_buf,len);
		free(snd_buf);
		snd_buf == NULL;
	}
	return 0;
}

//����snd_msg��ͬ���������ͷ��͵���Ӧ�Ľ���
int nl_send_to_others(mmsg_t *snd_msg, U16 length)
{
	while(lock)					//��֤ͳһʱ��ֻ�иú���ֻ��һ���������
	{
		sleep(1);
	}
	lock = 1;

	int qid;

	switch(snd_msg->mtype)
	{
		case MMSG_IP_DATA:
			qid = vi_qid;
			snd_msg->mtype = MMSG_IP_DATA;
			break;
		case MMSG_RPM:
			qid = rp_qid;
			snd_msg->mtype = MMSG_RPM;
			break;
        case MMSG_FT_DATA:
            qid = rp_qid;
			snd_msg->mtype = MMSG_FT_DATA;
			break;
		case MMSG_MAODV:
			qid = ma_qid;
			snd_msg->mtype = MMSG_MAODV;
			break;
		case MMSG_SMS_TEST:
			printf("rcv sms from %d:\n",snd_msg->node);
			printf("%s\n",snd_msg->data);
			return 0;
		default:
			qid = -1;
			break;
	}
	while(msgsnd(qid, snd_msg, length + sizeof(MADR), 0) < 0)
	{
		if (errno == EINTR)
			continue;
		else
			{
				EPT(stdout, "%s:------snd to himac wrong------------\n", qinfs[re_qin].pname);
				return -1;
			}
	}

	printf("nl,send\n");

	lock = 0;
	return 0;
}

//����Ϣ���ݾ�����װ�ֽⷢ�͵�himac��
int nl_send_to_himac(mmsg_t *msg,int len)
{
	while(lock_of_himac)									//��֤ͬһʱ��ֻ�иú���ֻ��һ���������
	{
		sleep(1);
	}
	lock_of_himac = 1;

	mmsg_t * snd_buf;
	snd_buf = (mmsg_t *)malloc(sizeof(mmsg_t));
	if (snd_buf == NULL)
		EPT(stderr, "malloc error when deal sigle pkt\n");
	memset(&snd_buf, sizeof(snd_buf), 0);
	//snd_buf->mtype = MMSG_MP_DATA;							//��������Ϊ������㵽HIMAC

	//һ�����������ͣ�MMSG_IP_DATA��MMSG_FT_DATA��MMSG_RPM,MMSG_MAODV
	snd_buf->mtype = msg->mtype;
	snd_buf->node = msg->node;

	char *ptr = NULL;
	int left,n;
	int count = 2;
	nl_package_t * pkt;
	pkt = (nl_package_t *)(snd_buf->data);					//��pkt����snd_buf��data����
	init_package_head(pkt,msg->mtype,msg->node);			//��ʼ��ͷ��

	//int length = ((mmhd_t *)msg->data)->len;
	//��Ϣ����data�ĳ���
	int length =len;
	left = length;
	ptr = (char *)(msg->data);
	while(left > 0)
	{
		if (left > (MAX_PACKAGE_DATA_LENGTH -sizeof(mmhd_t)))
		{
			n = MAX_PACKAGE_DATA_LENGTH -sizeof(mmhd_t);
		}
		else
		{
			n = left;
		}

		set_data_length(pkt, n);

		if (left == length)									//����ĵ�һ��pkt
		{
		    printf("this is the pkt : 1\n");
			set_H(pkt, 1);
			if(length >(MAX_PACKAGE_DATA_LENGTH -sizeof(mmhd_t)))
				set_SN(pkt,(length-1)/(MAX_PACKAGE_DATA_LENGTH -sizeof(mmhd_t))+1);//�����յĳ���С��pkt����,��Ĭ��SN = 0
		}
		else
		{
		    printf("this is the pkt : %d\n",count);
			set_H(pkt, 0);
			set_SN(pkt,count++);
		}
		memcpy(pkt->data + sizeof(mmhd_t), ptr, n);
		ptr += n;
		left -= n;
        printf("pkt data : %s\n",pkt->data + sizeof(mmhd_t));
		int size_for_snd = sizeof(MADR) + sizeof(mmhd_t)  + 8 + n;
		while(msgsnd(hm_qid, snd_buf, size_for_snd, 0) < 0)
		{
			if (errno == EINTR)
				continue;
			else
				{
					EPT(stdout, "%s:------snd to himac wrong------------\n", qinfs[re_qin].pname);
					break;
				}
		}

		/*nl_package_t * pkt_1 = (nl_package_t *)snd_buf->data;
		combine_send_pkt(pkt_1);

		printf("here2\n");*/

	}

	free(snd_buf);
	snd_buf == NULL;
	lock_of_himac = 0;
	return 0;
}

int manage_nl_buf(int key)				//����nl_buff������key�������Ӧ��buff_pool���
{
	time_t 	ctime;
	ctime = time(NULL);
	int id;

	for(id = 0;id < nl_buff_num;id++)
	{
		if((&nl_buf_pool[id])->flag == 1 && (&nl_buf_pool[id])->KEY == key)
		{
			(&nl_buf_pool[id])->time = ctime;
			return id;
		}
	}

	for(id = 0;id < nl_buff_num;id++)
	{
		if(ctime - (&nl_buf_pool[id])->time > nl_buff_timeout)
			(&nl_buf_pool[id])->flag = 0;
		if((&nl_buf_pool[id])->flag == 0)		//����ҵ����л�������ղ�ԤԼ�������
		{
			memset(&nl_buf_pool[id], sizeof(nl_buf_pool), 0);
			(&nl_buf_pool[id])->KEY = key;
			(&nl_buf_pool[id])->flag = 1;
			(&nl_buf_pool[id])->time = ctime;
			return id;
		}
	}

	return -1;
}
