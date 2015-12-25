#include "list.h"

//创建一个链表结点
ListNode* CreateListNode(TYPE value)
{
    ListNode *pNode = (ListNode * )malloc(sizeof(ListNode));
    pNode->m_nValue=value;
    pNode->m_pNext=NULL;
    return pNode;
}

//遍历链表中的所有结点
int PrintList(ListNode* pHead)
{
    ListNode *pNode=pHead;
    while(pNode!=NULL)
    {
        printf("%d ",pNode->m_nValue);
        pNode=pNode->m_pNext;
    }
    printf("\n");

    return 0;
}

//输出链表中的某一结点的值
int PrintListNode(ListNode* pNode)
{
    if(pNode == NULL)
    {
        printf("The node is NULL\n");
    }
    else
    {
        printf("The key in node is %d.\n", pNode->m_nValue);
    }

    return 0;
}

//按排序 插入节点需要三个指针，分别指向新建节点，前节点和后节点
ListNode * insert(ListNode* head,TYPE value)
{
    ListNode *p0,*p1,*p2;
    p0 = (ListNode* )malloc(sizeof(ListNode));;           //注意一定要申请空间
    p0->m_nValue = value;
    p0->m_pNext = NULL;

    if(head == NULL)//空链表
    {
        head = p0;
        return head;
    }

    p1 = head;
    while(p1->m_nValue < value && p1->m_pNext != NULL)
    {
        p2 = p1;
        p1 = p1->m_pNext;
    }
    if(value == p1->m_nValue)
    {
    	return head;
    }
    else if(value < p1->m_nValue)
    {
        if(p1 == head)
        {
            p0->m_pNext = head;
            head = p0;
        }
        else
        {
            p0->m_pNext = p1;
            p2->m_pNext = p0;
        }
    }
    else
    {
        p0->m_pNext = NULL;
        p1->m_pNext = p0;
    }
    return head;
}

ListNode* del(ListNode* head,TYPE value)
{
    ListNode *p1,*p2; //p1指向要删除的元素，p2指向要删除的元素前一个元素。
    p1 = head;
    //寻找删除位置，插入位置也是这样找
    while(p1->m_nValue != value && p1->m_pNext != NULL)
    {
        p2 = p1;
        p1 = p1->m_pNext;
    }
    if(p1->m_nValue == value)
    {
        if(p1 == head) //注意等号不要写成赋值号，很难检查出来。
        {
            head = head->m_pNext;
        }
        else
        {
            p2->m_pNext = p1->m_pNext;
        }

        free(p1);   //注意删除元素的内存回收
        p1 = NULL;
    }
    else
        printf("not found value %d,can't delete\n",value);

    return head;
}

ListNode* del_all(ListNode* head)
{
    ListNode *p1,*p2; //p1指向要删除的元素，p2指向要删除的元素前一个元素。
    p1 = head;
    //寻找删除位置，插入位置也是这样找
    while(p1 != NULL)
    {
        p2 = p1->m_pNext;
        free(p1);
        p1 = p2;
    }

    return head;
}

int length(ListNode* head)
{
    int n = 0;
    ListNode* p = head;
    while(p != NULL)
    {
        p = p->m_pNext;
        n++;
    }
    return n;
}
