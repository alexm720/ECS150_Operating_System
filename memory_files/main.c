#include <stdint.h>
#include <stddef.h>
#include "RVCOS.h"
#include <ctype.h>
 
volatile int global = 42;
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
volatile uint32_t *saved_gp;
volatile int ran_once = 0;
volatile int mutex_num = 0;
volatile int thread_num = 0;
volatile uint32_t *saved_back_sp;
volatile int run_again = 0;
typedef void (*TFunctionPointer)(void);
volatile int Tick = 0;
void enter_cartridge(void);
void switch_gp(void);
void set_up_tp(TThreadIDRef threadId);
void set_sp();
void context_switch(uint32_t **old, uint32_t *new);
TThreadIDRef get_tp();
TThreadReturn call_on_cartridge_gp(TThreadEntry entry, void * param, uint32_t *gp);
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
#define CART_STAT_REG (*(volatile uint32_t *)0x4000001C)
#define CONTROLLER    (*(volatile uint32_t *)0x40000018)
 
PoolObj* PoolList;
volatile struct Node *Waitlist;
Mutex *Mutexlist;
prioQueue priorityQ;
volatile struct Node2 *Threadlist2;
 
void idle(){
    asm volatile ("csrsi mstatus, 0x8");
    int idlecount = 0;
    while(1){
        idlecount++;
    }
}
 
struct Node2* newNode2(struct TCB* thread){// functions(newnode, pop, and push) :modified from :
                                                // https://www.geeksforgeeks.org/priority-queue-using-linked-list/
    struct Node2 *tcb = (struct Node2 *)malloc(sizeof(struct Node2));
    tcb->Thread = thread;

    tcb->nextTCB = NULL;
    return tcb;
};
 
struct Node* newNode(TThreadID threadid)         // functions(newnode, pop, and push) :modified from :
                                                // https://www.geeksforgeeks.org/priority-queue-using-linked-list/
 
{
        struct Node *tcb = (struct Node *)malloc(sizeof(struct Node));
        tcb->Thread = threadid;
        tcb->nextTCB = NULL;
        return tcb;
}
 
TThreadID pop(struct Node **head)
{
    if(!*head){
        return -1;
    }
    struct Node *temp = *head;
    TThreadID found = (*head)->Thread;
    *head = (*head)->nextTCB;
    free(temp);
    return found;
}
 
struct TCB * find_node(const struct Node2 **head, TThreadID threadid){
    if((*head) == NULL)
        {
                return (struct Node2 *)NULL;
        }
        if(((*head)->Thread->Tidentifier) == threadid){
            return((*head)->Thread);
        }
        struct TCB *stat = find_node(&(*head)->nextTCB, threadid);
    return stat;
}
 
struct Node2 *delete_node(struct Node2 **head, TThreadID threadid){
    if((*head) == NULL)
        {
                return (struct Node2*) NULL;
        }
        if((unsigned long)((*head)->Thread->Tidentifier) == (unsigned long)threadid){
            struct Node2 *next = (*head) -> nextTCB;
            free((*head));
            return next;
        }
        (*head)->nextTCB = delete_node(&(*head)->nextTCB, threadid);
        return (*head);
}
 
void push2(struct Node2 **head, struct TCB *thread){
        struct Node2 *start = (*head);
        struct Node2 *temp = newNode2(thread);
        while(start->nextTCB != NULL)
        {
                start = start->nextTCB;
        }
        temp->nextTCB = start->nextTCB;
        start->nextTCB = temp;
}
 
void push(struct Node **head, TThreadID threadid)
{
        struct Node *start = (*head);
        struct Node *temp = newNode(threadid);
        while(start->nextTCB != NULL)
        {
                start = start->nextTCB;
        }
        temp->nextTCB = start->nextTCB;
        start->nextTCB = temp;
}
 
void insertWaitQueue(TThreadID threadid){
    if(!Waitlist){
        Waitlist = (struct Node *)malloc(sizeof(struct Node));
        Waitlist->Thread = threadid;
        Waitlist->nextTCB = NULL;
        return;
    }
    push(&Waitlist, threadid);
    return;
}
 
