#ifndef _HM_COMMON_H
#define _HM_COMMON_H
#include "hm_with_lowmac.h"

/* struct for the data sharing among thread */
typedef struct _hm_tshare_t {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
	int hm_rcvfrom_nl_run;
	int lm2nl_run;
	int hm_sendto_lm_run;
	int slot_run;
} hm_tshare_t;

void hm_qrv_kill(int);
void* hm_rcvfrom_nl_thread(void *);
void* hm_lm2nl_thread(void *);
int hm_rmsg_ip_proc(U16, void *);
int hm_rmsg_rp_proc(U16, void *);
int hm_rmsg_ft_proc(U16, void *);
int hm_rmsg_proc(U16, void *);
int hm_smsg_proc(mmsg_t *, long, U8, U16, char *);
int hm_management(U16, char *, U8);






#endif

