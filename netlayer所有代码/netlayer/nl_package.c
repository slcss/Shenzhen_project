#include "nl_package.h"

static U8 seq = 0;


//只需要数据类型和目的地址就能生成完整的帧头
void init_package_head(nl_package_t* pkt,mmsg_t *msg)
{
	U8 PR = 0;						//协议版本，当前为0
	U8 TYPE = 0;
	U8 SubT = 0;
	U8 ttl = MAX_HOPS;
	U8 Cos = 0;
	U8 ack = 0;

	unsigned char protcl;
	unsigned short port;
	int i;

	long type;
	type = msg->mtype;
	//changeg by wanghao3 on 2.19
	switch(type)
	{
		case MMSG_RPM:
		   	Cos = 0;
			TYPE = 0;
			SubT = 0;
			break;
		case MMSG_IP_DATA:
			//changed by wanghao3 on 2.19
			TYPE = 0;
			SubT = 2;

			protcl = *( (unsigned char *)(msg->data + 9) );
	
	//4.4		EPT(stderr, "NL : pt: %hhu\n", (U8)protcl);
			
			port = *( (unsigned short *)(msg->data + 20) );
		
	//4.4	EPT(stderr, "NL : port: %hu\n", (U16)port); 

			/*65535: 端口号设置为-1的HtoNs后的数值*/
			if( (Cos = find_Cos(protcl, 65535) )!= 255)			//-1 在U8下为1111 1111（255）
			{
				EPT(stderr,"1 Cos: %hhu\n", Cos);
				break;
			}
				
			else if( (Cos = find_Cos(protcl, port)) != 255)
			{
				EPT(stderr,"2 Cos: %hhu\n", Cos);
				break;
			}

			else
				/*Cos默认（未找到classifier文件中相匹配的值）为3*/
			{
				Cos = 3;
				EPT(stderr,"3 Cos: %hhu\n", Cos);
				break;
			}	
			
/*
			if(protcl == 17)			//if IP首部中的协议位为UDP
			{
				port = *( (unsigned short *)(msg->data + 14 + 20) );
				EPT(stderr, "!!!port: %d\n", (int)port); 
				if(VO_PORT == port )		//VO_PORT:音频数据的源端口号
				{
					cos = 1;
					TYPE = 0;
					SubT = 2;
					break;
				}

				if(VD_PORT == port )		//VD_PORT:视频数据的源端口号
				{
					cos = 2;
					TYPE = 0;
					SubT = 3;
					break;				
				}
				else
				{
					cos = 3;
					TYPE = 0;
					SubT = 1;
					break;
				}
			}

			else
			{
				cos = 3;
				TYPE = 0;
				SubT = 1;
				break;
			}
*/
		case MMSG_MRPM:
		   	Cos = 0;
			TYPE = 0;
			SubT = 4;
			break;
			
		default: 
			break;
	}

	set_PR(pkt,PR);
	set_TYPE(pkt, TYPE);
	set_SubT(pkt,SubT);
	set_src_addr(pkt,SRC_ADDR);
	set_dst_addr(pkt,msg->node);
	//set_rcv_addr(pkt,rcv_addr);
	find_and_set_rcv_addr(pkt);
	set_snd_addr(pkt,SRC_ADDR);		//发送地址也设置为源地址
	
	set_SEQ(pkt);
	set_H(pkt,1);					//默认设置为完整帧不需要分割
	set_SN(pkt,0);
	set_TTL(pkt,ttl);
	set_CoS(pkt,Cos);
	set_ACK(pkt,ack);
	

//deleted by wanghao on4.18
// 	set_data_type(pkt,type);
}

inline void set_PR(nl_package_t* pkt,U8 PR)
{
	pkt->PR = PR;
}

inline U8 get_PR(nl_package_t *pkt)
{
	return pkt->PR;
}

inline void set_TYPE(nl_package_t* pkt,U8 TYPE)
{
	pkt->TYPE = TYPE;
}

