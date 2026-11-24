#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define PAGE_SIZE 256
#define TLB_SIZE 16
#define NUM_FRAMES 128
#define PAGE_TABLE_SIZE 256
#define BACKING_STORE "BACKING_STORE.bin"

// Estruturas
typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int last_used; // Para LRU
    int added_time; // Para FIFO
} TLBEntry;

typedef struct {
    int frame_number;
    int valid;
    int last_used; // Para LRU
    int added_time; // Para FIFO
} PageTableEntry;
// Variáveis Globais
TLBEntry tlb[TLB_SIZE];
PageTableEntry page_table[PAGE_TABLE_SIZE];
signed char physical_memory[NUM_FRAMES][PAGE_SIZE];

int page_faults = 0;
int tlb_hits = 0;
int total_addresses = 0;

int next_frame = 0;
int global_time = 0;

char page_algo[10];
char tlb_algo[10];

// Variáveis para Threads da TLB
int target_page;
int found_frame = -1;
int found_index = -1;
pthread_mutex_t tlb_mutex = PTHREAD_MUTEX_INITIALIZER;