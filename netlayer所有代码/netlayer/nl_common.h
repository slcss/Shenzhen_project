#ifndef _NL_COMMON_H
#define _NL_COMMON_H

#include "mr_common.h"
#include <sys/shm.h>

typedef struct _nl_tshare_t {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
	int qr_run;
	int gi_run;
} nl_tshare_t;

extern nl_tshare_t  share;
extern MADR SRC_ADDR;

int shm_init();
int maodv_shm_init();

int init_ip_hash();
int init_nl_hash();

int find_filter(U8 pt, U16 port);
U8 find_Cos(U8 pt, U16 port);


fwt_t * shm_fwt;
m_table_shm * p_mt_shm;


#endif

