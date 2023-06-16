#include <stdint.h>
#include <stddef.h>
#include "RVCOS.h"
#include <ctype.h>
#include "RVCOSPaletteDefault.c"
volatile uint32_t controller_status = 0;
volatile uint32_t *saved_sp;
volatile uint32_t *saved_gp;
volatile int ran_once = 0;
volatile int mutex_num = 0;
volatile int thread_num = 0;
volatile int pool_num_alloc = 1;
volatile uint32_t *saved_back_sp;
volatile int run_again = 0;
volatile int mem_pool = 1;
volatile int graphic_id = 1;
volatile int graphic_alloc = 0;
typedef void (*TFunctionPointer)(void);
volatile int Tick = 0;
void enter_cartridge(void);
void context_switch(uint32_t **old, uint32_t *new);
TThreadIDRef get_tp();
TThreadReturn call_on_cartridge_gp(TThreadEntry entry, void * param, uint32_t *gp);
#define CART_STAT_REG   (*(volatile uint32_t *)0x4000001C)
#define CONTROLLER      (*(volatile uint32_t *)0x40000018)
#define MODE            (*((volatile uint32_t *)0x500FF414))
extern char __stack_bottom;
extern char _heapbase;
volatile struct Node *Waitlist;
Mutex Mutexlist[10];
prioQueue priorityQ;
volatile struct Node2 *Threadlist2;
MemoryPool Poollist[10];
Buffer *Graphics;
volatile int palette_id = 1;
volatile int palette_alloc = 0;
Palette palette[5];
volatile Available backgroundAvailable[5];
volatile Available LSpriteAvailable[64];
volatile Available SSpriteAvailable[128];
volatile uint8_t *BackgroundData[5];
volatile uint8_t *LargeSpriteData[64];
volatile uint8_t *SmallSpriteData[128];

volatile SColor *BackgroundPalettes[4];
volatile SColor *SpritePalettes[4];
volatile Available PbackgroundAvailable[4];
volatile Available PSpriteAvailable[4];
volatile SBackgroundControl *BackgroundControls = (volatile SBackgroundControl *)0x500FF100;
volatile SLargeSpriteControl *LargeSpriteControls = (volatile SLargeSpriteControl *)0x500FF114;
volatile SSmallSpriteControl *SmallSpriteControls = (volatile SSmallSpriteControl *)0x500FF214;
volatile SVideoControllerMode *ModeControl = (volatile SVideoControllerMode *)0x500FF414;
MempoolFreeChunk megaFreeList[1];

void idle(){
    asm volatile ("csrsi mstatus, 0x8");
    int idlecount = 0;
    while(1){
        idlecount++;
    }
}
 
struct Node2* newNode2(struct TCB* thread){// functions(newnode, pop, and push) :modified from :
                                                // https://www.geeksforgeeks.org/priority-queue-using-linked-list/
    struct Node2 *tcb;
    RVCMemoryAllocate(sizeof(struct Node2), (void **)&tcb);
    tcb->Thread = thread;

    tcb->nextTCB = NULL;
    return tcb;
};
 
struct Node* newNode(TThreadID threadid)         // functions(newnode, pop, and push) :modified from :
                                                // https://www.geeksforgeeks.org/priority-queue-using-linked-list/
 
{
        struct Node *tcb;
        RVCMemoryAllocate(sizeof(struct Node), (void **)&tcb);
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
    RVCMemoryDeallocate(temp);
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
            RVCMemoryDeallocate((*head));
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
        RVCMemoryAllocate(sizeof(struct Node), (void **)&Waitlist);
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&priorityQ.queue_lowest);
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&priorityQ.queue_low);
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&priorityQ.queue_normal);
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&priorityQ.queue_high);
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&(*low));
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&(*normal));
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
                        RVCMemoryAllocate(sizeof(struct Node), (void **)&(*high));
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
                    RVCMemoryAllocate(sizeof(struct Node2), (void **)&(*head));
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
    for(int Index = 0; Index < 4; Index++){
        BackgroundPalettes[Index] = (volatile SColor *)(0x500FC000 + 256 * sizeof(SColor) * Index);
        SpritePalettes[Index] = (volatile SColor *)(0x500FD000 + 256 * sizeof(SColor) * Index);
    }
    for(int Index = 0; Index < 5; Index++){
        BackgroundData[Index] = (volatile uint8_t *)(0x50000000 + 512 * 288 * Index);
    }
    for(int Index = 0; Index < 64; Index++){
        LargeSpriteData[Index] = (volatile uint8_t *)(0x500B4000 + 64 * 64 * Index);
    }
    for(int Index = 0; Index < 128; Index++){
        SmallSpriteData[Index] = (volatile uint8_t *)(0x500F4000 + 16 * 16 * Index);
    }
    memcpy((void *)BackgroundPalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor));
    memcpy((void *)SpritePalettes[0],RVCOPaletteDefaultColors,256 * sizeof(SColor));
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
uintptr_t roundUp(uintptr_t numToRound, int multiple)//https://stackoverflow.com/questions/3407012/rounding-up-to-the-nearest-multiple-of-a-number
{
    uintptr_t remainder = numToRound % multiple;
    if (remainder == 0)
        return numToRound;

    return (char*)numToRound + multiple - (char*)remainder;
}

