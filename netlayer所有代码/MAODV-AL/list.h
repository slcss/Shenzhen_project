#ifndef LIST_H_
#define LIST_H_

#include <stdio.h>
#include <stdlib.h>		//malloc函数需要此头文件，否则报错

//宏定义的作用域是从定义到文件结束（或者引用文件结束），这里没有引用最初定义的头文件mr_common.h，所以重新定义一下MADR
#define U8			unsigned char
#define MADR		U8

#define TYPE 		MADR

//链表结构
typedef struct Node
{
    TYPE m_nValue;
    struct Node* m_pNext;
}ListNode;


ListNode* CreateListNode(TYPE value);
ListNode * insert(ListNode* head,TYPE value);
ListNode* del(ListNode* head,TYPE value);
ListNode* del_all(ListNode* head);
int length(ListNode* head);
int PrintList(ListNode* pHead);
int PrintListNode(ListNode* pNode);




#endif
