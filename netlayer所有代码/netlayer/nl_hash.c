#include "nl_common.h"

typedef struct _node
{
	U8 pt;
	U16 port;
	U8 Cos;
	struct _node *next;
}node;

void hash_insert(U8 pt, U16 port, U8 Cos);
int find_key(U8 pt, U16 port);

char fname[] = "/home/root/classifier";

int node_num = 0;
int mod_num;
node * hashTab;

int flag = 0;

// ¶ÌÕûÐÍŽóÐ¡¶Ë»¥»»

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

 

int file_len()
{
	FILE *fp_port = NULL;
	char c;
	int j = 0;
	int len = 0;

	fp_port = fopen(fname, "r");
	if (NULL == fp_port)
	{
		EPT(stderr,"!!!! file: %s does't exist\n", fname);
		return -1;
	}
	else
	{
		while((c = fgetc(fp_port)) != EOF)
		{
			j = (int)(c);
			EPT(stderr,"@ j: '%d'\n",j);
			if(j == 255)
				break;
			EPT(stderr,"! read : '%c'\n", c);

			if(c == '\n')
			{
				len++;
			}
		}
		node_num = len * 2;
		EPT(stderr,"the size of file is %d\n", len);
		if(len == 0)
		{
			EPT(stderr,"!!! file: %s does't have word\n", fname);
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



	fp_port = fopen(fname, "r");
	if (NULL == fp_port)
	{
		rval = 1;
		goto fexit;
	//	printf("~~~~~~does not exist file:%s\n",fname);
		
	}
	else
	{
		while(!feof(fp_port))
		{
			int pt;
			int port;
			int Cos;
			
			fscanf(fp_port, "%d %d %d", &pt, &port, &Cos);
			EPT(stderr,"%d %d %d (%hhu %hu %hhu)\n", pt, port, Cos, pt, port, Cos);
			
			if(!feof(fp_port))
		//	printf("%d, %d, %d\n", pt, port, Cos);
			{
				hash_insert((U8)pt, (U16)HtoNs(port), (U8)Cos);
			}

			rval = 2;
		
		}
	}
fexit:
	if (NULL != fp_port)
		fclose(fp_port);
	return rval;
}

void hash_insert(U8 pt, U16 port, U8 Cos)
{
	int key;
	key = find_key(pt, port);

	if(hashTab[key].Cos == 255)
	{
		EPT(stderr,"key:%d\n", key);

		hashTab[key].pt = pt;
		hashTab[key].port = port;
		hashTab[key].Cos = Cos;
	}
	else
	{
		node *temp;
		node *new_node = (node *)calloc(1, sizeof(node));
		
		new_node->pt = pt;
		new_node->port = port;
		new_node->Cos = Cos;
		new_node->next = NULL;

		temp = &hashTab[key];

		for(; temp->next != NULL; temp = temp->next)
			printf("   ");
		EPT(stderr,"key:%d\n", key);
		
		temp->next = new_node;
	}

}

int find_key(U8 pt, U16 port)
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
		hashTab[i].Cos = -1;
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
		
		EPT(stderr,"hashTab[%d] -> pt: %hhu, port: %hu, Cos: %hhu\n", i, 
			hashTab[i].pt, hashTab[i].port, hashTab[i].Cos);

		node * temp = &hashTab[i];
		while(temp->next != NULL)
		{
			temp = temp->next;


			for(j = num ; j > 0; j--)
			{
				printf("%s", str);
			}
			num++;
			EPT(stderr,"next -> pt: %hhu, port: %hu, Cos: %hhu\n",
			 temp->pt, temp->port, temp->Cos);
		}
	}
}

U8 find_Cos(U8 pt, U16 port)
{
	int key;
	key = find_key(pt, port);
	
//	EPT(stderr, "~ nl find key: %d(pt: %hhu ,port: %hu)\n", key, pt, port);
	
	if(flag)
	{
		return -1;
	}
		

	if(hashTab[key].Cos == 255)
	{
		return -1;
	}
	else
	{

		node *temp;

		temp = &hashTab[key];
		do
		{
			if( (temp->pt == pt) && (temp->port == port) )
			{
				return (temp->Cos);
				
			}
			temp = temp->next;
		}
		while(temp != NULL);
	}
	return -1;

}

int init_nl_hash()
{
	int rval;

    if( file_len() == 1)
    {
 		find_max_prime();	
	
		hashTab = (node*)calloc(node_num, sizeof(node));

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
	file_len();
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

 

