#ifndef AL_LINK_H_
#define AL_LINK_H_

typedef enum
{
	START,
	CLOSE

} flag_al_t;

typedef struct grph_al
{
    MADR m_addr;        //组播地址
    MADR l_addr;        //组长地址
    flag_al_t flag;		//标志位，判断是否已传出组播树
    int TTL;

}grph_al_t;

typedef struct rreq_al
{
    MADR m_addr;        //目的组播地址
    int m_series;       //组播序列号
    int TTL;
    int hop;            //请求节点到达本节点的跳数，生成时为0，每次发出前加一
    //rreq报文经过的路径，第0个元素是源节点，对应hop=0，然后通过hop确定下一个节点添加的位置，link[hop]即为发送节点
    MADR link[MAX_HOP];
    //到组长节点的链路
    MADR leader_link[MAX_HOP];
}rreq_al_t;

typedef struct rrep_al
{
    MADR m_addr;        //目的组播地址
    int index;          //当前节点数组下标，每次发出前减一
    int node_num;		//节点数组最大下标，由rreq_ptr->hop获得
    MADR link[MAX_HOP]; //直接拷贝rreq记录的路径，第0个元素是RREQ的源节点，应被置为组成员，其余置为ROUTE类型

}rrep_al_t;

typedef struct mact_al
{
    MADR m_addr;        //目的组播地址
    MADR first_addr;
    MADR end_addr;
    int index;          //当前节点数组下标，每次发出前减一
    int node_num;		//节点数组最大下标，由rreq_ptr->hop获得
    MADR link[MAX_HOP]; //直接拷贝rreq记录的路径，第0个元素是RREQ的源节点，应被置为组成员，其余置为ROUTE类型

}mact_al_t;

typedef struct hello_al
{
    MADR m_addr;        //目的组播地址
    MADR first_addr;
    MADR end_addr;

}hello_al_t;



int send_build(int m_addr,int AL_HOP);
int send_delete(int m_addr,int AL_HOP);
int snd_rreq_al(MADR m_addr,int TTL);
int rcv_grph_al(mmsg_t * rcv_buff);
int rcv_rreq_al(mmsg_t * rcv_buff);
int rcv_rrep_al(mmsg_t * rcv_buff);
int rcv_hello_al(mmsg_t * rcv_buff);
int add_al(rrep_al_t * ptr);
int print_link_table();
int rcv_mact_al(mmsg_t * rcv_buff);

int snd_hello_al();

int check_link_table(MADR m_addr,MADR first_addr,MADR end_addr);
int refresh_link_table();

#endif