void insertPrioQueue(TThreadID threadid, TThreadPriority prio){
        if(threadid == 1){
            if(!priorityQ.queue_lowest)
                {
                        priorityQ.queue_lowest = (struct Node *)malloc(sizeof(struct Node));
                        priorityQ.queue_lowest->Thread = threadid;
                        priorityQ.queue_lowest->nextTCB = NULL;
                        return;
                }
                push(&priorityQ.queue_lowest, threadid);
                return;
        }
        if(prio == RVCOS_THREAD_PRIORITY_LOW)
        {
                if(!priorityQ.queue_low)
                {
                        priorityQ.queue_low = (struct Node *)malloc(sizeof(struct Node));
                        priorityQ.queue_low->Thread = threadid;
                        priorityQ.queue_low->nextTCB = NULL;
                        return;
                }
                push(&priorityQ.queue_low, threadid);
                return;
        }
        else if(prio == RVCOS_THREAD_PRIORITY_NORMAL)
        {
                if(!priorityQ.queue_normal)
                {
                        priorityQ.queue_normal = (struct Node *)malloc(sizeof(struct Node));
                        priorityQ.queue_normal->Thread = threadid;
                        priorityQ.queue_normal->nextTCB = NULL;
                        return;
                }
                push(&priorityQ.queue_normal, threadid);
                return;
        }
        else if(prio == RVCOS_THREAD_PRIORITY_HIGH)
        {
                if(!priorityQ.queue_high)
                {
                        priorityQ.queue_high = (struct Node *)malloc(sizeof(struct Node));
                        priorityQ.queue_high->Thread = threadid;
                        priorityQ.queue_high->nextTCB = NULL;
                        return;
                }
                push(&priorityQ.queue_high, threadid);
                return;
        }
 
}

void insertPrioQueue2(struct Node **low, struct Node **normal, struct Node **high, TThreadID threadid, TThreadPriority prio){
        if(prio == RVCOS_THREAD_PRIORITY_LOW)
        {
                if(!(*low))
                {
                        (*low) = (struct Node *)malloc(sizeof(struct Node));
                        (*low)->Thread = threadid;
                        (*low)->nextTCB = NULL;
                        return;
                }
                push(&(*low), threadid);
                return;
        }
        else if(prio == RVCOS_THREAD_PRIORITY_NORMAL)
        {
                if(!(*normal))
                {
                        (*normal) = (struct Node *)malloc(sizeof(struct Node));
                        (*normal)->Thread = threadid;
                        (*normal)->nextTCB = NULL;
                        return;
                }
                push(&(*normal), threadid);
                return;
        }
        else if(prio == RVCOS_THREAD_PRIORITY_HIGH)
        {
                if(!(*high))
                {
                        (*high) = (struct Node *)malloc(sizeof(struct Node));
                        (*high)->Thread = threadid;
                        (*high)->nextTCB = NULL;
                        return;
                }
                push(&(*high), threadid);
                return;
        }
 
}
 
void insertThreadlist(struct Node2 **head, struct TCB* thread){
    if(!(*head))
                {
                    (*head) = (struct Node2 *)malloc(sizeof(struct Node2));
                    (*head)->Thread = thread;
                    (*head)->nextTCB = NULL;
                    return;
                }
                push2(&(*head), thread);
                return;
}
 
uint32_t *initialize_stack(uint32_t *sp, void (*skeleton_or_idle)(uint32_t), uint32_t param, uint32_t tcb){
    sp--;
    *sp = (uint32_t)skeleton_or_idle; //sw      ra,48(sp)
    sp--;
    *sp = tcb;//sw      tp,44(sp)
    sp--;
    *sp = 0;//sw      t0,40(sp)
    sp--;
    *sp = 0;//sw      t1,36(sp)
    sp--;
    *sp = 0;//sw      t2,32(sp)
    sp--;
    *sp = 0;//sw      s0,28(sp)
    sp--;
    *sp = 0;//sw      s1,24(sp)
    sp--;
    *sp = param;//sw      a0,20(sp)
    sp--;
    *sp = 0;//sw      a1,16(sp)
    sp--;
    *sp = 0;//sw      a2,12(sp)
    sp--;
    *sp = 0;//sw      a3,8(sp)
    sp--;
    *sp = 0;//sw      a4,4(sp)
    sp--;
    *sp = 0;//sw      a5,0(sp)
    return sp;
}
 
