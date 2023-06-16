#include <stdint.h>
#include <stddef.h>
#include "RVCOS.h"
extern uint8_t _erodata[];
extern uint8_t _data[];
extern uint8_t _edata[];
extern uint8_t _sdata[];
extern uint8_t _esdata[];
extern uint8_t _bss[];
extern uint8_t _ebss[];
// Adapted from https://stackoverflow.com/questions/58947716/how-to-interact-with-risc-v-csrs-by-using-gcc-c-code
__attribute__((always_inline)) inline uint32_t csr_mstatus_read(void){
    uint32_t result;
    asm volatile ("csrr %0, mstatus" : "=r"(result));
    return result;
}
 
__attribute__((always_inline)) inline void csr_mstatus_write(uint32_t val){
    asm volatile ("csrw mstatus, %0" : : "r"(val));
}
 
__attribute__((always_inline)) inline void csr_write_mie(uint32_t val){
    asm volatile ("csrw mie, %0" : : "r"(val));
}
 
__attribute__((always_inline)) inline void csr_enable_interrupts(void){
    asm volatile ("csrsi mstatus, 0x8");
}
 
__attribute__((always_inline)) inline void csr_disable_interrupts(void){
    asm volatile ("csrci mstatus, 0x8");
}
 
void* _sbrk(int incr) {
  extern char _heapbase;        /* Defined by the linker */
  static char *heap_end;
  char *prev_heap_end;
 
  if (heap_end == 0) {
    heap_end = &_heapbase;
  }
  prev_heap_end = heap_end;
  /*if (heap_end + incr > stack_ptr) {
    write (1, "Heap and stack collision\n", 25);
    abort ();
  }*/
 
  heap_end += incr;
  return (void*) prev_heap_end;
}
 
#define IER             (*((volatile uint32_t *)0x40000000))
#define IPR             (*((volatile uint32_t *)0x40000004))
#define MTIME_LOW       (*((volatile uint32_t *)0x40000008))
#define MTIME_HIGH      (*((volatile uint32_t *)0x4000000C))
#define MTIMECMP_LOW    (*((volatile uint32_t *)0x40000010))
#define MTIMECMP_HIGH   (*((volatile uint32_t *)0x40000014))
#define CONTROLLER      (*((volatile uint32_t *)0x40000018))
void init(void){
    uint8_t *Source = _erodata;
    uint8_t *Base = _data < _sdata ? _data : _sdata;
    uint8_t *End = _edata > _esdata ? _edata : _esdata;
 
    while(Base < End){
        *Base++ = *Source++;
    }
    Base = _bss;
    End = _ebss;
    while(Base < End){
        *Base++ = 0;
    }
 
    csr_write_mie(0x888);       // Enable all interrupt soruces
    IER |= 1 << 1; //https://www.codesdope.com/blog/article/set-toggle-and-clear-a-bit-in-c/ //enables VIE;
    csr_enable_interrupts();    // Global interrupt enable
    MTIMECMP_LOW = 2;
    MTIMECMP_HIGH = 0;
}
 
extern volatile int global;
extern volatile int Tick;
extern volatile struct Node2 *Threadlist2;
extern volatile struct Node *Waitlist;
extern volatile struct Node *queue_low;
extern volatile struct Node *queue_normal;
extern volatile struct Node *queue_high;
extern int thread_num;
volatile int mode = 0;
volatile int line = 0;
volatile int column = 0;
volatile int x_pos = 0;
volatile char *VIDEO_MEMORY = (volatile char *)(0x50000000 + 0xFE800);
 
void tickDown(struct Node2 **head){
    struct Node2 *start = (*head);
    while((start)){
        if(start -> Thread -> tick > 0){
            start -> Thread -> tick--;
            if(start -> Thread -> tick == 0){
                start -> Thread -> state = RVCOS_THREAD_STATE_READY;
                insertPrioQueue(start -> Thread -> Tidentifier, start -> Thread ->priority);
            }
        }
        start = start->nextTCB;
    }
}
 
 
void c_interrupt_handler(void){
    //int bit = (IPR >> 1) & 1;
    //if(bit){
    //    IPR |= 1 << 1;
    //}
    //else{
        Tick++;
        uint64_t NewCompare = (((uint64_t)MTIMECMP_HIGH)<<32) | MTIMECMP_LOW;
        NewCompare = MTIME_LOW + 2;
        MTIMECMP_HIGH = NewCompare>>32;
        MTIMECMP_LOW = NewCompare;
        tickDown(&Threadlist2);
        schedule_new_thread();
        global++;
    //}
}
 