inline U8 get_TYPE(nl_package_t *pkt)
{
	return pkt->TYPE;
}

inline void set_SubT(nl_package_t* pkt,U8 SubT)
{

	pkt->SubT = SubT;
}

inline U8 get_SubT(nl_package_t *pkt)
{
	return pkt->SubT;
}

inline void set_src_addr(nl_package_t* pkt,U8 addr)
{
	pkt->src_addr = addr;
}

inline U8 get_src_addr(nl_package_t *pkt)
{
	return pkt->src_addr;
}

inline void set_dst_addr(nl_package_t* pkt,U8 addr)
{
	pkt->dst_addr = addr;
}

inline U8 get_dst_addr(nl_package_t *pkt)
{
	return pkt->dst_addr;
}

inline void set_rcv_addr(nl_package_t* pkt,U8 addr)
{
	pkt->rcv_addr = addr;
}

void find_and_set_rcv_addr(nl_package_t *pkt)
{
	fwi_t* rt;
	if(MADR_BRDCAST == pkt->dst_addr)
		set_rcv_addr(pkt,MADR_BRDCAST);
	else
	{
		rt = &(shm_fwt->ft[pkt->dst_addr - 1]);
		if (rt->fnd == 0 && rt->snd == 0)
		{
			 
			//2.26	EPT(stderr, "have no route to %d ,set rcv_addr as dest addr\n", pkt->dst_addr);
			
			set_rcv_addr(pkt,pkt->dst_addr);
			return;
		}
		set_rcv_addr(pkt, rt->fnd);
	}
}

inline U8 get_rcv_addr(nl_package_t *pkt)
{
	return pkt->rcv_addr;
}

inline void set_snd_addr(nl_package_t* pkt,U8 addr)
{
	pkt->snd_addr = addr;
}

inline U8 get_snd_addr(nl_package_t *pkt)
{
	return pkt->snd_addr;
}

inline void set_SEQ(nl_package_t* pkt)
{
	if(seq > 127)
		seq = 0;
	pkt->SEQ = seq;
	seq++;
}

inline U8 get_SEQ(nl_package_t *pkt)
{
	return pkt->SEQ;
}

inline void set_H(nl_package_t *pkt, U8 H)
{
	pkt->H = H;
}

inline U8 get_H(nl_package_t *pkt)
{
	return pkt->H;
}

inline void set_SN(nl_package_t *pkt, U8 SN)
{
	pkt->SN = SN;
}

inline U8 get_SN(nl_package_t *pkt)
{
	return pkt->SN;
}

inline void set_TTL(nl_package_t *pkt, U8 ttl)
{
	if (ttl > MAX_HOPS)
		ttl = MAX_HOPS;
	pkt->TTL = ttl;
}

inline U8 get_TTL(nl_package_t *pkt)
{
	return pkt->TTL;
}

inline void set_CoS(nl_package_t *pkt, U8 cos)
{
	pkt->CoS = cos;
}
inline void set_ACK(nl_package_t *pkt, U8 ack)
{
	pkt->ACK = ack;
}

inline U8 get_CoS(nl_package_t *pkt)
{
	return pkt->CoS;
}

inline U8 get_ACK(nl_package_t *pkt)
{
	return pkt->ACK;
}

inline void set_CRC(nl_package_t *pkt, U8 CRC)
{
	//pkt->CRC = CRC;
}

inline U8 get_CRC(nl_package_t *pkt)
{
	//return pkt->CRC;
}

/*
inline void set_data_type(nl_package_t *pkt,U16 data_type)
{
	((mmhd_t*)pkt->data)->type = data_type;
}

inline U16 get_data_type(nl_package_t *pkt)
{
	return ((mmhd_t*)pkt->data)->type;
}

inline void set_data_length(nl_package_t *pkt,U16 data_length)
{
	((mmhd_t*)pkt->data)->len = data_length;
}

inline U16 get_data_length(nl_package_t *pkt)
{
	return ((mmhd_t*)pkt->data)->len;
}
*/