void switchImmed(int index){
    TThreadID oldtp = get_tp();
    struct TCB *current = find_node(&Threadlist2, oldtp);
    struct TCB *new = find_node(&Threadlist2, index);
    if(new -> priority == RVCOS_THREAD_PRIORITY_HIGH && current -> priority != RVCOS_THREAD_PRIORITY_HIGH){
        insertPrioQueue(current -> Tidentifier,current -> priority);
        current -> state = RVCOS_THREAD_STATE_READY;
        TThreadID something = pop(&priorityQ.queue_high);
        new -> state = RVCOS_THREAD_STATE_RUNNING;
        context_switch(&current -> StackPointer, new -> StackPointer);
            }
    else if(new -> priority == RVCOS_THREAD_PRIORITY_NORMAL && current -> priority == RVCOS_THREAD_PRIORITY_LOW){
        insertPrioQueue(current -> Tidentifier,current -> priority);
        current -> state = RVCOS_THREAD_STATE_READY;
        TThreadID something = pop(&priorityQ.queue_normal);
        new -> state = RVCOS_THREAD_STATE_RUNNING;
        context_switch(&current -> StackPointer, new -> StackPointer);                
    }
}
 
void thread_skeleton(TThreadID this_tcb){
    struct TCB *current = find_node(&Threadlist2, this_tcb);
    asm volatile ("csrsi mstatus, 0x8");
    //switch global pointer to application global pointer
    //entry(param)
    //swich global pointer back before returning
    TThreadReturn ret_val = call_on_cartridge_gp(current -> parameters,current -> FuncEntry, saved_gp);
    asm volatile ("csrci mstatus, 0x8");
    RVCThreadTerminate(current -> Tidentifier, ret_val);
}
 
void schedule_new_thread(){
    TThreadID oldtp = get_tp();
    struct TCB *current = find_node(&Threadlist2, oldtp);
    int i = 0;
    if(current->priority == RVCOS_THREAD_PRIORITY_NORMAL){
        i++;
    }
    if(current -> state == RVCOS_THREAD_STATE_RUNNING){
        current -> state = RVCOS_THREAD_STATE_READY;
    }
    if(current -> state == RVCOS_THREAD_STATE_READY){
        insertPrioQueue(current -> Tidentifier, current -> priority);
    }
    TThreadID newTCB = pop(&priorityQ.queue_high);
    if(newTCB == -1){
        newTCB = pop(&priorityQ.queue_normal);
        if(newTCB == -1){
            newTCB = pop(&priorityQ.queue_low);
            if(newTCB == -1){
                newTCB = pop(&priorityQ.queue_lowest);
                if(newTCB == -1){
                    return;
                }
            }
        }
    }
        struct TCB *new = find_node(&Threadlist2, newTCB);
        new -> state = RVCOS_THREAD_STATE_RUNNING;
        if(current -> Tidentifier == new -> Tidentifier){
            return;
        }
        context_switch(&current -> StackPointer, new -> StackPointer);  
}
int main() {
    saved_sp = &controller_status;
    while(1){
        if(CART_STAT_REG & 0x1){
            if(!run_again){
                run_again = 1;
                enter_cartridge();
            }
        }
        else{
            run_again = 0;
        }
    }
    return 0;
}
 