void Interrupt_Write(const TTextCharacter *buffer, TMemorySize writesize){
        int digit = -1;
        int colDigit = -1;
        for(int i = 0; i < writesize; i++){
            if(mode == 0){
                if(buffer[i] == 0x8){
                    if(x_pos > 0){
                        x_pos--;
                        column--;
                        if(x_pos%64 == 0){
                            line--;
                            column = 63;
                        }
                    }
                }
                else if(buffer[i] == 0xA){
                    line++;
                    column = 0;
                    x_pos = line*64;
                }
                else if(buffer[i] == '\x1B'){
                    mode++;
                }
                else{
                    VIDEO_MEMORY[x_pos] = buffer[i];
                    x_pos++;
                    column++;
                    if(x_pos%64 == 0){
                        line++;
                        column = 0;
                    }
                }
            }
            else if(mode == 1){
                if(buffer[i] == '['){
                    mode++;
                }
                else{
                    mode = 0;
                }
            }
            else if(mode == 2){
                if(buffer[i] == 'A'){
                    if(line > 0){
                        line--;
                        x_pos -= 64;
                        line = x_pos/64;
                    }
                    mode = 0;
                }
                else if(buffer[i] == 'B'){
                    if(line < 36){
                        x_pos += 64;
                        line = x_pos/64;
                    }
                    mode = 0;
                }
                else if(buffer[i] == 'C'){
                    column++;
                    x_pos++;
                    if(x_pos%64 == 0){
                        line++;
                        column = 0;
                    }
                    mode = 0;
                }
                else if(buffer[i] == 'D'){
                    x_pos--;
                    column--;
                    if(x_pos%64 == 0){
                        line--;
                        column = 63;
                    }
                    mode = 0;
                }
                else if(buffer[i] == 'H'){
                    VIDEO_MEMORY[x_pos] = "";
                    x_pos = 0;
                    line = 0;
                    column = 0;
                    mode = 0;
                }
                else if(isdigit(buffer[i])){
                    mode++;
                    digit = buffer[i];
                }
                else{
                    mode = 0;
                }
            }
            else if(mode == 3){
                if(buffer[i] == 'J'){
                    if(digit == 2){
                        for(int i = 0; i<2304; i++){
                            VIDEO_MEMORY[i] = "";
                        }
                        VIDEO_MEMORY[x_pos] = "X";
                    }
                    digit = -1;
                    mode = 0;
                }
                else if(buffer[i] == ';'){
                    mode++;
                }
                else{
                    digit = -1;
                    mode = 0;
                }
            }
            else if(mode == 4){
                if(isdigit(buffer[i])){
                    colDigit = buffer[i];
                    mode++;
                }
                else{
                    digit = -1;
                    mode = 0;
                }
            }
            else if(mode == 5){
                if(buffer[i] == 'H'){
                    line = digit;
                    column = colDigit;
                    x_pos = (digit*64) + colDigit;
                    VIDEO_MEMORY[x_pos] = 'X';
                }
                digit = -1;
                colDigit = -1;
                mode = 0;
 
            }
 
        }
}
 
void video_interrupt_handler(void){
    IPR |= 1 << 1;
    int found = 0;
    TThreadID turn = pop(&Waitlist);
    while(turn != -1){
        found = 1;
        struct TCB *current = find_node(&Threadlist2, turn);
        Interrupt_Write(current -> waitwrite, current -> size);
        current -> state = RVCOS_THREAD_STATE_READY;
        insertPrioQueue(current -> Tidentifier, current -> priority);
        turn = pop(&Waitlist);
    }
    if(found == 1){
        schedule_new_thread();
    }
}
