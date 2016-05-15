#include "nl_common.h"

typedef struct _ip_node
{
	U8 pt;
	U16 port;
	struct _ip_node *next;
}ip_node;

void ip_hash_insert(U8 pt, U16 port);
int ip_find_key(U8 pt, U16 port);

char fname[] = "/home/root/forbid";

int node_num = 0;
int mod_num;
ip_node * hashTab;
int flag = 0;


#define BigLittleSwap16(A)  ((((uint16)(A) & 0xff00) >> 8) | (((uint16)(A) & 0x00ff) << 8))

// ±Ÿ»úŽó¶Ë·µ»Ø1£¬Ð¡¶Ë·µ»Ø0

int checkCPUendian()

{

       union{

              unsigned long int i;

              unsigned char s[4];

       }c;

 

       c.i = 0x12345678;

       return (0x12 == c.s[0]);

}

 

// Ä£Äâhtonsº¯Êý£¬±Ÿ»ú×ÖœÚÐò×ªÍøÂç×ÖœÚÐò

unsigned short int HtoNs(unsigned short int h)

{

       // Èô±Ÿ»úÎªŽó¶Ë£¬ÓëÍøÂç×ÖœÚÐòÍ¬£¬Ö±œÓ·µ»Ø

       // Èô±Ÿ»úÎªÐ¡¶Ë£¬×ª»»³ÉŽó¶ËÔÙ·µ»Ø

       return checkCPUendian() ? h : BigLittleSwap16(h);

}

 

int ip_file_len()
{
	FILE *fp_port = NULL;
	char c;
	int i = 0;
	int len = 0;

	fp_port = fopen(fname, "r");
	if (NULL == fp_port)
	{
		return -1;
		EPT(stderr,"!!!!file %s doesn't exist\n", fname);
	}
	else
	{
		while((c = fgetc(fp_port)) != EOF)
		{
			i = (int)(c);
			EPT(stderr,"@ i: '%d'\n",i);
			if(i == 255)
				break;
			
			if(c == '\n')
			{
				len++;
			}
		}
		node_num = len * 2;
		EPT(stderr,"IP size of the file %s is %d\n", fname, len);

		if(len == 0)
		{
			EPT(stderr,"!!!!file: %s does't have word\n", fname);
			return 0;
		}

		return 1;
	}
	fclose(fp_port);
	
}

void find_max_prime()
{
	int i;
	int x = node_num;
	while(x > 1)
	{
		for(i = 2; i < x; i++)
		{
			if(x % i == 0)
			{
				x --;
				break;
			}
		}
		if( i == x)
			break;
	}
	mod_num = x;
	EPT(stderr,"mode_num: %d\n", mod_num);
}

int read_file()
{
	int rval = 0;
	FILE *fp_port = NULL;
//	char fname[] = "/home/root/port";


	fp_port = fopen(fname, "r");
	if (NULL == fp_port)
	{
		rval = 1;
		goto fexit;
	//	printf("~~~~~~does not exit file:%s\n",fname);
		
	}
	else
	{
		while(!feof(fp_port))
		{
			int pt;
			int port;
			
			fscanf(fp_port, "%d %d", &pt, &port);

			EPT(stderr,"%d %d (%hhu %hu)\n", pt, port, pt, port);

			if(!feof(fp_port))
		//	printf("%d, %d, %d\n", pt, port, Cos);
			{
				ip_hash_insert((U8)pt, (U16)HtoNs(port));
			}

			rval = 2;
		
		}
	}
fexit:
	if (NULL != fp_port)
		fclose(fp_port);
	return rval;
}

void ip_hash_insert(U8 pt, U16 port)
{
	int key;
	key = ip_find_key(pt, port);

	if(hashTab[key].pt == 0)
	{
		EPT(stderr,"key:%d\n", key);

		hashTab[key].pt = pt;
		hashTab[key].port = port;
	}
	else
	{
		ip_node *temp;
		ip_node *new_node = (ip_node *)calloc(1, sizeof(ip_node));
		
		new_node->pt = pt;
		new_node->port = port;
		new_node->next = NULL;

		temp = &hashTab[key];

		for(; temp->next != NULL; temp = temp->next)
			printf("   ");
		EPT(stderr,"key:%d\n", key);
		
		
		temp->next = new_node;
	}

}

int ip_find_key(U8 pt, U16 port)
{
	int key;
	key = ( pt + (pt<<5) + port ) % mod_num;
	return key;
}

void init_hash()
{
	int i;
	for( i = 0; i < node_num; i++)
	{
		hashTab[i].pt = 0;
		hashTab[i].port = 0;
		hashTab[i].next = NULL;
	}
}

void show_hash()
{
	int i;
	

	for(i = 0; i < node_num; i++)
	{
		int num = 1,j;
		char str[] = "   ";
		EPT(stderr,"IP hashTab[%d] -> pt: %hhu, port: %hu\n", i, 
			hashTab[i].pt, hashTab[i].port);
		
		ip_node * temp = &hashTab[i];
		while(temp->next != NULL)
		{
			temp = temp->next;


			for(j = num ; j > 0; j--)
			{
				EPT(stderr,"%s",str);
			}
			num++;
			EPT(stderr,"next -> pt: %hhu, port: %hu\n",
			 temp->pt, temp->port);
			
		}
	}
}

int find_filter(U8 pt, U16 port)
{
	int key;
	key = ip_find_key(pt, port);
	
//4.4	EPT(stderr, " Filter: find key:%d, pt:%d(%hhu), port:%d(%hu)\n", key, pt, pt, port, port);
	
	if(flag)
		return -1;

	if(hashTab[key].pt == 0)
	{
//4.4		EPT(stderr," Filter's pt = 0 return 0\n");
		return 0;
	}
	else
	{

		ip_node *temp;

		temp = &hashTab[key];
		do
		{
	//4.4		EPT(stderr," find_filter start compare\n");
			if( (temp->pt == pt) && (temp->port == port) )
			{
				EPT(stderr," find_filter return 1\n");
				return 1;
			}
				
			temp = temp->next;
		}
		while(temp != NULL);
	}
	return 0;

}

int init_ip_hash()
{
	int rval;

    if( ip_file_len() == 1)
    {
 		find_max_prime();	
	
		hashTab = (ip_node*)calloc(node_num, sizeof(ip_node));

		init_hash();

		show_hash();
		read_file();

		show_hash();   	
		return 1;
    }
    flag = 1;
    return 0;

	
}


/*//for test
void main()
{
	int i;
	ip_file_len();
	find_max_prime();	
	
	hashTab = (node*)calloc(node_num, sizeof(node));
	init_hash();

	show_hash();
	read_file();

	show_hash();

	int pt_temp, port_temp;

	for(i = 0; i < 5; i++)
	{
		fscanf(stdin, "%d %d", &pt_temp, &port_temp);
		printf("pt:%d, port:%d, Cos:%d\n", pt_temp,port_temp,find_Cos(pt_temp,HtoNs(port_temp)));
	}

	
	free(hashTab);	
}
*/

 

