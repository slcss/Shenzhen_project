#ifndef _HM_TO_LOWMAC_H
#define _HM_TO_LOWMAC_H

#define LM_DATA_MAX 2044

/* highmac to lowmac packet type */
#define HL_RP_DATA    0x00     /*lowmac routing protocol data*/
#define HL_MP_DATA    0x00     /*lowmac MAC protocol data*/
#define HL_VO_DATA    0x01     /*lowmac voice data*/
#define HL_VD_DATA    0x02     /*lowmac video data*/
#define HL_OT_DATA    0x03     /*lowmac other data*/
#define HL_BBRF_DATA  0x04     /*lowmac BB/RF data*/
#define HL_FC_DATA    0x05     /*lowmac flow control data*/
#define HL_FT_DATA    0x06     /*lowmac forward table data*/
#define HL_ST_DATA    0x07     /*lowmac slot table data*/
#define HL_SF_DATA    0x08     /*lowmac service frame data*/
#define HL_MF_REQ     0x09     /*lowmac manage frame request*/
#define HL_IF_DATA    0xff     /*lowmac initial frame data*/

/* lowmac to highmac packet type */
#define LH_RP_DATA    0x00     /*lowmac routing protocol data*/
#define LH_MP_DATA    0x00     /*lowmac MAC protocol data*/
#define LH_VO_DATA    0x01     /*lowmac voice data*/
#define LH_VD_DATA    0x02     /*lowmac video data*/
#define LH_OT_DATA    0x03     /*lowmac other data*/

#define LH_FT_DATA    0x06     /*lowmac forward table data*/
#define LH_ST_DATA    0x07     /*lowmac slot table data*/
#define LH_SF_DATA    0x08     /*lowmac service frame data*/
#define LH_BBRF_DATA  0x09     /*lowmac BB/RF data*/

#define LH_FC_DATA    0x10     /*lowmac flow control data*/
#define LH_FF_DATA    0xff     /*lowmac finish frame data*/

/************** 下发给LowMAC的数据帧结构 **************/
typedef struct _lm_packet_t{	
	U16  len;
	U8	 type;
	U8   Lsn:4;
	U8   Hsn:4;
	char data[LM_DATA_MAX];
}lm_packet_t;


/************** 差错控制帧结构 **************/
typedef struct _lm_flow_ctrl_t{
	U8   HSN:4;
	U8	 HSN_flag:2;
	U8   q_flag:2;
}lm_flow_ctrl_t;


int hm_sem_init(sem_t *);
int hm_sem_destory(sem_t *);
void *hm_sendto_lm_thread(void *);
void hm_get_HLsn(lm_packet_t *, U8);
int hm_sendto_McBSP(lm_packet_t *, U16);
int hm_readfm_McBSP(lm_packet_t *);
int hm_nARQ(lm_packet_t *);
int hm_sendto_NT(lm_packet_t *, U16);


#endif

