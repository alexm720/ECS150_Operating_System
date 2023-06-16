#ifndef RVCOS_H
#define RVCOS_H
#include <stdint.h>
#define RVCOS_STATUS_FAILURE                        ((TStatus)0x00)
#define RVCOS_STATUS_SUCCESS                        ((TStatus)0x01)
#define RVCOS_STATUS_ERROR_INVALID_PARAMETER        ((TStatus)0x02)
#define RVCOS_STATUS_ERROR_INVALID_ID               ((TStatus)0x03)
#define RVCOS_STATUS_ERROR_INVALID_STATE            ((TStatus)0x04)
#define RVCOS_STATUS_ERROR_INSUFFICIENT_RESOURCES   ((TStatus)0x05)
#define RVCOS_THREAD_STATE_CREATED                  ((TThreadState)0x01)
#define RVCOS_THREAD_STATE_DEAD                     ((TThreadState)0x02)
#define RVCOS_THREAD_STATE_RUNNING                  ((TThreadState)0x03)
#define RVCOS_THREAD_STATE_READY                    ((TThreadState)0x04)
#define RVCOS_THREAD_STATE_WAITING                  ((TThreadState)0x05)
#define RVCOS_THREAD_PRIORITY_LOW                   ((TThreadPriority)0x01)
#define RVCOS_THREAD_PRIORITY_NORMAL                ((TThreadPriority)0x02)
#define RVCOS_THREAD_PRIORITY_HIGH                  ((TThreadPriority)0x03)
#define RVCOS_THREAD_ID_INVALID                     ((TThreadID)-1)
#define RVCOS_TIMEOUT_INFINITE                      ((TTick)0)
#define RVCOS_TIMEOUT_IMMEDIATE                     ((TTick)-1)
#define RVCOS_MEMORY_POOL_ID_SYSTEM                 ((TMemoryPoolID)0)
#define RVCOS_MEMORY_POOL_ID_INVALID                ((TMemoryPoolID)-1)
#define RVCOS_MUTEX_ID_INVALID                      ((TMutexID)-1)
typedef uint32_t TStatus, *TStatusRef;
typedef uint32_t TTick, *TTickRef;
typedef int32_t  TThreadReturn, *TThreadReturnRef;
typedef uint32_t TMemorySize, *TMemorySizeRef;
typedef uint32_t TThreadID, *TThreadIDRef;
typedef uint32_t TThreadPriority, *TThreadPriorityRef;
typedef uint32_t TThreadState, *TThreadStateRef;
typedef char     TTextCharacter, *TTextCharacterRef;
typedef uint32_t TMemoryPoolID, *TMemoryPoolIDRef;
typedef uint32_t TMutexID, *TMutexIDRef;
typedef TThreadReturn (*TThreadEntry)(void *);
typedef struct{
    uint32_t DLeft:1;
    uint32_t DUp:1;
    uint32_t DDown:1;
    uint32_t DRight:1;
    uint32_t DButton1:1;
    uint32_t DButton2:1;
    uint32_t DButton3:1;
    uint32_t DButton4:1;
    uint32_t DReserved:24;
} SControllerStatus, *SControllerStatusRef;
TStatus RVCInitialize(uint32_t *gp);
TStatus RVCTickMS(uint32_t *tickmsref);
TStatus RVCTickCount(TTickRef tickref);
TStatus RVCThreadCreate(TThreadEntry entry, void *param, TMemorySize memsize,
TThreadPriority prio, TThreadIDRef tid);
TStatus RVCThreadDelete(TThreadID thread);
TStatus RVCThreadActivate(TThreadID thread);
TStatus RVCThreadTerminate(TThreadID thread, TThreadReturn returnval);
TStatus RVCThreadWait(TThreadID thread, TThreadReturnRef returnref, TTick timeout);
TStatus RVCThreadID(TThreadIDRef threadref);
TStatus RVCThreadState(TThreadID thread, TThreadStateRef stateref);
TStatus RVCThreadSleep(TTick tick);
#define RVCMemoryAllocate(size,pointer)             RVCMemoryPoolAllocate(RVCOS_MEMORY_POOL_ID_SYSTEM, (size), (pointer))
#define RVCMemoryDeallocate(pointer)                RVCMemoryPoolDeallocate(RVCOS_MEMORY_POOL_ID_SYSTEM, (pointer))
TStatus RVCMemoryPoolCreate(void *base, TMemorySize size, TMemoryPoolIDRef
memoryref);
TStatus RVCMemoryPoolDelete(TMemoryPoolID memory);
TStatus RVCMemoryPoolQuery(TMemoryPoolID memory, TMemorySizeRef bytesleft);
TStatus RVCMemoryPoolAllocate(TMemoryPoolID memory, TMemorySize size, void
**pointer);
TStatus RVCMemoryPoolDeallocate(TMemoryPoolID memory, void *pointer);
TStatus RVCMutexCreate(TMutexIDRef mutexref);
TStatus RVCMutexDelete(TMutexID mutex);
TStatus RVCMutexQuery(TMutexID mutex, TThreadIDRef ownerref);
TStatus RVCMutexAcquire(TMutexID mutex, TTick timeout);
TStatus RVCMutexRelease(TMutexID mutex);
TStatus RVCWriteText(const TTextCharacter *buffer, TMemorySize writesize);
TStatus RVCReadController(SControllerStatusRef statusref);
 