int roundUpInt(int numToRound, int multiple){
    int remainder = numToRound % multiple;
    if (remainder == 0)
        return numToRound;

    return numToRound + multiple - remainder;
}

void InitFreeChunk(MemoryPoolRef pool, int size){
    int size2 = size/64;
    char* end = (char*)pool->MemoryBase;
    int inc = size;
    for(int i = 0; i < size2; i++){
        megaFreeList[i].base = (void*)end;
        megaFreeList[i].size = inc;
        megaFreeList[i].next = -1;
        inc -= 64;
        end += 64;
    }
    pool->FirstFree = 0;
}


TStatus RVCInitialize(uint32_t *gp){
    int hello = (&__stack_bottom-&_heapbase);
    if(!ran_once){
        saved_gp = gp;
        ran_once++;
        for(int Index = 0; Index < 5; Index++){
            BackgroundData[Index] = (volatile uint8_t *)(0x50000000 + 512 * 288 * Index);
        }
        for(int Index = 0; Index < 64; Index++){
            LargeSpriteData[Index] = (volatile uint8_t *)(0x500B4000 + 64 * 64 * Index);
        }
        for(int Index = 0; Index < 128; Index++){
            SmallSpriteData[Index] = (volatile uint8_t *)(0x500F4000 + 16 * 16 * Index);
        }
        /*Poollist[0].ID = 0;
        void *base = &_heapbase;
        uintptr_t align = (uintptr_t)base;//align ptr to 16
        align = roundUp(align, 16);
        base = (void *)align;
        int newSize = &__stack_bottom-&_heapbase;
        Poollist[0].MemoryBase = base;
        Poollist[0].OGMemorySize = newSize;
        Poollist[0].MemorySize = newSize;
        Poollist[0].FirstFree = -1;
        InitFreeChunk(&Poollist[0], newSize);*/
        RVCMemoryAllocate(50*sizeof(Buffer), (void**)&Graphics);
        struct TCB *mainThread;
        RVCMemoryAllocate(sizeof(struct TCB), (void **)&mainThread);
        mainThread->Tidentifier= 0;
        mainThread->priority = RVCOS_THREAD_PRIORITY_NORMAL;
        mainThread->state = RVCOS_THREAD_STATE_RUNNING;
        RVCMemoryAllocate(10 * sizeof(int), (void **)&mainThread->acquired);
        mainThread->mutex_num = 0;
        insertThreadlist(&Threadlist2,mainThread);
        struct TCB* hi = find_node(&Threadlist2,0);
        thread_num++;
        struct TCB *IdleThread;
        RVCMemoryAllocate(sizeof(struct TCB), (void **)&IdleThread);
        IdleThread->Tidentifier= 1;
        IdleThread->FuncEntry = idle;
        IdleThread->MemorySize = 1024;
        IdleThread->priority = RVCOS_THREAD_PRIORITY_LOW;
        RVCMemoryAllocate(1024, (void **)&IdleThread->BaseStack);
        IdleThread->state = RVCOS_THREAD_STATE_CREATED;
        RVCMemoryAllocate(10 * sizeof(int), (void **)&IdleThread->acquired);
        IdleThread->mutex_num = 0;
        insertThreadlist(&Threadlist2, IdleThread);
        thread_num++;
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
    struct TCB *createThread;
    RVCMemoryAllocate(sizeof(struct TCB), (void **)&createThread);
    *tid = thread_num;
    createThread->Tidentifier= thread_num;
    createThread->FuncEntry = entry;
    createThread->parameters = param;
    createThread->MemorySize = memsize;
    createThread->priority = prio;
    RVCMemoryAllocate(memsize, (void **)&createThread->BaseStack);
    createThread->state = RVCOS_THREAD_STATE_CREATED;
    RVCMemoryAllocate(10 * sizeof(int), (void **)&createThread->acquired);
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
    if(current == NULL)
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
    if(targetTCB == NULL){
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
    *tickmsref = 5;
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
MemoryPoolRef FindPool(PoolObjRef head, TMemoryPoolID id){
    if(head == NULL){
        return (MemoryPoolRef)NULL;
    }
    if(head->pool->ID == head){
        return head->pool;
    }
    MemoryPoolRef found = FindPool(head->nextPool, id);
    return found;
}

PoolObjRef newNode3(MemoryPoolRef pool){// functions(newnode, pop, and push) :modified from :
                                                // https://www.geeksforgeeks.org/priority-queue-using-linked-list/
    PoolObjRef start;
    RVCMemoryAllocate(sizeof(PoolObj), (void **)&start);
    start->pool = pool;
    start->nextPool = NULL;
    return start;
}

void push3(PoolObjRef head, MemoryPoolRef pool){
    PoolObjRef start = head;
    PoolObjRef temp = newNode3(pool);
    while(start->nextPool != NULL){
        start = start->nextPool;
    }
    temp->nextPool = start->nextPool;
    start->nextPool = temp;
}


TStatus RVCMemoryPoolCreate(void *base, TMemorySize size, TMemoryPoolIDRef memoryref){
    if(!memoryref || !base || size<(2*0x40)){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    uintptr_t align = (uintptr_t)base;//align ptr to 16
    align = roundUp(align, 16);
    base = (void *)align;
    *memoryref = mem_pool;
    Poollist[pool_num_alloc].ID = mem_pool;
    Poollist[pool_num_alloc].MemoryBase = base;
    Poollist[pool_num_alloc].OGMemorySize = size;
    Poollist[pool_num_alloc].MemorySize = size;
    int found;
    for(int i = 0; i < 1000; i++){
        if(megaFreeList[i].base == base){
            found = i;
            break;
        }
    }
    int block = roundUpInt(size, 64);
    megaFreeList[found].size = block;
    Poollist[pool_num_alloc].FirstFree = found;
    mem_pool++;
    pool_num_alloc++;

    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCMemoryPoolDelete(TMemoryPoolID memory){
    if(memory == RVCOS_MEMORY_POOL_ID_SYSTEM){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    for(int i = 0; i < pool_num_alloc; i++){
        if(Poollist[i].ID == memory){
            if(Poollist[i].OGMemorySize != Poollist[i].MemorySize){
                return RVCOS_STATUS_ERROR_INVALID_STATE;
            }
            for(int j = i; j < pool_num_alloc-1; j++){
                Poollist[j] = Poollist[j+1];
            }
            pool_num_alloc--;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_PARAMETER; 
}

TStatus RVCMemoryPoolQuery(TMemoryPoolID memory, TMemorySizeRef bytesleft){
    if(bytesleft == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    for(int i = 0; i<pool_num_alloc; i++){
        if(Poollist[i].ID == memory){
            *bytesleft = Poollist[i].MemorySize;
             return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
}

int sdelete_mem_node(MemoryPoolRef head, int size){//https://www.geeksforgeeks.org/linked-list-set-3-deleting-node/
    int temp = head->FirstFree, prev;
    if(temp != -1 && megaFreeList[temp].size >= size){
        int numberC = size/64;
        if(megaFreeList[temp].size > size){
            megaFreeList[temp+numberC].size = megaFreeList[temp].size - size;
            megaFreeList[temp].size = size;
            head->FirstFree = temp+numberC;
            head->MemorySize -= size;
        }
        else{
            megaFreeList[temp].size = size;
            int new = megaFreeList[temp].next;
            head->FirstFree = new;
            head->MemorySize -= size;   
        }
        return temp;
    }
    while(temp != -1 && megaFreeList[temp].size < size){
        prev = temp;
        temp = megaFreeList[temp].next;
    }
    if(temp == -1){
        return -1;
    }
        int numberC = size/64;
        megaFreeList[temp].size = size;
        if(megaFreeList[temp].size > size){
            megaFreeList[temp+numberC].size = megaFreeList[temp].size - size;
            megaFreeList[temp].size = size;
            megaFreeList[prev].next = temp+numberC;
            head->MemorySize -= size;
        }
        else{
            megaFreeList[temp].size = size;
            int new = megaFreeList[temp].next;
            megaFreeList[prev].next = new;
            head->MemorySize -= size;   
        }
    return temp;
}

TStatus RVCMemoryPoolAllocate(TMemoryPoolID memory, TMemorySize size, void **pointer){
    if(size == 0 || pointer == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(memory == 0){
        *pointer = malloc(size);
        return RVCOS_STATUS_SUCCESS;
    }
    else{
        for(int i = 0; i < pool_num_alloc; i++){
            if(Poollist[i].ID == memory){
                if(Poollist[i].MemorySize < size){
                    return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                }
                int round = roundUpInt(size, 64);
                if(round == -1){
                    return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                }
                int base = sdelete_mem_node(&Poollist[i], round);
                *pointer = megaFreeList[base].base;
                return RVCOS_STATUS_SUCCESS;
            }
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
}

int add_node(MemoryPoolRef head, void* ptr){//https://www.geeksforgeeks.org/linked-list-set-3-deleting-node/
    int temp = head->FirstFree;
    int prev = -1;
    int found = -1;
    for(int i = 0; i < 1000; i++){
        if(megaFreeList[i].base == ptr){
            found = i;
            break;
        }
    }
    if(found == -1){
        return 0;
    }
    if(temp == -1){
        head->FirstFree = found;
        megaFreeList[found].next = temp;
        head->MemorySize = megaFreeList[found].size;
        return 1;
    }
    while(temp != -1 && ptr > megaFreeList[temp].base){
        prev = temp;
        temp = megaFreeList[temp].next;
    }
    head->MemorySize += megaFreeList[found].size;
    int numberC = megaFreeList[found].size/64;
    if(prev == -1){
        if(found+numberC == temp){
            head->FirstFree = found;
            megaFreeList[found].next = megaFreeList[temp].next;
            megaFreeList[found].size += megaFreeList[temp].size;
        }
        else{
            head->FirstFree = found;
            megaFreeList[found].next = temp;
        }
        return 1;
    }
    int numberC2 = megaFreeList[prev].size/64;
    if(found+numberC == temp){
        megaFreeList[prev].next = found;
        megaFreeList[found].next = megaFreeList[temp].next;
        megaFreeList[found].size += megaFreeList[temp].size;
    }
    else{
        megaFreeList[prev].next = found;
        megaFreeList[found].next = temp;
    }
    if(prev+numberC2 == found){
        megaFreeList[prev].next = megaFreeList[found].next;
        megaFreeList[prev].size += megaFreeList[found].size;
    }
    else{
        megaFreeList[found].next = temp;
    }
    return 1;
}
 
TStatus RVCMemoryPoolDeallocate(TMemoryPoolID memory, void *pointer){
    if(pointer == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(memory == 0){
        free(pointer);
        return RVCOS_STATUS_SUCCESS;
    }
    else{
        for(int i = 0; i < pool_num_alloc; i++){
            if(Poollist[i].ID == memory){
                if(!add_node(&Poollist[i], pointer)){
                    return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
                }
                return RVCOS_STATUS_SUCCESS;
            }
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
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
            int popped = pop(&Mutexlist[i].queue);
            while(popped != -1){
                popped = pop(&Mutexlist[i].queue);
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
    for(int i = 0; i < mutex_num; i++){
        if(Mutexlist[i].Midentifier == mutex){
            if(Mutexlist[i].lock == 0){
                return RVCOS_THREAD_ID_INVALID;
            }
            *ownerref = Mutexlist[i].Owner;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
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
TStatus RVCChangeVideoMode(TVideoMode mode){
    if(mode < RVCOS_VIDEO_MODE_TEXT || mode > RVCOS_VIDEO_MODE_GRAPHICS){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(mode == RVCOS_VIDEO_MODE_TEXT){
        MODE &= ~(1 << 0);
    }
    if(mode == RVCOS_VIDEO_MODE_GRAPHICS){
        MODE |= 1 << 0;
    }
    TThreadID tp = get_tp();
    struct TCB *current = find_node(&Threadlist2, tp);
    current -> state = RVCOS_THREAD_STATE_WAITING;
    current -> waitwrite = "idk";
    current -> size = 0;
    insertWaitQueue(tp);
    schedule_new_thread();
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCSetVideoUpcall(TThreadEntry upcall, void *param){

}

TStatus RVCGraphicCreate(TGraphicType type, TGraphicIDRef gidref){
    if(gidref == NULL || type > 2 || type < 0){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    int buffersize = 0;
    if(type == RVCOS_GRAPHIC_TYPE_FULL){
        buffersize = 512*228;
    }
    if(type == RVCOS_GRAPHIC_TYPE_LARGE){
        buffersize = 64*64;
    }
    if(type == RVCOS_GRAPHIC_TYPE_SMALL){
        buffersize = 16*16;
    }
    //Graphics[graphic_alloc].buffer = malloc(buffersize);
    if(RVCMemoryAllocate(buffersize, &Graphics[graphic_alloc].buffer) == RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES){
        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    Graphics[graphic_alloc].ID = graphic_id;
    Graphics[graphic_alloc].type = type;
    Graphics[graphic_alloc].status = RVCOS_BUFFER_DEACTIVATED;
    Graphics[graphic_alloc].spriteN = -1;
    *gidref = graphic_id;
    graphic_alloc++;
    graphic_id++;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCGraphicDelete(TGraphicID gid){
}

SGraphicPosition find_dest(SGraphicPositionRef pos, TGraphicType type){
    SGraphicPosition translate;
    translate.DXPosition = pos->DXPosition;
    translate.DYPosition = pos->DYPosition;
    translate.DZPosition = pos->DZPosition;
    if(type == RVCOS_GRAPHIC_TYPE_FULL){
        translate.DXPosition = pos->DXPosition + 512;
        translate.DYPosition = pos->DYPosition + 288;
    }
    else if(type == RVCOS_GRAPHIC_TYPE_LARGE){
        translate.DXPosition = pos->DXPosition + 64;
        translate.DYPosition = pos->DYPosition + 64;
    }
    else{
        translate.DXPosition = pos->DXPosition + 16;
        translate.DYPosition = pos->DYPosition + 16;
    }
    return translate;
}

SGraphicDimensions find_dest2(SGraphicDimensionsRef dim, TGraphicType type){
    SGraphicDimensions translate;
    translate.DWidth = dim->DWidth;
    translate.DHeight = dim->DHeight;
    if(type == RVCOS_GRAPHIC_TYPE_LARGE){
        translate.DWidth = dim->DWidth - 33;
        translate.DHeight = dim->DHeight - 33;
    }
    else if(type == RVCOS_GRAPHIC_TYPE_SMALL){
        translate.DWidth = dim->DWidth - 1;
        translate.DHeight = dim->DHeight - 1;
    }
    return translate;
}

TStatus RVCGraphicActivate(TGraphicID gid, SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteID pid){
    if(pos == NULL){
        return RVCOS_STATUS_ERROR_INVALID_STATE;
    }
    for(int i = 0; i < graphic_alloc; i++){
        if(Graphics[i].ID == gid){
            if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_FULL){
                if(pos->DXPosition < -512 || pos->DXPosition >= 512 || pos->DYPosition < -288 || pos->DYPosition >= 288
                || pos->DZPosition < 0 || pos->DZPosition >= 8){
                    return RVCOS_STATUS_ERROR_INVALID_STATE;
                }
                int start = -1;
                if(Graphics[i].status == RVCOS_BUFFER_DEACTIVATED){
                    for(int j = 0; j < 5; j++){
                        if(backgroundAvailable[j].used == 0){
                            start = j;
                            break;
                        }
                    }
                    if(start == -1){
                        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                    }
                    backgroundAvailable[start].used = 1;
                    Graphics[i].spriteN = start;
                }
                int start2 = -1;
                for(int j = 0; j < palette_alloc; j++){
                    if(palette[j].ID == pid){
                        start2 = j;
                        if(palette[j].activated == 0){
                            palette[j].activated == 1;
                            for(int k = 0; k < 4; k++){
                                if(PbackgroundAvailable[k].used == 0){
                                    palette[j].activatedBy = k;
                                    PbackgroundAvailable[k].ID = palette[j].ID;
                                    PbackgroundAvailable[k].used = 1;
                                    memcpy((void *)BackgroundPalettes[k],palette[j].RVCOPaletteColors,256 * sizeof(SColor));
                                    break;
                                }
                            }
                        }
                        if(!palette[j].acquired){
                            RVCMemoryAllocate(sizeof(palette[j].acquired), &palette[j].acquired);
                            palette[j].acquired->usedBy = gid;
                            palette[j].acquired->next = NULL;
                        }
                        else{
                            struct acquired *new;
                            RVCMemoryAllocate(sizeof(new), &new);
                            new->usedBy = gid;
                            new->next = palette[j].acquired;
                            palette[j].acquired = new;
                        }
                    }
                }
                SGraphicPosition offset = find_dest(pos, Graphics[i].type);
                if(backgroundAvailable[start].ID != gid || Graphics[i].status == RVCOS_BUFFER_ACTIVATED){
                    memcpy((void *)0x50000000 + 512 * 288 * Graphics[i].spriteN,Graphics[i].buffer,512*288);
                }
                Graphics[i].status = RVCOS_BUFFER_ACTIVATED;
                Graphics[i].palette = palette[start2].ID;
                backgroundAvailable[start].ID = gid;
                BackgroundControls[Graphics[i].spriteN].DPalette = palette[start2].activatedBy;
                BackgroundControls[Graphics[i].spriteN].DXOffset = offset.DXPosition;
                BackgroundControls[Graphics[i].spriteN].DYOffset = offset.DYPosition;
                BackgroundControls[Graphics[i].spriteN].DZ = offset.DZPosition;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_LARGE){
                if(dim == NULL || pos->DXPosition < -64 || pos->DXPosition >= 512 || pos->DYPosition < -64 || 
                pos->DYPosition >= 288 || pos->DZPosition < 0 || pos->DZPosition >= 8 || dim->DWidth < 33 || dim->DWidth > 64
                || dim->DHeight < 33 || dim->DHeight > 64){
                    return RVCOS_STATUS_ERROR_INVALID_STATE;
                }
                int start = -1;
                if(Graphics[i].status == RVCOS_BUFFER_DEACTIVATED){
                    for(int j = 0; j < 64; j++){
                        if(LSpriteAvailable[j].used == 0){
                            start = j;
                            break;
                        }
                    }
                    if(start == -1){
                        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                    }
                    LSpriteAvailable[start].used = 1;
                    Graphics[i].spriteN = start;
                }
                int start2 = -1;
                for(int j = 0; j < palette_alloc; j++){
                    if(palette[j].ID == pid){
                        start2 = j;
                        if(palette[j].activated == 0){
                            palette[j].activated == 1;
                            for(int k = 0; k < 4; k++){
                                if(PSpriteAvailable[k].used == 0){
                                    palette[j].activatedBy = k;
                                    PSpriteAvailable[k].ID = palette[j].ID;
                                    PSpriteAvailable[k].used = 1;
                                    memcpy((void *)SpritePalettes[k],palette[j].RVCOPaletteColors,256 * sizeof(SColor));
                                    break;
                                }
                            }
                        }
                        if(!palette[j].acquired){
                            RVCMemoryAllocate(sizeof(palette[j].acquired), &palette[j].acquired);
                            palette[j].acquired->usedBy = gid;
                            palette[j].acquired->next = NULL;
                        }
                        else{
                            struct acquired *new;
                            RVCMemoryAllocate(sizeof(new), &new);
                            new->usedBy = gid;
                            new->next = palette[j].acquired;
                            palette[j].acquired = new;
                        }
                    }
                }
                SGraphicPosition offset = find_dest(pos, Graphics[i].type);
                SGraphicDimensions offset2 = find_dest2(dim, Graphics[i].type);
                if(LSpriteAvailable[start].ID != gid || Graphics[i].status == RVCOS_BUFFER_ACTIVATED){
                    memcpy((void *)0x500B4000 + 64 * 64 * Graphics[i].spriteN,Graphics[i].buffer,64*64);
                }
                Graphics[i].status = RVCOS_BUFFER_ACTIVATED;
                Graphics[i].palette = palette[start2].ID;
                LSpriteAvailable[start].ID = gid;
                LargeSpriteControls[Graphics[i].spriteN].DPalette = palette[start2].activatedBy;
                LargeSpriteControls[Graphics[i].spriteN].DXOffset = offset.DXPosition;
                LargeSpriteControls[Graphics[i].spriteN].DYOffset = offset.DYPosition;
                LargeSpriteControls[Graphics[i].spriteN].DWidth = offset2.DWidth;
                LargeSpriteControls[Graphics[i].spriteN].DHeight = offset2.DHeight;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_SMALL){

                if(dim == NULL || pos->DXPosition < -16 || pos->DXPosition >= 512 || pos->DYPosition < -16 || 
                pos->DYPosition >= 288 || pos->DZPosition < 0 || pos->DZPosition >= 8 || dim->DWidth < 1 || dim->DWidth > 16
                || dim->DHeight < 1 || dim->DHeight > 16){
                    return RVCOS_STATUS_ERROR_INVALID_STATE;
                }
                int start = -1;
                if(Graphics[i].status == RVCOS_BUFFER_DEACTIVATED){
                    for(int j = 0; j < 128; j++){
                        if(SSpriteAvailable[j].used == 0){
                            start = j;
                            break;
                        }
                    }
                    if(start == -1){
                        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
                    }
                    SSpriteAvailable[start].used = 1;
                    Graphics[i].spriteN = start;
                }
                int start2 = -1;
                for(int j = 0; j < palette_alloc; j++){
                    if(palette[j].ID == pid){
                        start2 = j;
                        if(palette[j].activated == 0){
                            palette[j].activated == 1;
                            for(int k = 0; k < 4; k++){
                                if(PSpriteAvailable[k].used == 0){
                                    palette[j].activatedBy = k;
                                    PSpriteAvailable[k].ID = palette[j].ID;
                                    PSpriteAvailable[k].used = 1;
                                    memcpy((void *)SpritePalettes[k],palette[j].RVCOPaletteColors,256 * sizeof(SColor));
                                    break;
                                }
                            }
                        }
                        if(!palette[j].acquired){
                            RVCMemoryAllocate(sizeof(palette[j].acquired), &palette[j].acquired);
                            palette[j].acquired->usedBy = gid;
                            palette[j].acquired->next = NULL;
                        }
                        else{
                            struct acquired *new;
                            RVCMemoryAllocate(sizeof(new), &new);
                            new->usedBy = gid;
                            new->next = palette[j].acquired;
                            palette[j].acquired = new;
                        }
                    }
                }
                SGraphicPosition offset = find_dest(pos, Graphics[i].type);
                SGraphicDimensions offset2 = find_dest2(dim, Graphics[i].type);
                if(SSpriteAvailable[start].ID != gid || Graphics[i].status == RVCOS_BUFFER_ACTIVATED){
                    memcpy((void *)0x500F4000 + 16 * 16 * Graphics[i].spriteN,Graphics[i].buffer,16*16);
                }
                Graphics[i].status = RVCOS_BUFFER_ACTIVATED;
                Graphics[i].palette = palette[start2].ID;
                SSpriteAvailable[start].ID = gid;
                SmallSpriteControls[Graphics[i].spriteN].DPalette = palette[start2].activatedBy;
                SmallSpriteControls[Graphics[i].spriteN].DXOffset = offset.DXPosition;
                SmallSpriteControls[Graphics[i].spriteN].DYOffset = offset.DYPosition;
                SmallSpriteControls[Graphics[i].spriteN].DZ = offset.DZPosition;
                SmallSpriteControls[Graphics[i].spriteN].DWidth = offset2.DWidth;
                SmallSpriteControls[Graphics[i].spriteN].DHeight = offset2.DHeight;
            }
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
}

TStatus RVCGraphicDeactivate(TGraphicID gid){
    for(int i = 0; i < graphic_alloc; i++){
        if(Graphics[i].ID == gid){
            if(Graphics[i].status == RVCOS_BUFFER_DEACTIVATED || Graphics[i].status == RVCOS_BUFFER_PENDING){
                return RVCOS_STATUS_ERROR_INVALID_STATE;
            }
            for(int j = 0; j < palette_alloc; j++){
                if(palette[j].ID == Graphics[i].palette){
                    struct acquired *temp = palette[j].acquired, *prev;
                    if(temp->next == NULL){
                        RVCMemoryDeallocate(temp);
                        palette[j].activated = 0;
                        if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_FULL){
                            PbackgroundAvailable[palette[j].activatedBy].used = 0;
                        }
                        else{
                            PSpriteAvailable[palette[j].activatedBy].used = 0;
                        }
                    }
                    else{
                        while(temp != NULL && temp->usedBy != Graphics[i].ID){
                            prev = temp;
                            temp = temp->next;
                        }
                        if(temp != NULL){
                            prev->next = temp->next;
                            RVCMemoryDeallocate(temp);
                        }
                    }
                    break;
                }
            }
            if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_FULL){
                BackgroundControls[Graphics[i].spriteN].DPalette = 0;
                BackgroundControls[Graphics[i].spriteN].DXOffset = 0;
                BackgroundControls[Graphics[i].spriteN].DYOffset = 0;
                BackgroundControls[Graphics[i].spriteN].DZ = 0;
                backgroundAvailable[Graphics[i].spriteN].used = 0;
                Graphics[i].spriteN = -1;
                Graphics[i].status = RVCOS_BUFFER_DEACTIVATED;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_LARGE){
                LargeSpriteControls[Graphics[i].spriteN].DPalette = 0;
                LargeSpriteControls[Graphics[i].spriteN].DXOffset = 0;
                LargeSpriteControls[Graphics[i].spriteN].DYOffset = 0;
                LargeSpriteControls[Graphics[i].spriteN].DWidth = 0;
                LargeSpriteControls[Graphics[i].spriteN].DHeight = 0;
                LSpriteAvailable[Graphics[i].spriteN].used = 0;
                Graphics[i].spriteN = -1;
                Graphics[i].status = RVCOS_BUFFER_DEACTIVATED;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_SMALL){
                SmallSpriteControls[Graphics[i].spriteN].DPalette = 0;
                SmallSpriteControls[Graphics[i].spriteN].DXOffset = 0;
                SmallSpriteControls[Graphics[i].spriteN].DYOffset = 0;
                SmallSpriteControls[Graphics[i].spriteN].DZ = 0;
                SmallSpriteControls[Graphics[i].spriteN].DWidth = 0;
                SmallSpriteControls[Graphics[i].spriteN].DHeight = 0;
                SSpriteAvailable[Graphics[i].spriteN].used = 0;
                Graphics[i].spriteN = -1;
                Graphics[i].status = RVCOS_BUFFER_DEACTIVATED;
            }
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
}

void overlap_check(TGraphicType type, SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteIndexRef src, uint32_t srcwidth){
    if(pos->DXPosition < 0){
        src = src - pos->DXPosition;
        dim->DWidth = dim->DWidth + pos->DXPosition;
        pos->DXPosition = 0;
    }
    if(pos->DYPosition < 0){
        src = src + ((-1)*pos->DYPosition)*srcwidth;
        dim->DHeight = dim->DHeight + pos->DYPosition;
        pos->DYPosition = 0;
    }
    if(type == RVCOS_GRAPHIC_TYPE_FULL){
        if(pos->DXPosition + dim->DWidth > 512){
            int help = pos->DXPosition + dim->DWidth;
            int offset = help - 512;
            dim->DWidth -= offset;
        }
        if(pos->DYPosition + dim->DHeight > 288){
            int help = pos->DYPosition + dim->DHeight;
            int offset = help - 288;
            dim->DHeight -= offset;
        }
    }
    else if(type == RVCOS_GRAPHIC_TYPE_LARGE){
        if(pos->DXPosition + dim->DWidth > 64){
            int help = pos->DXPosition + dim->DWidth;
            int offset = help - 64;
            dim->DWidth -= offset;
        }
        if(pos->DYPosition + dim->DHeight > 64){
            int help = pos->DYPosition + dim->DHeight;
            int offset = help - 64;
            dim->DHeight -= offset;
        }
    }
    else if(type == RVCOS_GRAPHIC_TYPE_SMALL){
        if(pos->DXPosition + dim->DWidth > 16){
            int help = pos->DXPosition + dim->DWidth;
            int offset = help - 16;
            dim->DWidth -= offset;
        }
        if(pos->DYPosition + dim->DHeight > 16){
            int help = pos->DYPosition + dim->DHeight;
            int offset = help - 16;
            dim->DHeight -= offset;
        }
    }
}

TStatus RVCGraphicDraw(TGraphicID gid, SGraphicPositionRef pos, SGraphicDimensionsRef dim, TPaletteIndexRef src, uint32_t srcwidth){
    if(pos == NULL || dim == NULL || src == NULL || srcwidth < dim->DWidth){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    for(int i = 0; i < graphic_alloc; i++){
        if(Graphics[i].ID == gid){
            overlap_check(Graphics[i].type, pos, dim, src, srcwidth);
            int starting = 0;
            int start = 0;
            if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_FULL){
                start = 512;
                starting = (pos->DYPosition*start) + pos->DXPosition;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_LARGE){
                start = 64;
                starting = (pos->DYPosition*start) + pos->DXPosition;
            }
            else if(Graphics[i].type == RVCOS_GRAPHIC_TYPE_SMALL){
                start = 16;
                starting = (pos->DYPosition*start) + pos->DXPosition;
            }
            if(start == 64){
            TPaletteIndexRef track = src;
            for(int j = 0; j < dim->DHeight; j++){
                if(j != 27){
                memcpy((void*) Graphics[i].buffer+starting, track, dim->DWidth);
                }
                starting += start;
                track += srcwidth;
            }
            }
            else{
            TPaletteIndexRef track = src;
            for(int j = 0; j < dim->DHeight; j++){
                memcpy((void*) Graphics[i].buffer+starting, track, dim->DWidth);
                starting += start;
                track += srcwidth;
            }
            }
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
}

TStatus RVCPaletteCreate(TPaletteIDRef pidref){
    if(pidref == NULL){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(RVCMemoryAllocate(256 * sizeof(SColor), &palette[palette_alloc].RVCOPaletteColors) == RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES){
        return RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    palette[palette_alloc].ID = palette_id;
    palette[palette_alloc].activated = 0;
    memcpy((void *)palette[palette_alloc].RVCOPaletteColors, RVCOPaletteDefaultColors, 256 * sizeof(SColor));
    *pidref = palette_id;
    palette_id++;
    palette_alloc++;
    return RVCOS_STATUS_SUCCESS;
}

TStatus RVCPaletteDelete(TPaletteID pid){
    for(int i = 0; i < palette_alloc; i++){
        if(palette[i].ID == pid){
            if(palette[i].activated){
                return RVCOS_STATUS_ERROR_INVALID_STATE;
            }
            RVCMemoryDeallocate(palette[i].RVCOPaletteColors);
            for(int j = i; j < palette_alloc-1; j++){
                palette[j] = palette[j+1];
            }
            palette_alloc--;
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
}

TStatus RVCPaletteUpdate(TPaletteID pid, SColorRef cols, TPaletteIndex offset, uint32_t count){
    if(cols == NULL || offset + count > 256){
        return RVCOS_STATUS_ERROR_INVALID_PARAMETER;
    }
    for(int i = 0; i < palette_alloc; i++){
        if(palette[i].ID == pid){
            SColorRef begin = palette[i].RVCOPaletteColors + offset;
            memcpy((void *)begin, cols, count * sizeof(SColor));
            return RVCOS_STATUS_SUCCESS;
        }
    }
    return RVCOS_STATUS_ERROR_INVALID_ID;
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
        case 23: return RVCChangeVideoMode((void *)p1);
        case 24: return RVCSetVideoUpcall((void *)p1, (void *)p2);
        case 25: return RVCGraphicCreate((void *)p1, (void *)p2);
        case 26: return RVCGraphicDelete((void *)p1);
        case 27: return RVCGraphicActivate((void *)p1, (void *)p2, (void *)p3, (void *)p4);
        case 28: return RVCGraphicDeactivate((void *)p1);
        case 29: return RVCGraphicDraw((void *)p1, (void *)p2, (void *)p3, (void *)p4, (void *)p5);
        case 30: return RVCPaletteCreate((void *)p1);
        case 31: return RVCPaletteDelete((void *)p1);
        case 32: return RVCPaletteUpdate((void *)p1, (void *)p2, (void *)p3, (void *)p4);
    }
}
