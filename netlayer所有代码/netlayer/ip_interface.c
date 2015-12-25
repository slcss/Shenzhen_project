
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>


#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <semaphore.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>//���С��ת��
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/sendfile.h>
#include <syslog.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <bits/endian.h>

#include "mr_common.h"


#define __DEBUG__
#ifdef __DEBUG__
#define DEBUG(format,...)   \
do{                         \
    printf(format, ##__VA_ARGS__); \
    fprintf(fp,format, ##__VA_ARGS__);\
    fflush(fp);\
}while(0)
#else
#define DEBUG(format,...)
#endif


//#define TEST

int handle1,tun;
mmsg_t snd_buf;
mmsg_t rcv_buff;
FILE *fp;
unsigned char my_mac[6] = {0x00,0x11,0x22,0x33,0x44,0x00};

typedef struct inter_head
{
    unsigned char dst_mac[6];
    unsigned char src_mac[6];
    unsigned short type;
}INTER_HEAD;

typedef struct arp_pkt
{
    unsigned short hdtyp;              //Ӳ������
    unsigned short protyp;             //Э������
    unsigned char hdsize;              //Ӳ����ַ����
    unsigned char prosize;             //Э���ַ����
    unsigned short op;                 //����ֵ
    unsigned char smac[6];             //ԴMAC��ַ
    unsigned char sip[4];              //ԴIP��ַ
    unsigned char dmac[6];             //Ŀ��MAC��ַ
    unsigned char dip[4];              //Ŀ��IP��ַ
}ARP_PKT;

typedef struct ip_hd
{
    #if  __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned char ip_hl : 4; //4λ�ײ�����
    unsigned char ip_v : 4; //4λIP�汾��
    #else
    unsigned char ip_v : 4; //4λIP�汾��
    unsigned char ip_hl : 4; //4λ�ײ�����
    #endif

    unsigned char ip_tos;          //��������
    unsigned short ip_len;         //���ݰ�����

    unsigned short ip_flag;          //��־λ,�Ƿ�ָ�
    unsigned short ip_offset;      //ƫ����

    unsigned char ip_ttl;          //����ʱ��
    unsigned char ip_pro;          //Э��
    unsigned short ip_sum;         //У���

    unsigned char ip_src[4];
    unsigned char ip_dst[4];

}IP_HD;

int tuntap_create (char *dev);
void *rcv_thread(void *arg);
int print_arp(char * );
int replace_mac(char* buf_ptr);
int decide_dest(char* buf_ptr);

int test_data();

int main(int argc, char * argv[])
{
	int i;
	unsigned char dst;
	int ret, rsize;

	fp=fopen("int.txt","w");

	char *dev="tap";
	pthread_t 	tid;

	if (argc < 2)
	{
		printf("less parameter my_id\n");
		return -1;
	}
	int my_id;
	my_id = atoi(argv[1]);
	if (my_id < MADR_UNI_MIN || my_id > MADR_UNI_MAX)
    {
		printf("error parameter my_id\n");
		return -1;
	}
	my_mac[5] = my_id;
    DEBUG("my mac addr:\n");
    for(i=0;i<6;i++)
    {
        DEBUG("%x",my_mac[i]);
        DEBUG("-");
    }
    DEBUG("\n");

	snd_buf.mtype = MMSG_IP_DATA;
	snd_buf.node = 0xff;

	mr_queues_init("if2tcpip");

    if (pthread_create(&tid, NULL, rcv_thread, NULL) < 0)
    {
		printf("ip_interface pthread create failed\n");
		exit(0);
	}
	//������������
    tun = tuntap_create(dev);
	if (tun < 0)
	{
		perror("tun_create");
		return -1;
	}

	#ifdef TEST
	test_data();
	sleep(5);
	test_data();
	while(1);
	#else
	while (1)
	{

		DEBUG("======waiting for read from tap0======\n");
        //ͨ������������Դ����������豸�ж������ݵ�snd_buf,��ʵ֮���Խ�������������Ψһ��Ŀ�ľ��Ƿ������������������ݣ���Ϊֱ��LAN�ڲ������ȡ
		ret = read(tun, snd_buf.data, sizeof(snd_buf.data));
		if (ret < 0)
            continue;
		DEBUG("read %d bytes\n", ret);

		INTER_HEAD * inter_head = (INTER_HEAD *)(snd_buf.data);
		//��Դ��ַ��Ŀ�ĵ�ַΪ������macʱ������Ϊ������Ϣ
		if(strncmp(my_mac,inter_head->dst_mac,6)==0 || strncmp(my_mac,inter_head->src_mac,6)==0)
		{
            //���ˣ�ֻ����ICMP��Ϣ
		    IP_HD *ip_hd = (IP_HD *)(snd_buf.data + 14);
		    if(ip_hd->ip_pro != 1)
		    {
		        printf("not ICMP ,return\n");
		        continue;
		    }

		    //print_arp(snd_buf.data);
            //��Ŀ��ip��ַ�ĵ������ֶμ�������Ϊadhoc·�ɵ�Ŀ��ڵ��ַ
		    snd_buf.node = decide_dest(snd_buf.data);
		    DEBUG("ip_interface rcv_buff->node_addr = %d\n" ,snd_buf.node);

            //��ʱ������ע�͵�������msgsnd������������ʹ�ý��̿�������
            int k = msgsnd(nl_qid, &snd_buf, ret+1, 0);
            if(k == 0)
            DEBUG("send success\n");
            else
            DEBUG("send error\n");
		}
	}
	#endif
	return 0;
}

int test_data()
{
	snd_buf.mtype = MMSG_IP_DATA;
	snd_buf.node = 0xff;
	int i;
	memset(snd_buf.data,1,sizeof(MAX_DATA_LENGTH));

	for(i=0;i<10;i++)
	{
		snd_buf.data[i] = i + '0';
	}

	snd_buf.data[MAX_DATA_LENGTH - 4] = 'e';
	snd_buf.data[MAX_DATA_LENGTH - 3] = 'n';
	snd_buf.data[MAX_DATA_LENGTH - 2] = 'd';

	snd_buf.data[MAX_DATA_LENGTH - 1] = '\0';



	int k = msgsnd(nl_qid, &snd_buf, sizeof(snd_buf) - sizeof(long), 0);
	if(k == 0)
		DEBUG("send success\n");
	else
		DEBUG("send error\n");

	return 0;
}

void *rcv_thread(void *arg)
{
	printf("rcv_thread run\n");

	int size,ret,i;

	while (1)
	{
		size=msgrcv(vi_qid, &rcv_buff, sizeof(mmsg_t) - sizeof(long), 0, 0);
        if (size < 0)
            break;
		//if(0 == strcmp(rcv_buff.data,snd_buf.data))
			//printf("rcv match!!\n");
        //�ѷ���ԭ�ڵ㴦��αװĿ��mac��ַ���ɱ��ڵ�mac
        //replace_mac(rcv_buff.data);

		#ifdef TEST
		DEBUG("rcv:%s\n",rcv_buff.data);
		for(i=0;i<MAX_DATA_LENGTH;i++)
		{
			DEBUG("%c ",rcv_buff.data[i]);
		}
		DEBUG("\n");

		#else
		ret = write(tun, rcv_buff.data, size-1);
                if (ret < 0) break;
		DEBUG("write %d bytes\n", ret);
		#endif
	}
}

int print_arp(char* buf_ptr)
{
    int i;

    INTER_HEAD * inter_head = (INTER_HEAD *)buf_ptr;
    DEBUG("dst mac addr of frame is :\n");
    for(i=0;i<6;i++)
    {
        DEBUG("%x",inter_head->dst_mac[i]);
        DEBUG("-");
    }
    DEBUG("\n");
    DEBUG("src mac addr of frame is :\n");
    for(i=0;i<6;i++)
    {
        DEBUG("%x",inter_head->src_mac[i]);
        DEBUG("-");
    }
    DEBUG("\n");
    unsigned short frame_type;
    frame_type = ntohs(inter_head->type);
    DEBUG("frame type is :%x\n",frame_type);

    DEBUG("#######\n");

    if(frame_type == 0x806)
    {
        DEBUG("this is a arp request frame\n");
        ARP_PKT *arp = (ARP_PKT *)(buf_ptr + 14);
        //�����ֽ���ת��Ϊ�����ֽ���
        unsigned short arp_hdtype;
        arp_hdtype = ntohs(arp->hdtyp);
        DEBUG("hard type : %x\n",arp_hdtype);
        unsigned short arp_protype;
        arp_protype = ntohs(arp->protyp);
        DEBUG("protocal type : %x\n",arp_protype);
        unsigned short arp_op;
        arp_op = ntohs(arp->op);
        DEBUG("action value %x\n",arp_op);

        DEBUG("src ip addr of arp pkt is :\n");
        for(i=0;i<4;i++)
        {
            DEBUG("%d",arp->sip[i]);
            DEBUG(".");
        }
        DEBUG("\n");
        DEBUG("src mac addr of arp pkt is :\n");
        for(i=0;i<6;i++)
        {
            DEBUG("%x",arp->smac[i]);
            DEBUG("-");
        }
        DEBUG("\n");
        DEBUG("dst ip addr of arp pkt is :\n");
        for(i=0;i<4;i++)
        {
            DEBUG("%d",arp->dip[i]);
            DEBUG(".");
        }
        DEBUG("\n");
        DEBUG("dst mac addr of arp pkt is :\n");
        for(i=0;i<6;i++)
        {
            DEBUG("%x",arp->dmac[i]);
            DEBUG("-");
        }
        DEBUG("\n");
    }


    if(frame_type == 0x800)
    {
        DEBUG("this is a ip frame\n");
        IP_HD *ip_hd = (IP_HD *)(buf_ptr + 14);
        DEBUG("ip version : %d\n",ip_hd->ip_v);
        DEBUG("ip head_len : %d\n",ip_hd->ip_hl);
        DEBUG("ip server type : %d\n",ip_hd->ip_tos);
        unsigned short ip_len;
        ip_len = ntohs(ip_hd->ip_len);
        DEBUG("ip total len : %d\n",ip_len);
        unsigned short ip_flag;
        ip_flag = ntohs(ip_hd->ip_flag);
        DEBUG("ip id : %d\n",ip_flag);
        unsigned short ip_offset;
        ip_offset = ntohs(ip_hd->ip_offset);
        DEBUG("ip offset : %x\n",ip_offset);
        DEBUG("ip ttl : %d\n",ip_hd->ip_ttl);
        DEBUG("ip pro : %d\n",ip_hd->ip_pro);
        unsigned short ip_sum;
        ip_sum = ntohs(ip_hd->ip_sum);
        DEBUG("ip sum : %d\n",ip_sum);

        DEBUG("src ip addr of frame is :\n");
        for(i=0;i<4;i++)
        {
            DEBUG("%d",ip_hd->ip_src[i]);
            DEBUG(".");
        }
        DEBUG("\n");

        DEBUG("dst ip addr of frame is :\n");
        for(i=0;i<4;i++)
        {
            DEBUG("%d",ip_hd->ip_dst[i]);
            DEBUG(".");
        }
        DEBUG("\n");

    }
    return 0;
}

int replace_mac(char* buf_ptr)
{
    int i;
    INTER_HEAD * inter_head = (INTER_HEAD *)buf_ptr;
    for(i=0;i<6;i++)
    {
        inter_head->dst_mac[i] = my_mac[i];
    }

    return 0;
}

int decide_dest(char* buf_ptr)
{
    int i;

    INTER_HEAD * inter_head = (INTER_HEAD *)buf_ptr;

    unsigned short frame_type;
    frame_type = ntohs(inter_head->type);
    DEBUG("frame type is :%x\n",frame_type);

    DEBUG("#######\n");

    if(frame_type == 0x800)
    {
        DEBUG("this is a ip frame\n");
        IP_HD *ip_hd = (IP_HD *)(buf_ptr + 14);
        int dst;
        dst = ip_hd->ip_dst[2];
        if (dst >= MADR_UNI_MIN && dst <= MADR_UNI_MAX)
            DEBUG("decide dst addr :%d\n",dst);
        return dst;
    }
    return 0;
}

int tuntap_create (char *dev)
{
	struct ifreq ifr;
	int fd;
	char *device = "/dev/net/tun";
	if ((fd = open (device, O_RDWR)) < 0)
		DEBUG("Cannot open TUN/TAP dev %s", device);

	memset (&ifr, 0, sizeof (ifr));
	ifr.ifr_flags = IFF_NO_PI; //����������Ϣ��Ĭ�ϵ�ÿ�����ݰ��������û��ռ�ʱ����������һ�����ӵİ�ͷ���������Ϣ
	if (!strncmp (dev, "tun", 3))
	{
  		ifr.ifr_flags |= IFF_TUN; //����һ����Ե��豸
	}
	else if (!strncmp (dev, "tap", 3))
	{
   		ifr.ifr_flags |= IFF_TAP; //����һ����̫���豸
	}
	else
 	{
		DEBUG("I don't recognize device %s as a TUN or TAP device",dev);
	}

	if (strlen (dev) > 3)/* unit number specified? */
		strncpy (ifr.ifr_name, dev, IFNAMSIZ);

	if(ioctl(fd, TUNSETIFF, (void *)&ifr) < 0)
		DEBUG("Cannot ioctl TUNSETIFF %s\n", dev);

	if(ioctl(fd, TUNSETNOCSUM, (void *)&ifr) < 0)
		DEBUG("Cannot ioctl TUNSETIFF %s\n", dev);

	DEBUG("TUN/TAP device %s opened\n", ifr.ifr_name);

	return fd;
}