struct TCB{
    TThreadID Tidentifier;
    TThreadEntry FuncEntry;
    void * parameters;
    TMemorySize MemorySize;
    TThreadPriority priority;
    volatile uint32_t *BaseStack;
    TThreadState state;
    volatile uint32_t *StackPointer;
    TThreadReturn retval;
    TTextCharacter *waitwrite;
    TMemorySize size;
    int tick;
    TThreadID waiter;
    int *acquired;
    int mutex_num;
};
 
struct Node2{
    struct TCB* Thread;
    struct Node2* nextTCB;
};
 
struct Node{
        TThreadID Thread;
        struct Node* nextTCB;
 
};

typedef struct{
    struct Node *queue_lowest;
    struct Node *queue_low;
    struct Node *queue_normal;
    struct Node *queue_high;
}prioQueue;

typedef struct{
    TMutexID Midentifier;
    TThreadID Owner;
    int lock;
    prioQueue queue;
}Mutex;
 
struct MutexNode{
    Mutex *lock;
    struct MutexNode* nextMutex;
};
#define MIN_ALLOCATION_COUNT 0X40

typedef struct FREE_FNODE MemPoolFree, *MemPoolFreeRef;
struct FREE_FNODE{
	struct FREE_FNODE *next;
};
typedef struct FREE_CHUNK MemPoolFreeChunk, *MemPoolFreeChunkRef;

struct FREE_CHUNK{
	TMemorySize size;
	int allocated;
	MemPoolFreeChunkRef next;

};
 
typedef struct{
	TMemoryPoolID id;
	int chunkNum;
	void *FirstFree;
	TMemorySize MemorySize;
	TMemorySize Memoryleft;
	MemPoolFreeChunkRef blocks;

} MemoryPoolAllocater, *MemoryPoolAllocaterRef;


/*
struct poollst{
	TMemoryPoolID id;
	MemoryPoolAllocater curr;
	poollst
*/
typedef struct{
	TMemoryPoolID poolid;
	MemoryPoolAllocater curr;
	void *baseStart;
	struct PoolObj * next;
} PoolObj, *PoolObjRef;


void insertMemoryPool(MemoryPoolAllocater pool, void *base);
PoolObj * FindPool(PoolObj **head, TMemoryPoolID id);
void insertPrioQueue(TThreadID threadid, TThreadPriority prio);
void push(struct Node **head, TThreadID threadid);
TThreadID pop(struct Node **head);
struct Node* newNode(TThreadID threadid);
struct Node2* newNode2(struct TCB* thread);
struct TCB * find_node(const struct Node2 **head, TThreadID threadid);
void schedule_new_thread();
#endif
