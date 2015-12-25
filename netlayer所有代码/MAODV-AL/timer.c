#include "timer.h"
#include "mr_common.h"


T_table t_table;
//开启定时器
int start_timer()
{
	printf("start timer\n");


	t_table_init();

	int rval = 0;
	struct itimerval value,value2;
	//指定初始定时时间
    value.it_value.tv_sec = 1;
    value.it_value.tv_usec = 0;
    //指定间隔时间，如果未指定这里的it_interval，则定时器只执行一次
    value.it_interval.tv_sec = 1;
    value.it_interval.tv_usec = 0;

    value2.it_value.tv_sec = 1;
    value2.it_value.tv_usec = 0;
    value2.it_interval.tv_sec = 1;
    value2.it_interval.tv_usec = 0;

	signal(SIGALRM, timer_sche);

	rval = setitimer(ITIMER_REAL, &value, NULL);

	if (-1 == rval)
	{
		EPT(stderr, "error setting timer !\n");
	}

	return rval;
}

int t_table_init()
{
	memset(&t_table,0,sizeof(t_table));

	printf("timer table init over\n");
	return 0;
}

//向定时器表中添加表项，四个参数分别为：定时器名，定时器周期（基于基准定时器），定时器运行次数（0为无限次），调用函数指针
int t_table_add(char* name, int cycle, int times, SIG_FUN fun)
{
	int index = t_table.num;
	strncpy(t_table.item[index].name,name,20);
	t_table.item[index].state = 1;
	t_table.item[index].cycle = cycle;
	t_table.item[index].times = times;
	t_table.item[index].run_times = 0;
	t_table.item[index].fun = fun;
	t_table.num++;

	return 0;
}

int t_table_del(char * name)
{
	int i;
	for(i=0;i<t_table.num;i++)
	{
		if(0 == strcmp(name,t_table.item[i].name))
		{
			int j;
			for(j=i;j<t_table.num - 1;j++)
			{
				t_table.item[j] = t_table.item[j+1];
			}
			memset(&t_table.item[t_table.num - 1],0,sizeof(T_item));
			t_table.num--;
			break;
		}
	}

	return 0;
}

void timer_sche(int signo)
{
	int i;
	for(i=0;i<t_table.num;i++)
	{
		if(t_table.item[i].state == 1)
		{
			t_table.item[i].fun();
			if(t_table.item[i].times != 0)
			{
				t_table.item[i].run_times++;
				if(t_table.item[i].run_times == t_table.item[i].times)
				{
					//删除该定时器
					t_table_del(t_table.item[i].name);
				}
			}
		}
	}
}

//接受外部的SIGUSR1信号，打印路由表
void signal_deal(int signal)
{
    printf("Signal = %d\n",signal);
    print_mtable();
    return;
}