TStatus RVCInitialize(uint32_t *gp){
    if(!ran_once){
        saved_gp = gp;
        ran_once++;
        Mutexlist = malloc(10 * sizeof(Mutex));
        struct TCB *mainThread = (struct TCB *)malloc(sizeof(struct TCB));
        mainThread->Tidentifier= 0;
        mainThread->priority = RVCOS_THREAD_PRIORITY_NORMAL;
        mainThread->state = RVCOS_THREAD_STATE_RUNNING;
        mainThread->acquired = (int *)malloc(10 * sizeof(int));
        mainThread->mutex_num = 0;
        insertThreadlist(&Threadlist2,mainThread);
        thread_num++;
        struct TCB *IdleThread = (struct TCB *)malloc(sizeof(struct TCB));
        IdleThread->Tidentifier= 1;
        IdleThread->FuncEntry = idle;
        IdleThread->MemorySize = 1024;
        IdleThread->priority = RVCOS_THREAD_PRIORITY_LOW;
        IdleThread->BaseStack = (uint32_t *)malloc(1024);
        IdleThread->state = RVCOS_THREAD_STATE_CREATED;
        IdleThread->acquired = (int *)malloc(10 * sizeof(int));
        IdleThread->mutex_num = 0;
        insertThreadlist(&Threadlist2, IdleThread);
        thread_num++;
        set_up_tp(mainThread->Tidentifier);
        TStatus fininit = RVCThreadActivate(IdleThread->Tidentifier);
        return RVCOS_STATUS_SUCCESS;
    }
    else{
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
}










 
TStatus RVCThreadCreate(TThreadEntry entry, void *param, TMemorySize memsize, TThreadPriority prio, TThreadIDRef tid){
    if(!entry||!tid){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct TCB *createThread = (struct TCB *)malloc(sizeof(struct TCB));
    *tid = thread_num;
    createThread->Tidentifier= thread_num;
    createThread->FuncEntry = entry;
    createThread->parameters = param;
    createThread->MemorySize = memsize;
    createThread->priority = prio;
    createThread->BaseStack = (uint32_t *)malloc(memsize);
    createThread->state = RVCOS_THREAD_STATE_CREATED;
    createThread->acquired = (int *)malloc(10 * sizeof(int));
    createThread->mutex_num = 0;
    insertThreadlist(&Threadlist2, createThread);
    thread_num++;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadDelete(TThreadID thread){
    struct TCB *targetTCB = find_node(&Threadlist2, thread);
    if(targetTCB->BaseStack == 0 && targetTCB->priority == 0 && targetTCB->StackPointer == 0){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    if(targetTCB->state != RVCOS_THREAD_STATE_DEAD){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    struct Node2 *del = delete_node(&Threadlist2, thread);
    thread_num--;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadActivate(TThreadID thread)
{
        struct TCB *targetTCB = find_node(&Threadlist2, thread);
        if(targetTCB->state != RVCOS_THREAD_STATE_CREATED && targetTCB->state != RVCOS_THREAD_STATE_DEAD){
            return RVCOS_STATUS_ERROR_INVALID_STATE;
        }
        targetTCB->state = RVCOS_THREAD_STATE_READY;
        insertPrioQueue(targetTCB->Tidentifier, targetTCB->priority);
        targetTCB->StackPointer = initialize_stack(targetTCB->BaseStack+targetTCB->MemorySize, thread_skeleton , targetTCB->Tidentifier, targetTCB->Tidentifier);
        switchImmed(thread);
        return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadTerminate(TThreadID thread, TThreadReturn returnval){
    struct TCB *current = find_node(&Threadlist2, thread);
    if(current -> BaseStack == 0 && current -> priority == 0 && current -> StackPointer == 0)
            {
                 return RVCOS_STATUS_ERROR_INVALID_ID;
            }
    if(current -> state == RVCOS_THREAD_STATE_DEAD || current -> state == RVCOS_THREAD_STATE_CREATED)
            {
             return RVCOS_STATUS_ERROR_INVALID_STATE;
        }
 
    current -> retval = returnval;
    current -> state = RVCOS_THREAD_STATE_DEAD;
    for(int i = 0; i < current->mutex_num; i++){
        Mutexlist[current->acquired[i]].lock = 0;
        TThreadID wake = pop(&Mutexlist[current->acquired[i]].queue.queue_high);
        struct TCB *targetTCB = find_node(&Threadlist2, wake);
        while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
            wake = pop(&Mutexlist[current->acquired[i]].queue.queue_high);
            targetTCB = find_node(&Threadlist2, wake);
        }
        if(wake == -1){
            wake = pop(&Mutexlist[current->acquired[i]].queue.queue_normal);
            targetTCB = find_node(&Threadlist2, wake);
            while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
            wake = pop(&Mutexlist[current->acquired[i]].queue.queue_normal);
            targetTCB = find_node(&Threadlist2, wake);
        }
        if(wake == -1){
            wake = pop(&Mutexlist[current->acquired[i]].queue.queue_low);
            targetTCB = find_node(&Threadlist2, wake);
            while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
                wake = pop(&Mutexlist[current->acquired[i]].queue.queue_low);
                targetTCB = find_node(&Threadlist2, wake);
            }
        }
        }
        if(wake != -1){
            targetTCB->state = RVCOS_THREAD_STATE_READY;
            targetTCB->tick = 0;
            targetTCB->acquired[targetTCB->mutex_num] = current->acquired[i];
            targetTCB->mutex_num++;
            Mutexlist[current->acquired[i]].Owner = wake;
            Mutexlist[current->acquired[i]].lock = 1;
            current->acquired[i] = -1;
            insertPrioQueue(wake,targetTCB->priority);
        }
    }
    current->mutex_num = 0;
    struct Node2 *start = Threadlist2;
    while(start){
        if(start -> Thread -> waiter == thread){
            start -> Thread -> waiter = NULL;
            start -> Thread -> retval = returnval;
            start -> Thread -> state = RVCOS_THREAD_STATE_READY;
            insertPrioQueue(start -> Thread -> Tidentifier, start -> Thread -> priority);
        }
        start = start->nextTCB;
    }
    schedule_new_thread();
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadWait(TThreadID thread, TThreadReturnRef returnref, TTick timeout){
    TThreadID oldtp = get_tp();
    struct TCB *current = find_node(&Threadlist2, oldtp);
    struct TCB *new = find_node(&Threadlist2, thread);
    if(timeout == RVCOS_TIMEOUT_IMMEDIATE){
        if(new -> state != RVCOS_THREAD_STATE_DEAD){
            return RVCOS_STATUS_FAILURE;
        }
        else{
            *returnref = new -> retval;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    if(new -> BaseStack == 0 && new -> priority == 0 && new -> StackPointer == 0)
        {
            return RVCOS_STATUS_ERROR_INVALID_ID;
        }
    if(returnref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(new -> state != RVCOS_THREAD_STATE_DEAD){
        current -> state = RVCOS_THREAD_STATE_WAITING;
        current -> waiter = thread;
        if(timeout != RVCOS_TIMEOUT_INFINITE){
            current -> tick = timeout;
        }
        schedule_new_thread();
        *returnref = current -> retval;
    }
    else{
        *returnref = new -> retval;
    }
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadID(TThreadIDRef threadref){
    *threadref = get_tp();
    if(threadref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadState(TThreadID thread, TThreadStateRef stateref){
    if(stateref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    struct TCB *targetTCB = find_node(&Threadlist2, thread);
    if(targetTCB -> BaseStack == 0 && targetTCB -> priority == 0 && targetTCB -> StackPointer == 0){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    *stateref = targetTCB -> state;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCThreadSleep(TTick tick){
    if(tick == RVCOS_TIMEOUT_INFINITE){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    TThreadID tp = get_tp();
    struct TCB *current = find_node(&Threadlist2, tp);
    if(tick == RVCOS_TIMEOUT_IMMEDIATE){
        schedule_new_thread();
    }
    else{
        current -> state = RVCOS_THREAD_STATE_WAITING;
        current -> tick = tick;
        schedule_new_thread();
    }
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCTickMS(uint32_t *tickmsref){
    if(tickmsref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    *tickmsref = 2;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCTickCount(TTickRef tickref){
    if(tickref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    *tickref = Tick;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCWriteText(const TTextCharacter *buffer, TMemorySize writesize){
    if(!buffer){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        TThreadID tp = get_tp();
        struct TCB *current = find_node(&Threadlist2, tp);
        current -> state = RVCOS_THREAD_STATE_WAITING;
        current -> waitwrite = buffer;
        current -> size = writesize;
        insertWaitQueue(tp);
        schedule_new_thread();
        return RVCOS_STATUS_SUCCESS;
    }
}
 
TStatus RVCReadController(SControllerStatusRef statusref){
    if(!statusref){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    else{
        statusref->DLeft = (CONTROLLER >> 0) & 1;
        statusref->DUp = (CONTROLLER >> 1) & 1;
        statusref->DDown = (CONTROLLER >> 2) & 1;
        statusref->DRight = (CONTROLLER >> 3) & 1;
        statusref->DButton1 = (CONTROLLER >> 4) & 1;
        statusref->DButton2 = (CONTROLLER >> 5) & 1;
        statusref->DButton3 = (CONTROLLER >> 6) & 1;
        statusref->DButton4 = (CONTROLLER >> 7) & 1;
        return RVCOS_STATUS_SUCCESS;
    }
}



//MemPoolFreeChunk initFree[5];
//MemoryPoolAllocater allocater;
volatile int poolCount = 0;
int initbool = 0; 





void push_pool(PoolObj **head, MemoryPoolAllocater pool, void *base)
{
	if((*head) == NULL)
	{
		PoolObj *node;
		TStatus check = RVCMemoryAllocate(sizeof(PoolObj),&node);
		node->poolid = pool.id;
		node->curr = pool;
		node->baseStart = base;
		node->next = NULL;
		(*head) = node;
		return;
	}
	if((*head)->next == NULL)
	{
		PoolObj *node;
		TStatus check = RVCMemoryAllocate(sizeof(PoolObj *),&node);
		node->poolid = pool.id;
		node->curr = pool;		
		node->baseStart = base;
		node->next = NULL;
		(*head)->next = node;
		return;
	}
	push_pool(&(*head)->next, pool, base);
}
uintptr_t roundUp(uintptr_t numToRound, int multiple)
{
	uintptr_t remainder = numToRound % multiple;
	if(remainder == 0)
	{
		return numToRound;
	}
	return (char*)numToRound + multiple - (char*)remainder;
}

TStatus RVCMemoryPoolCreate(void *base, TMemorySize size, TMemoryPoolIDRef memoryref)
{

	if(!memoryref || !base || size < (2 * 0x40))
	{
		return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
	}
	uintptr_t align = (uintptr_t)base;
	align = roundUp(align, 16);
	base = (void *)align;
	poolCount++;
	MemoryPoolAllocater newpool;
	newpool.id = poolCount;
	*memoryref = (TMemoryPoolID)poolCount;
	newpool.chunkNum = 0;
	newpool.FirstFree = NULL;
	newpool.MemorySize = size;
	newpool.Memoryleft = size;
	newpool.blocks = NULL;

	push_pool(&PoolList,newpool, base);
	return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCMemoryPoolDelete(TMemoryPoolID memory)
{
	if(memory == RVCOS_MEMORY_POOL_ID_SYSTEM)
	{	

	}
			
}
 
 
PoolObj* FindPool(PoolObj **head, TMemoryPoolID id)
{
	if((*head)->poolid == id)
	{
		return *head;
	}
	if((*head) == NULL)
	{
		return NULL;
	}
	PoolObjRef temp = FindPool((*head)->next, id);
	return temp;
}


TStatus RVCMemoryPoolQuery(TMemoryPoolID memory, TMemorySizeRef bytesleft)
{
	PoolObjRef TargetPool;
	TargetPool = FindPool(&PoolList, memory);
	*bytesleft = TargetPool->curr.Memoryleft;
	return RVCOS_STATUS_SUCCESS;

}

MemPoolFreeChunkRef create_chunk(TMemorySize size,int all)
{
	MemPoolFreeChunkRef new;
	TStatus check = RVCMemoryAllocate(sizeof(MemPoolFreeChunk),&new);
	new->allocated = all;
	new->size = size;
	new->next = NULL;
	return new;
}

TStatus RVCMemoryPoolAllocate(TMemoryPoolID memory, TMemorySize size, void **pointer){
	if(memory == RVCOS_MEMORY_POOL_ID_SYSTEM)
	{
		*pointer = malloc(size);
		return RVCOS_STATUS_SUCCESS;
	}
	if(size == 0 || !pointer){
		return RVCOS_STATUS_ERROR_INVALID_ID;
	}
	PoolObjRef TargetPool;
	TargetPool = FindPool(&PoolList, memory);
	if(!TargetPool)
	{
		return RVCOS_STATUS_ERROR_INVALID_ID;
	}
	if(TargetPool->curr.MemorySize < size)
	{
		return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
	}
	TMemorySize rounded = size;
	while(rounded%0x40 != 0x00)
	{
		rounded = rounded + 0x01;
	}
	uintptr_t *startbase = TargetPool->baseStart;
	MemoryPoolAllocater chunks = TargetPool->curr;
	if(!(chunks.chunkNum))
	{
		TMemorySize tot = 0;
		*pointer = startbase;
		MemPoolFreeRef current = TargetPool->curr.FirstFree;
		TargetPool->curr.FirstFree = current;
		TargetPool->curr.chunkNum++;
		TargetPool->curr.blocks = create_chunk(rounded,1); 
		MemPoolFreeChunkRef cur = TargetPool->curr.blocks;
		tot += rounded;
		
		while(tot < TargetPool->curr.MemorySize)
		{
			TargetPool->curr.chunkNum++;
			cur->next = create_chunk(rounded, 0);
			cur = cur->next;
			tot += rounded;
		}
		TargetPool->curr.Memoryleft -= rounded;
		return RVCOS_STATUS_SUCCESS;
	}
	MemPoolFreeChunkRef head = TargetPool->curr.blocks;
	TMemorySize sum = 0;
	while(head != NULL)
	{
		if(rounded <= head->size && !head->allocated) 
		{
			
			head->allocated = 1;
			*pointer = startbase;
			return RVCOS_STATUS_SUCCESS;
		}
		startbase = (uintptr_t *)startbase + head->size;
		sum += head->size;
		if(!head->next)
		{
			break;
		}
		head = head->next;
	}
	TargetPool->curr.Memoryleft -= sum;
	return RVCOS_STATUS_SUCCESS;

}





TStatus RVCMemoryPoolDeallocate(TMemoryPoolID memory, void *pointer){
	if(!pointer)
	{
		return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
	}
	if(memory == RVCOS_MEMORY_POOL_ID_SYSTEM)
	{
		free(PoolList);
		PoolList->curr.chunkNum--;
		return RVCOS_STATUS_SUCCESS;
	}
	PoolObjRef target;
	target = FindPool(&PoolList, memory);
	
	if(!target)
	{
		RVCWriteText("why",3);
		return RVCOS_STATUS_ERROR_INVALID_ID;
	}
	uintptr_t starterbase = target->baseStart;
	MemPoolFreeChunkRef head = target->curr.blocks;
	while(head != NULL)
	{
		if(starterbase == (uintptr_t)pointer)
		{
			head->allocated = 0;
			target->curr.chunkNum--;
			return RVCOS_STATUS_SUCCESS;
		}
		starterbase = (uintptr_t *)starterbase + head->size;
		head = head->next;

	}
	if(!head)
	{
		return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
	}
	return RVCOS_STATUS_SUCCESS;
}



TStatus RVCMutexCreate(TMutexIDRef mutexref){
      if(mutexref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    Mutexlist[mutex_num].Midentifier = mutex_num;
    Mutexlist[mutex_num].lock = 0;
    *mutexref = mutex_num;
    if(Mutexlist == NULL){
        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    mutex_num++;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCMutexDelete(TMutexID mutex){
     for(int i = 0; i < mutex_num; i++){
        if(Mutexlist[i].Midentifier == mutex){
            if(Mutexlist[i].lock == 1){
                return RVCOS_STATUS_ERROR_INVALID_STATE;
            }
            for(int k = i; k < mutex_num-1; k++){
                Mutexlist[k] = Mutexlist[k+1];
            }
            mutex_num--;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
}
 
TStatus RVCMutexQuery(TMutexID mutex, TThreadIDRef ownerref){
     if(ownerref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(mutex_num > 10){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    if(Mutexlist[mutex].lock == 0){
        return RVCOS_THREAD_ID_INVALID;
    }
    *ownerref = Mutexlist[mutex].Owner;
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCMutexAcquire(TMutexID mutex, TTick timeout){
     if(mutex_num > 10){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    TThreadID current = get_tp();
    struct TCB *targetTCB = find_node(&Threadlist2, current);
    if(Mutexlist[mutex].lock == 1){
        if(timeout == RVCOS_TIMEOUT_IMMEDIATE){
            return RVCOS_STATUS_FAILURE;
        }
        insertPrioQueue2(&Mutexlist[mutex].queue.queue_low, &Mutexlist[mutex].queue.queue_normal, &Mutexlist[mutex].queue.queue_high, current, targetTCB->priority);
        targetTCB->state = RVCOS_THREAD_STATE_WAITING;
        if(timeout != RVCOS_TIMEOUT_INFINITE){
            targetTCB->tick = timeout;
        }
        schedule_new_thread();
        if(Mutexlist[mutex].Owner != current){
            return RVCOS_STATUS_FAILURE;
        }
    }
    else{
        Mutexlist[mutex].Owner = current;
        Mutexlist[mutex].lock = 1;
        targetTCB->acquired[targetTCB->mutex_num] = mutex;
        targetTCB->mutex_num++;
    }
    return RVCOS_STATUS_SUCCESS;
}
 
TStatus RVCMutexRelease(TMutexID mutex){
     if(mutex_num > 10){
        return RVCOS_STATUS_ERROR_INVALID_ID;
    }
    TThreadID current = get_tp();
    if(Mutexlist[mutex].Owner != current){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    Mutexlist[mutex].lock = 0;
    struct TCB *currentTCB = find_node(&Threadlist2, current);
    for(int i = 0; i < currentTCB->mutex_num; i++){
        if(currentTCB->acquired[i] == mutex){
            for(int k = i; k < currentTCB->mutex_num-1; k++){
                currentTCB->acquired[k] = currentTCB->acquired[k+1];
            }
            currentTCB->mutex_num--;
            break;
        }
    }
    TThreadID wake = pop(&Mutexlist[mutex].queue.queue_high);
    struct TCB *targetTCB = find_node(&Threadlist2, wake);
    while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
        wake = pop(&Mutexlist[mutex].queue.queue_high);
        targetTCB = find_node(&Threadlist2, wake);
    }
    if(wake == -1){
        wake = pop(&Mutexlist[mutex].queue.queue_normal);
        targetTCB = find_node(&Threadlist2, wake);
        while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
            wake = pop(&Mutexlist[mutex].queue.queue_normal);
            targetTCB = find_node(&Threadlist2, wake);
        }
        if(wake == -1){
            wake = pop(&Mutexlist[mutex].queue.queue_low);
            targetTCB = find_node(&Threadlist2, wake);
            while(wake != -1 && targetTCB->state != RVCOS_THREAD_STATE_WAITING){
                wake = pop(&Mutexlist[mutex].queue.queue_low);
                targetTCB = find_node(&Threadlist2, wake);
            }
            if(wake == -1){
                return RVCOS_STATUS_SUCCESS;
            }
        }
    }
    targetTCB->state = RVCOS_THREAD_STATE_READY;
    targetTCB->tick = 0;
    targetTCB->acquired[targetTCB->mutex_num] = mutex;
    targetTCB->mutex_num++;
    Mutexlist[mutex].Owner = wake;
    Mutexlist[mutex].lock = 1;
    insertPrioQueue(wake,targetTCB->priority);
    if(targetTCB->priority > currentTCB->priority){
        schedule_new_thread();
    }
    return RVCOS_STATUS_SUCCESS;
}
 
uint32_t c_syscall_handler(uint32_t p1,uint32_t p2,uint32_t p3,uint32_t p4,uint32_t p5,uint32_t code){
    switch(code){
        case 0: return RVCInitialize((void *)p1);
        case 1: return RVCThreadCreate((void *)p1, (void *)p2, (void *)p3, (void *)p4, (void *)p5);
        case 2: return RVCThreadDelete((void *)p1);
        case 3: return RVCThreadActivate((void *)p1);
        case 4: return RVCThreadTerminate((void *)p1, (void *)p2);
        case 5: return RVCThreadWait((void *)p1, (void *)p2, (void *)p3);
        case 6: return RVCThreadID((void *)p1);
        case 7: return RVCThreadState((void *)p1, (void *)p2);
        case 8: return RVCThreadSleep((void *)p1);
        case 9: return RVCTickMS((void *)p1);
        case 10: return RVCTickCount((void *)p1);
        case 11: return RVCWriteText((void *)p1, (void *)p2);
        case 12: return RVCReadController((void *)p1);
        case 13: return RVCMemoryPoolCreate((void *)p1, (void *)p2, (void *)p3);
        case 14: return RVCMemoryPoolDelete((void *)p1);
        case 15: return RVCMemoryPoolQuery((void *)p1, (void *)p2);
        case 16: return RVCMemoryPoolAllocate((void *)p1, (void *)p2, (void *)p3);
        case 17: return RVCMemoryPoolDeallocate((void *)p1, (void *)p2);
        case 18: return RVCMutexCreate((void *)p1);
        case 19: return RVCMutexDelete((void *)p1);
        case 20: return RVCMutexQuery((void *)p1, (void *)p2);
        case 21: return RVCMutexAcquire((void *)p1, (void *)p2);
        case 22: return RVCMutexRelease((void *)p1);
    }
}
