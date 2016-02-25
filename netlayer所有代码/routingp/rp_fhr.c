#include "mr_common.h"
#include "rp_common.h"
#include "rp_fhr.h"


extern rtable_t rt;
extern ntable_t nt;
extern MADR *sa;

void rp_fhrmsg_disp(MADR node, int sub, int len, U8 *data)
{
	switch(sub) {
		case RPM_FHR_SOP:
			rp_fhrsop_proc(node, len, data);
			break;

		case RPM_FHR_RII:
			rp_fhrrii_proc(node, len, data);
			break;

		case RPM_FHR_RIR:
			rp_fhrrir_proc(node, len, data);
			break;

		default:
			EPT(stderr, "fhr: unknowm protocol message\n");
			break;
	}
}

//�����data����Ϣ����data������psh����ʼ��ַ��len������phd->len����psh+item[n]�ĳ���
void rp_fhrsop_proc(MADR node, int len, U8 *data)
{
	int i, pos = 0;
	U8 items, hop;
	MADR dest;
//	EPT(stdout, "node[%d]: reveive sop message, nb=%d, len=%d\n", *sa, node, len);
#if 0
	for (i = 0; i < len; i++) {
		EPT(stderr, "%3d", data[i]);
	}
	EPT(stderr, "\n");
#endif
    //src�ǽ���sop���������ڽӵ�
	MADR src = *(MADR *)data;
	//pos�Ƕ�ָ��λ��
	pos += sizeof(MADR);
	ASSERT(src == node);
	//�ھӱ����src�ڵ���·�յ��İ���+1�����ھ�����·״̬�ĸı�
	rlink_inc(src);
	//�����յ��İ���������·״̬ת�ƣ����µ�src�ڵ����·״̬���ڶ�������Ϊ0˵����sop���������,�������յ�����
	//�����µ�״̬���ڵ�ǰ״̬�����Ը���״̬�滻Ϊ��ǰ״̬����ǰ״̬�滻Ϊ��״̬�������滻
	rlink_fsm(src, 0);

	/* drop the message of LQ_NULL or LQ_EXPIRE */
	if (!WH_NL_FEAS(nt.fl[MR_AD2IN(src)].lstatus))
	{
	    //WH_NL_FEAS��·״̬Ϊ��Ծ���߲��ȶ�����������������������
		EPT(stderr, "node[%d]: drop the message from link to %d, status=%d\n", *sa, MR_AD2IN(src), nt.fl[MR_AD2IN(src)].lstatus);
		return;
	}
    //��ֵһ�����ڽӵ�src��·�ɣ�srcΪ��һ��������ԭ���Ƚϣ������������֮
	ritem_nup(src, NULL, 0);
	//���͸���һ��·����·���ڶ�������up=0˵�������ݰ����¶����Ƕ�ʱ������
	//�������ڲ�Ƕ�����ת������*****
	ritem_fsm(&rt.item[MR_AD2IN(src)], 0);

    //�����Ǹ���sop����ͷ����sop����������������һ���ڽڵ��·��·�������濪ʼ��ȡsop����item����

	items = *(data + pos++);
    //EPT(stderr, "sop message: items=%d\n", items);

	for(i = 0; i < items; i++)
	{
	    //����·��·��Ŀ�Ľڵ�
		dest = *(MADR *)(data + pos);
		pos += sizeof(MADR);
        //��Ŀ�Ľڵ�dest����
		hop = *(data + pos++);
        //���ﵽ��������ż��·�ɻ�·��
		if (RP_INHOPS == hop)
		{
		    //���·�ɻ�·�������������·��
		    //��������Ƕ�������ת������*****
			ritem_del(&rt.item[MR_AD2IN(dest)], src);
		}
		else
		{
			if ((hop > MAX_HOPS)||(pos + hop*sizeof(MADR) > len))
			{
				EPT(stderr, "wrong sop message dest=%d,hop=%d,len=%d\n", dest, hop, len);
				break;
			}
			//��src��Ϊ��һ������·�ɲ��Ƚϣ����������滻
			ritem_up(&rt.item[MR_AD2IN(dest)], src, hop, (MADR*)(data+pos));
			ritem_fsm(&rt.item[MR_AD2IN(dest)], 0);
			pos += hop*sizeof(MADR);
		}
	}

	if (pos != len) {
		EPT(stderr, "node[%d]: the sop message len is wrong, items=%d\n", *sa, items);
	}
    //�Ա�·�ɱ�����ת�������ת�����б仯����֪ͨ�ײ�
    update_fwt();
}



void rp_fhrrii_proc(MADR node, int len, U8 *data)
{
}

void rp_fhrrir_proc(MADR node, int len, U8 *data)
{
}

