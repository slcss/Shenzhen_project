#ifndef MAODV_H_
#define MAODV_H_

#include "mr_common.h"
#include "list.h"
#include "timer.h"

#define MAX_REPEAT_ITEM 50
#define REPEAT_TIMEOUT 20
//#define MAX_MAODV_ITEM 10  //放在mr_common.h
#define MAODV_TIMEOUT 10
#define MAX_HOP 12
#define MAX_LEADER_ITEM 10
#define MAX_RREP_TMP_NUM 5
#define MAX_SERIES 1024
//第二次发送RREQ之前睡眠时间
#define SLEEP_TIME 3
//拓扑图的列数
#define top_line 10

#define __DEBUG__
#ifdef __DEBUG__
#define DEBUG(format,...)   \
do{                         \
    printf(format, ##__VA_ARGS__); \
}while(0)
#else
#define DEBUG(format,...)
#endif

//静态组播地址：0x81~0x8F      动态组播地址：0x90~0xFE


typedef struct _nl_tshare_t {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
} nl_tshare_t;

//注意，再64位系统，此结构体的sizeof是24，1+4 // 8 // 4
//重复记录表结构
typedef struct repeat_i
{
    MADR source_addr;
    int is_alive;
    time_t time;
    int series;

}repeat_item;

typedef struct repeat_t
{
    int timeout;
    //int item_num;
    repeat_item item[MAX_REPEAT_ITEM];

}repeat_table;

//组播网络中节点的类型，从上到下优先级递减，优先级高的类型能够覆盖优先级低的类型
/*typedef enum
{
	LEADER = 1,
	MEMBER,
	ROUTE,
	OTHER
} node_type;*/

//MAODV报文类型
typedef enum
{
	RREQ = 1,
	RREP,
	MACT,
	GRPH,
	GRPH_AL,
	RREQ_AL,
	RREP_AL,
	MACT_AL,
	HELLO_AL,
	AL_DATA,
	DATA
} maodv_t;

//MAODV报文标志位类型
typedef enum
{
	NONE,
	JOIN,     	 	//JOIN
	REPAIR,         //REPAIR
	JR,          	//JOIN_REPAIR
	OUT,			//GRPH传播出组播树
	PG,				//组长节点退出且组长有多个下游节点，需要重新选举（即代表上游节点只是变为ROUTE，不退出）
	PRUNE			//修剪

} flag_t;

//组播路由表
typedef struct maodv_i
{
    MADR m_addr;        		//组播地址
    int m_series;      			//序列号，由组长更新
    int is_alive;      	 		//激活状态，值为1说明该链路已经激活，本节点是组播成员
    time_t time;       	 		//更新时间
    MADR l_addr;       		 	//组长节点
    int hop;     				//到组长跳数
    node_type Ntype;   			//本节点类型，MACT添加的设为ROUTE ，GRPH添加的设为OTHER。
								//说明，节点收到RREP时不添加路由表项，收到MACT才添加，MACT携带路径
    MADR up_node;      		    //上游节点
    MADR leader_link[MAX_HOP];//上游节点数组
    int low_num;		//下游节点总数
	ListNode* Lhead;	//下游节点链表指针，记得要初始化，用memset初始化为0就相当与NULL

}maodv_item;

typedef struct maodv_t
{
    int timeout;
    maodv_item item[MAX_MAODV_ITEM];

}maodv_table;

typedef struct leader
{
    int num;
    ListNode* Lhead;

}L_list;

//之所以分出一个头部类型，是为了再rcv_buff直接读取type
typedef struct maodv_head
{
	MADR m_addr;        //目的组播地址,用作data类型报文判断补丁
    maodv_t type;       //MAODV路由消息的类型
    int series;         //源序列号
    MADR source_addr;   //报文源节点地址
    MADR snd_addr;		//报文发送节点地址
    MADR r_addr;        //接受节点地址，一般为0xff，但有时候为单播

}maodv_h;

//RREQ结构，请求组播链路
typedef struct rreq
{
    MADR m_addr;        //目的组播地址
    int m_series;       //组播序列号
    flag_t flag;        //标志位
    int hop;            //请求节点到达本节点的跳数，生成时为0，每次发出前加一
    //rreq报文经过的路径，第0个元素是源节点，对应hop=0，然后通过hop确定下一个节点添加的位置，link[hop]即为发送节点
    MADR link[MAX_HOP];
}rreq_t;

//RREP结构，逆向返回组播链路，收到rrep报文的节点再本地路由表添加路径（未激活）,修改后不再添加路径，而由MACT添加
typedef struct rrep
{
    MADR m_addr;        //目的组播地址
    flag_t flag;
    int m_series;       //组播序列号
    time_t time;        //组播最后更新时间
    MADR l_addr;        //组长节点
    int hop;            //接收节点到组长节点的最终跳数，每次发出RREP前加一
    int index;          //当前节点数组下标，每次发出前减一
    int node_num;		//节点数组最大下标，由rreq_ptr->hop获得
    MADR link[MAX_HOP]; //直接拷贝rreq记录的路径，第0个元素是RREQ的源节点，应被置为组成员，其余置为ROUTE类型
    MADR leader_link[MAX_HOP];//从组长节点到该节点中间的所有节点，一般情况下比link数组元素更多

}rrep_t;

//MACT结构，收到MACT报文的节点激活相应路由路径，此处设计MACT完全拷贝RREP，以激活路径
typedef struct mact
{
    MADR m_addr;        //目的组播地址
    flag_t flag;		//标志位，路由维护用
    int m_series;       //组播序列号
    time_t time;        //组播最后更新时间
    MADR l_addr;        //组长节点
    int hop;            //到组长节点的最终跳数，每次发出MACT前减一
    int index;          //当前节点数组下标,生成时为0，每次发出之前加一
    int node_num;		//节点数组元素个数，在返回RREP时由RREQ跳数得到，中途不变
    MADR link[MAX_HOP]; //直接拷贝rreq记录的路径，第0个元素是RREQ的源节点，应被置为组成员，其余置为ROUTE类型
    MADR leader_link[MAX_HOP];//从组长节点到该节点中间的所有节点，一般情况下比link数组元素更多

}mact_t;

typedef struct grph
{
    MADR m_addr;        //组播地址
    MADR l_addr;        //组长地址
    int m_series;       //组序列号
    flag_t flag;		//标志位，判断是否已传出组播树
    time_t time;
    int hop;            //到组长跳数 ,初始为0，发出之前加一

}grph_t;


typedef struct control
{
	char type[10];
	int AL_HOP;

}control_t;

//辅助链路表
typedef struct al_link_i
{
    MADR m_addr;        		//组播地址
    MADR first_addr;
    MADR end_addr;
    MADR left_addr;
    MADR right_addr;
    int hop;
    time_t time;

}link_item;

typedef struct link_t
{
    int timeout;
    link_item item[MAX_MAODV_ITEM];

}al_link_table;

extern maodv_table m_table;
extern al_link_table link_table;
extern int LOCAL_ADDR;
extern L_list leader_list;


void *rcv_thread(void *arg);
int manage_maodv_msg(mmsg_t * rcv_buff,int size);
int manage_control_msg(mmsg_t * rcv_buff);

int check_repeat(MADR snd_addr,int series);
int rcv_rreq(mmsg_t * rcv_buff);
int rcv_rrep(mmsg_t * rcv_buff);
int rcv_mact(mmsg_t * rcv_buff);
int rcv_grph(mmsg_t * rcv_buff);
int rcv_data(mmsg_t * rcv_buff,int size);
int join(MADR m_addr);
int filt_rrep();
int tree_init(MADR m_addr);

int m_table_add(mact_t* mact_ptr);
int refresh_mtable();
int check_mtable(MADR m_addr);
int print_mtable();
int data_init();
int shm_init();
int get_series();
int get_m_series();

int quit(MADR m_addr);
int rcv_mact_p(mmsg_t * rcv_buff);


int test();


#endif
