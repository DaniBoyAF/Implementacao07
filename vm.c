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

// Função das Threads
void* search_tlb_thread(void* arg) {
    int i = *(int*)arg;
    if (tlb[i].valid && tlb[i].page_number == target_page) {
        pthread_mutex_lock(&tlb_mutex);
        found_frame = tlb[i].frame_number;
        found_index = i;
        pthread_mutex_unlock(&tlb_mutex);
    }
    return NULL;
}

// Algoritmos de Substituição TLB
void update_tlb(int page_num, int frame_num) {
    int target_index = -1;
    int min_time = 2147483647;

    // Procura se já existe na TLB
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid && tlb[i].page_number == page_num) {
            target_index = i;
            break;
        }
    }

    // Se não existe, procura um espaço vazio ou aplica substituição
    if (target_index == -1) {
        for (int i = 0; i < TLB_SIZE; i++) {
            if (!tlb[i].valid) {
                target_index = i;
                break;
            }
        }
    }

    if (target_index == -1) {
        if (strcmp(tlb_algo, "fifo") == 0) {
            for (int i = 0; i < TLB_SIZE; i++) {
                if (tlb[i].added_time < min_time) {
                    min_time = tlb[i].added_time;
                    target_index = i;
                }
            }
        } else { // LRU
            for (int i = 0; i < TLB_SIZE; i++) {
                if (tlb[i].last_used < min_time) {
                    min_time = tlb[i].last_used;
                    target_index = i;
                }
            }
        }
    }

    tlb[target_index].page_number = page_num;
    tlb[target_index].frame_number = frame_num;
    tlb[target_index].valid = 1;
    tlb[target_index].last_used = global_time;
    tlb[target_index].added_time = global_time;
}

// Algoritmos de Substituição de Página
int handle_page_fault(int page_num, FILE* bs) {
    int frame_to_use = -1;
    int min_time = 2147483647;

    // Verifica se há frames livres
    if (next_frame < NUM_FRAMES) {
        frame_to_use = next_frame++;
    } else {
        // Substituição
        if (strcmp(page_algo, "fifo") == 0) {
            for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
                if (page_table[i].valid && page_table[i].added_time < min_time) {
                    min_time = page_table[i].added_time;
                    frame_to_use = page_table[i].frame_number;
                    page_table[i].valid = 0; // Invalida a entrada antiga
                }
            }
        } else { // LRU
            for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
                if (page_table[i].valid && page_table[i].last_used < min_time) {
                    min_time = page_table[i].last_used;
                    frame_to_use = page_table[i].frame_number;
                    page_table[i].valid = 0;
                }
            }
        }
    }

    // Ler do BACKING_STORE
    fseek(bs, page_num * PAGE_SIZE, SEEK_SET);
    fread(physical_memory[frame_to_use], sizeof(signed char), PAGE_SIZE, bs);

    // Atualiza Page Table
    page_table[page_num].frame_number = frame_to_use;
    page_table[page_num].valid = 1;
    page_table[page_num].last_used = global_time;
    page_table[page_num].added_time = global_time;

    page_faults++;
    return frame_to_use;
}

void print_error_and_exit(const char* msg) {
    fprintf(stderr, "Erro: %s\n", msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    // Validação de argumentos
    if (argc != 4) {
        print_error_and_exit("Uso: ./vm <arquivo_enderecos> <algoritmo_pagina: fifo|lru> <algoritmo_tlb: fifo|lru>");
    }

    strcpy(page_algo, argv[2]);
    strcpy(tlb_algo, argv[3]);

    if ((strcmp(page_algo, "fifo") != 0 && strcmp(page_algo, "lru") != 0) ||
        (strcmp(tlb_algo, "fifo") != 0 && strcmp(tlb_algo, "lru") != 0)) {
        print_error_and_exit("Algoritmos inválidos. Use 'fifo' ou 'lru'.");
    }

    FILE *address_file = fopen(argv[1], "r");
    if (!address_file) {
        print_error_and_exit("Não foi possível abrir o arquivo de endereços.");
    }

    FILE *bs = fopen(BACKING_STORE, "rb");
    if (!bs) {
        print_error_and_exit("Arquivo BACKING_STORE.bin não encontrado. Execute 'python3 gen_backing_store.py' para gerá-lo.");
    }

    FILE *out_file = fopen("correct.txt", "w");
    if (!out_file) {
        print_error_and_exit("Não foi possível criar o arquivo correct.txt");
    }

    // Inicialização das estruturas
    memset(tlb, 0, sizeof(TLBEntry) * TLB_SIZE);
    memset(page_table, 0, sizeof(PageTableEntry) * PAGE_TABLE_SIZE);

    int logical_address;
    while (fscanf(address_file, "%d", &logical_address) == 1) {
        total_addresses++;
        
        int page_num = (logical_address >> 8) & 0xFF;
        int offset = logical_address & 0xFF;

        int frame_num = -1;
        int tlb_index = -1;

        // Busca na TLB com Threads
        target_page = page_num;
        found_frame = -1;
        found_index = -1;

        pthread_t threads[TLB_SIZE];
        int thread_args[TLB_SIZE];

        for (int i = 0; i < TLB_SIZE; i++) {
            thread_args[i] = i;
            pthread_create(&threads[i], NULL, search_tlb_thread, &thread_args[i]);
        }

        for (int i = 0; i < TLB_SIZE; i++) {
            pthread_join(threads[i], NULL);
        }

        if (found_frame != -1) {
            // TLB Hit
            frame_num = found_frame;
            tlb_index = found_index;
            tlb_hits++;
            tlb[tlb_index].last_used = global_time;
        } else {
            // TLB Miss
            if (page_table[page_num].valid) {
                // Page Hit
                frame_num = page_table[page_num].frame_number;
                page_table[page_num].last_used = global_time;
            } else {
                // Page Fault
                frame_num = handle_page_fault(page_num, bs);
            }
            // Atualiza TLB
            update_tlb(page_num, frame_num);
            
            // Descobre em qual índice da TLB a página foi inserida para o output
            for (int i = 0; i < TLB_SIZE; i++) {
                if (tlb[i].valid && tlb[i].page_number == page_num) {
                    tlb_index = i;
                    break;
                }
            }
        }

        global_time++;

        int physical_address = (frame_num << 8) | offset;
        signed char value = physical_memory[frame_num][offset];

        fprintf(out_file, "Virtual address: %d TLB: %d Physical address: %d Value: %d\n", 
                logical_address, tlb_index, physical_address, value);
    }

    // Escreve estatísticas
    fprintf(out_file, "Number of Translated Addresses = %d\n", total_addresses);
    fprintf(out_file, "Page Faults = %d\n", page_faults);
    fprintf(out_file, "Page Fault Rate = %.3f\n", (float)page_faults / total_addresses);
    fprintf(out_file, "TLB Hits = %d\n", tlb_hits);
    fprintf(out_file, "TLB Hit Rate = %.3f\n", (float)tlb_hits / total_addresses);

    fclose(address_file);
    fclose(bs);
    fclose(out_file);

    printf("Execução concluída. Verifique o arquivo 'correct.txt'.\n");

    return 0;
}