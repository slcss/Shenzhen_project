#ifndef _HM_TIMER_H
#define _HM_TIMER_H

#define NL_TDELAY_S	0			/* second */
#define NL_TDELAY_US	100000		/* us 0~999999 */
#define NL_TINTVL_S	0			/* second */
#define NL_TINTVL_US	100000		/* us 0~999999 */

typedef struct _tdslot_t {
	U32		tmap[MAX_DSLS_PCF+1];			/* timer map for support multiple timer function */
	U32		tmask[MAX_DSLS_PCF+1];			/* timer mask for disabling certain timer function 1: enbale */
	tproc_t	procs[MAX_DSLS_PCF+1][MAX_NODE_CNT+1];		/* 55��ʱ϶��ÿ��ʱ϶��Ӧ32���ڵ㣬��procs[i][0].wait����¼ռ��iʱ϶�Ľڵ���Ŀ��
														����ʱ��������*/
} tdslot_t;


void hm_tsch_init();
int  hm_start_timer();
void hm_timer_sche(int);
void hm_timer1(void *);
void hm_timer2(void *);
void hm_timer3(void *);
void REQ_timer(void *);
void ds_timer(void*, void*);

#endif

