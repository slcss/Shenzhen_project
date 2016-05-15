#include "nl_common.h"

nl_tshare_t  share = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	1,
	1
};

int shm_id = -1;

int shm_init()
{
	key_t 	shm_key;
	void	*shmaddr = (void*) NULL;

	printf("begin init shm\n");

	//shm_key = qinfs[2].key_q;		//实际上用的是路由进程消息队列的key
	shm_key = ftok(PATH_CREATE_KEY, 10010);
	if (shm_key<0)
	{
		EPT(stderr,"!!!! failed to get shm_key\n");
		return -1;
	}

	shm_id = shmget(shm_key, sizeof(fwt_t), 0640);
	if(shm_id != -1)								//先清除已经存在的共享内存
	{
		shmaddr = shmat(shm_id , NULL, 0);
		if (shmaddr != (void *)-1)
		{
			shmdt(shmaddr);							//与shmat函数相反，是用来断开与共享内存附加点的地址，禁止本进程访问此片共享内存
			shmctl(shm_id,IPC_RMID,0) ;
		}
	}

	shm_id = shmget(shm_key, sizeof(fwt_t), 0640|IPC_CREAT); //创建共享内存
	if(shm_id == -1)
	{
		perror("shmget");
		EPT(stderr, "!!!!!!!!!!netlayer: get shmget id error\n");
		return -1;
	}
	shmaddr = shmat(shm_id, NULL, 0);
	if (shmaddr == NULL )
	{
		EPT(stderr, "!!!!!!!!routingp: can not attach shared memory.\n");
		return -1;
	}

	shm_fwt =(fwt_t*)shmaddr;
	printf("addr of shm_fwt : %p\n",shm_fwt);
	memset(shm_fwt,0,sizeof(fwt_t));

	maodv_shm_init();

	return 0;
}

int maodv_shm_init()
{
	key_t 	shm_key;
	void	*shmaddr = (void*) NULL;


	//shm_key = qinfs[4].key_q;		//实际上用的是maodv进程消息队列的key
	shm_key = ftok(PATH_CREATE_KEY, 10086);

	printf("~ netlayer: maodv_shm_key = %d\n",shm_key);
	if (shm_key<0)
	{
		EPT(stderr,"!!!!!!failed to get m_table shm_key\n");
		return -1;
	}

	shm_id = shmget(shm_key, sizeof(m_table_shm), 0640);
	if(shm_id != -1)								//先清除已经存在的共享内存
	{
		shmaddr = shmat(shm_id , NULL, 0);
		if (shmaddr != (void *)-1)
		{
			shmdt(shmaddr);							//与shmat函数相反，是用来断开与共享内存附加点的地址，禁止本进程访问此片共享内存
			shmctl(shm_id,IPC_RMID,0) ;
		}
	}

	shm_id = shmget(shm_key, sizeof(m_table_shm), 0640|IPC_CREAT); //创建共享内存

	if(shm_id == -1)
	{
		perror("shmget");
		EPT(stderr, "!!!!! netlayer: get maodv shm id error\n");
		return -1;
	}

	shmaddr = shmat(shm_id, NULL, 0);
	if (shmaddr == NULL )
	{
		EPT(stderr, "!!!! routingp: can not attach shared memory.\n");
		return -1;
	}

	p_mt_shm =(m_table_shm*)shmaddr;
	printf("addr of shm maodv table : %p\n",p_mt_shm);
	memset(p_mt_shm,0,sizeof(m_table_shm));


	return 0;
}

