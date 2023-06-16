#include <stdint.h>
#include <stdlib.h>
#include "RVCOS.h"

#define EXPECTED_RETURN             5

TThreadID MainThreadID, LowThreadID, HighThreadID;

void WriteString(const char *str){
    const char *Ptr = str;
    while(*Ptr){
        Ptr++;
    }
    RVCWriteText(str,Ptr-str);
}

TThreadReturn LowPriorityThread(void *param){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;
    TThreadReturn ReturnValue;

    RVCWriteText("Low Thread:    ", 16);
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != LowThreadID){
        RVCWriteText("Invalid Thread ID\n", 19);
        return 1;
    }
    RVCWriteText("Valid Thread ID\n", 17);
    RVCWriteText("Checking Main: ",16);
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_WAITING != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCWriteText("Checking This: ",16);
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCThreadActivate(HighThreadID);
    RVCThreadWait(HighThreadID,&ReturnValue);
    RVCWriteText("Low Awake:     ",16);
    if(EXPECTED_RETURN != ReturnValue){
        RVCWriteText("Invalid Return\n",16);
        return 0;
    }
    RVCWriteText("Valid Return\n",14);
    RVCThreadTerminate(LowThreadID,ReturnValue);

    return 0;
}

TThreadReturn HighPriorityThread(void *param){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;

    RVCWriteText("High Thread:   ",16);
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != HighThreadID){
        RVCWriteText("Invalid Thread ID\n",19);
        return 1;
    }
    RVCWriteText("Valid Thread ID\n",17);
    RVCWriteText("Checking Main: ",16);
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_WAITING != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCWriteText("Checking This: ",16);
    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    return EXPECTED_RETURN;
}

int main(){
    TThreadID CurrentThreadID;
    TThreadState ThreadState;
    TThreadReturn ReturnValue;

    RVCWriteText("Main Thread:   ",16);
    RVCThreadID(&MainThreadID);
    RVCThreadState(MainThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_RUNNING != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCThreadCreate(HighPriorityThread,NULL,2048,RVCOS_THREAD_PRIORITY_HIGH,&HighThreadID);
    RVCWriteText("High Created:  ",16);
    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_CREATED != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",15);
    RVCThreadCreate(LowPriorityThread,NULL,2048,RVCOS_THREAD_PRIORITY_LOW,&LowThreadID);
    RVCWriteText("Low Created:   ",16);
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_CREATED != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCThreadActivate(LowThreadID);
    RVCWriteText("Low Activated: ",16);
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_READY != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCThreadWait(LowThreadID,&ReturnValue);
    
    RVCWriteText("Checking Low:  ",16);
    if(EXPECTED_RETURN != ReturnValue){
        RVCWriteText("Invalid Return\n",16);
        return 1;
    }
    RVCThreadState(LowThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_DEAD != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid Return and State\n",24);

    RVCWriteText("Checking High: ",16);
    RVCThreadState(HighThreadID,&ThreadState);
    if(RVCOS_THREAD_STATE_DEAD != ThreadState){
        RVCWriteText("Invalid State\n",15);
        return 1;
    }
    RVCWriteText("Valid State\n",13);
    RVCThreadID(&CurrentThreadID);
    if(CurrentThreadID != MainThreadID){
        RVCWriteText("Invalid Main Thread ID\n",24);
        return 1;
    }
    RVCWriteText("Main Exiting\n",14);
    
    return 0;
}
