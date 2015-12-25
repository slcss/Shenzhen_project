#ifndef _HM_TIMER_H
#define _HM_TIMER_H

#define NL_TDELAY_S		4			/* second */
#define NL_TDELAY_US	0			/* us 0~999999 */
#define NL_TINTVL_S		0			/* second */
#define NL_TINTVL_US	0			/* us 0~999999 */



//void hm_tsch_init();
int  hm_start_timer1();
int  hm_start_timer2();
int  hm_start_timer3();
void hm_timer_sche(int);
//void hm_timer_proc(int);

//void hm_timer_self(void*);
//void hm_timer_test1(void*);
//void hm_timer_test2(void*);


#endif

