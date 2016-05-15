#ifndef NL_PACKAGE_H
#define NL_PACKAGE_H

#include "nl_common.h"

#define MAX_PACKAGE_DATA_LENGTH		494


#pragma pack(1)
typedef struct								//ʹ��λ���ܹ����ٳ���
{
		//U8 PR_TYPE_SubT;
		U8 SubT : 4;
		U8 TYPE : 2;
		U8 PR : 2;	
		U8 rcv_addr;
		U8 snd_addr;
		U8 dst_addr;
		U8 src_addr;
		//U8 SEQ_H;		
		U8 H : 1;
		U8 SEQ : 7;
		//U8 SN_TTL;
		U8 TTL : 4;
		U8 SN : 4;		
		//U8 CoS_ACK_Rev;					//ACK��REV��HIGHMAC���ִ���
		U8 Rev : 4;
		U8 ACK : 1;
		U8 CoS : 3;		
		
		char data[MAX_PACKAGE_DATA_LENGTH];
		unsigned int CRC;
}nl_package_t;
#pragma pack()

//wanghao4 on 2.29 init_package_head�е�rcv_addr��Ҫ���ⲿ��ѯ·�ɱ���֮������Ϊ����������
void init_package_head(nl_package_t* pkt,mmsg_t *msg);

inline void set_data_len(nl_package_t* pkt,U16 len);
inline U16 get_data_len(nl_package_t* pkt);
inline void set_PR(nl_package_t* pkt,U8 PR);
inline U8 get_PR(nl_package_t *pkt);
inline void set_TYPE(nl_package_t* pkt,U8 TYPE);
inline U8 get_TYPE(nl_package_t *pkt);
inline void set_SubT(nl_package_t* pkt,U8 SubT);
inline U8 get_SubT(nl_package_t *pkt);
inline void set_src_addr(nl_package_t* pkt,U8 addr);
inline U8 get_src_addr(nl_package_t *pkt);
inline void set_dst_addr(nl_package_t* pkt,U8 addr);
inline U8 get_dst_addr(nl_package_t *pkt);
inline void set_rcv_addr(nl_package_t* pkt,U8 addr);
//���Һ�������һ����ַ,�����һ����ַ����ת��ʱ�޸�
void find_and_set_rcv_addr(nl_package_t *pkt);
inline U8 get_rcv_addr(nl_package_t *pkt);
inline void set_snd_addr(nl_package_t* pkt,U8 addr);
inline U8 get_snd_addr(nl_package_t *pkt);
inline void set_SEQ(nl_package_t* pkt);
inline U8 get_SEQ(nl_package_t *pkt);

//֡ͷ��SN��H������Ҫ��init_package_head����֮������,����Ĭ������ΪH=1,SN=0,��ʾֻ��һ�����ָ�֡
inline void set_H(nl_package_t *pkt, U8 H);
inline U8 get_H(nl_package_t *pkt);
inline void set_SN(nl_package_t *pkt, U8 SN);
inline U8 get_SN(nl_package_t *pkt);

inline void set_TTL(nl_package_t *pkt, U8 ttl);
inline U8 get_TTL(nl_package_t *pkt);
inline void set_CoS(nl_package_t *pkt, U8 cos);
inline U8 get_CoS(nl_package_t *pkt);
inline void set_ACK(nl_package_t *pkt, U8 ack);
inline U8 get_ACK(nl_package_t *pkt);
inline void set_CRC(nl_package_t *pkt, U8 CRC);
inline U8 get_CRC(nl_package_t *pkt);

//inline void set_data_type(nl_package_t *pkt,U16 data_type);
//inline U16 get_data_type(nl_package_t *pkt);
//inline void set_data_length(nl_package_t *pkt,U16 data_length);
//inline U16 get_data_length(nl_package_t *pkt);

#endif // NL_PACKAGE_H
