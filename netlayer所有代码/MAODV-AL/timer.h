#ifndef TIMER_H_
#define TIMER_H_

#define T_DELAY_S		1			/* second */
#define T_DELAY_US		0			/* us 0~999999 */
#define T_INTVL_S		1			/* second */
#define T_INTVL_US		0			/* us 0~999999 */

//定义函数指针类型SIG_FUN
typedef int (*SIG_FUN)();

typedef struct timer_item
{
	char name[20];
	//状态，0为失活，1为激活
	int state;
	//周期，以基准定时器为参照
	int cycle;
	//执行次数，若值为0则无限次
	int times;
	//已经执行次数
	int run_times;
	//调用函数的指针
	SIG_FUN fun;

}T_item;

typedef struct timer_table
{
	int num;
	T_item item[10];

}T_table;

extern T_table t_table;

int start_timer();

int broadcast_grph();
void signal_deal(int signal);
void timer_sche(int signo);
int t_table_init();
int t_table_add(char* name, int cycle, int times, SIG_FUN fun);
int t_table_del(char * name);


#endif
