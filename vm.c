/*
 * vm.c - Gerenciador de memória virtual (Silberschatz, "Designing a Virtual
 * Memory Manager"), com 128 frames físicos, substituição de página FIFO/LRU,
 * TLB de 16 entradas com substituição FIFO/LRU e busca na TLB com uma thread
 * por entrada.
 *
 * Uso: ./vm <arquivo_enderecos> <fifo|lru> <fifo|lru>
 *
 * Macros de compilação (apenas para demonstrar as etapas de desenvolvimento;
 * os valores padrão são os do enunciado):
 *   -DNUM_FRAMES=256   quantidade de frames físicos (padrão 128)
 *   -DUSE_TLB=0        desliga a TLB (padrão 1)
 *   -DUSE_THREADS=0    busca na TLB sem threads, sequencial (padrão 1)
 */
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef NUM_FRAMES
#define NUM_FRAMES 128
#endif
#ifndef USE_TLB
#define USE_TLB 1
#endif
#ifndef USE_THREADS
#define USE_THREADS 1
#endif

#define PAGE_SIZE 256
#define TLB_SIZE 16
#define PAGE_TABLE_SIZE 256
#define MAX_ADDRESS 65535
#define BACKING_STORE "BACKING_STORE.bin"
#define OUTPUT_FILE "correct.txt"
#define INT_MAX_TIME 2147483647

typedef struct {
    int page_number;
    int frame_number;
    int valid;
    int last_used;  /* LRU  */
    int added_time; /* FIFO */
} TLBEntry;

typedef struct {
    int frame_number;
    int valid;
    int last_used;  /* LRU  */
    int added_time; /* FIFO */
} PageTableEntry;

#if USE_TLB
static TLBEntry tlb[TLB_SIZE];
#endif
static PageTableEntry page_table[PAGE_TABLE_SIZE];
static signed char physical_memory[NUM_FRAMES][PAGE_SIZE];

static int page_faults = 0;
static int tlb_hits = 0;
static int total_addresses = 0;
static int next_frame = 0;
static int global_time = 0;

static char page_algo[10];
static char tlb_algo[10];

static void fail(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "Erro: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

#if USE_TLB && USE_THREADS
/* Variáveis compartilhadas pelas threads de busca na TLB */
static int target_page;
static int found_frame = -1;
static int found_index = -1;
static pthread_mutex_t tlb_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Cada thread examina exatamente uma entrada da TLB */
static void *search_tlb_thread(void *arg) {
    int i = *(int *)arg;
    if (tlb[i].valid && tlb[i].page_number == target_page) {
        pthread_mutex_lock(&tlb_mutex);
        found_frame = tlb[i].frame_number;
        found_index = i;
        pthread_mutex_unlock(&tlb_mutex);
    }
    return NULL;
}
#endif

#if USE_TLB
/* Procura a página na TLB. Devolve o índice da entrada ou -1. */
static int search_tlb(int page_num, int *frame_out) {
#if USE_THREADS
    pthread_t threads[TLB_SIZE];
    int args[TLB_SIZE];

    target_page = page_num;
    found_frame = -1;
    found_index = -1;

    for (int i = 0; i < TLB_SIZE; i++) {
        args[i] = i;
        if (pthread_create(&threads[i], NULL, search_tlb_thread, &args[i]) != 0) {
            fail("não foi possível criar a thread %d da TLB.", i);
        }
    }
    for (int i = 0; i < TLB_SIZE; i++) {
        pthread_join(threads[i], NULL);
    }

    *frame_out = found_frame;
    return found_index;
#else
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid && tlb[i].page_number == page_num) {
            *frame_out = tlb[i].frame_number;
            return i;
        }
    }
    return -1;
#endif
}

/* Insere a página na TLB (vaga livre ou vítima FIFO/LRU). Devolve o índice. */
static int update_tlb(int page_num, int frame_num) {
    int target_index = -1;
    int min_time = INT_MAX_TIME;

    for (int i = 0; i < TLB_SIZE; i++) {
        if (!tlb[i].valid) {
            target_index = i;
            break;
        }
    }

    if (target_index == -1) {
        int use_fifo = (strcmp(tlb_algo, "fifo") == 0);
        for (int i = 0; i < TLB_SIZE; i++) {
            int key = use_fifo ? tlb[i].added_time : tlb[i].last_used;
            if (key < min_time) {
                min_time = key;
                target_index = i;
            }
        }
    }

    tlb[target_index].page_number = page_num;
    tlb[target_index].frame_number = frame_num;
    tlb[target_index].valid = 1;
    tlb[target_index].last_used = global_time;
    tlb[target_index].added_time = global_time;
    return target_index;
}
#endif

/* Trata um page fault: escolhe o frame (livre ou vítima FIFO/LRU) e carrega a página. */
static int handle_page_fault(int page_num, FILE *bs) {
    int frame_to_use;

    if (next_frame < NUM_FRAMES) {
        frame_to_use = next_frame++;
    } else {
        int victim = -1;
        int min_time = INT_MAX_TIME;
        int use_fifo = (strcmp(page_algo, "fifo") == 0);

        for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
            if (!page_table[i].valid) {
                continue;
            }
            int key = use_fifo ? page_table[i].added_time : page_table[i].last_used;
            if (key < min_time) {
                min_time = key;
                victim = i;
            }
        }

        /* Somente a vítima é invalidada (após terminar a busca). */
        frame_to_use = page_table[victim].frame_number;
        page_table[victim].valid = 0;

#if USE_TLB
        /* A entrada da TLB que aponta para a vítima fica obsoleta. */
        for (int i = 0; i < TLB_SIZE; i++) {
            if (tlb[i].valid && tlb[i].page_number == victim) {
                tlb[i].valid = 0;
            }
        }
#endif
    }

    if (fseek(bs, (long)page_num * PAGE_SIZE, SEEK_SET) != 0 ||
        fread(physical_memory[frame_to_use], sizeof(signed char), PAGE_SIZE, bs) != PAGE_SIZE) {
        fail("falha ao ler a página %d de %s.", page_num, BACKING_STORE);
    }

    page_table[page_num].frame_number = frame_to_use;
    page_table[page_num].valid = 1;
    page_table[page_num].last_used = global_time;
    page_table[page_num].added_time = global_time;

    page_faults++;
    return frame_to_use;
}

/* Lê e valida todo o arquivo de endereços antes de qualquer processamento.
 * Formato esperado: um inteiro entre 0 e 65535 por linha. */
static int *load_addresses(const char *path, int *count) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fail("o arquivo de endereços '%s' não existe.", path);
    }
    if (!S_ISREG(st.st_mode)) {
        fail("'%s' não é um arquivo regular.", path);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fail("não foi possível abrir o arquivo de endereços '%s'.", path);
    }

    int capacity = 1024, n = 0;
    int *values = malloc((size_t)capacity * sizeof(*values));
    if (!values) {
        fail("memória insuficiente.");
    }

    char line[256];
    long line_number = 0;
    while (fgets(line, sizeof(line), f)) {
        line_number++;
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(f)) {
            fail("linha %ld do arquivo de endereços é longa demais.", line_number);
        }

        char *p = line;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            continue; /* linha em branco */
        }

        char *end;
        errno = 0;
        long value = strtol(p, &end, 10);
        if (end == p || errno != 0) {
            fail("formato inválido na linha %ld do arquivo de endereços (esperado um inteiro).", line_number);
        }
        while (isspace((unsigned char)*end)) {
            end++;
        }
        if (*end != '\0') {
            fail("formato inválido na linha %ld do arquivo de endereços (esperado um inteiro por linha).", line_number);
        }
        if (value < 0 || value > MAX_ADDRESS) {
            fail("endereço %ld na linha %ld fora do intervalo 0 a %d.", value, line_number, MAX_ADDRESS);
        }

        if (n == capacity) {
            capacity *= 2;
            int *grown = realloc(values, (size_t)capacity * sizeof(*values));
            if (!grown) {
                fail("memória insuficiente.");
            }
            values = grown;
        }
        values[n++] = (int)value;
    }

    if (ferror(f)) {
        fail("erro ao ler o arquivo de endereços '%s'.", path);
    }
    fclose(f);

    if (n == 0) {
        fail("o arquivo de endereços '%s' está vazio.", path);
    }

    *count = n;
    return values;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fail("uso: %s <arquivo_enderecos> <fifo|lru> <fifo|lru>", argv[0]);
    }

    if (strcmp(argv[2], "fifo") != 0 && strcmp(argv[2], "lru") != 0) {
        fail("algoritmo de página '%s' inválido (use fifo ou lru).", argv[2]);
    }
    if (strcmp(argv[3], "fifo") != 0 && strcmp(argv[3], "lru") != 0) {
        fail("algoritmo de TLB '%s' inválido (use fifo ou lru).", argv[3]);
    }
    strcpy(page_algo, argv[2]);
    strcpy(tlb_algo, argv[3]);

    int count = 0;
    int *addresses = load_addresses(argv[1], &count);

    struct stat bs_stat;
    if (stat(BACKING_STORE, &bs_stat) != 0) {
        fail("arquivo %s não encontrado (deve estar na pasta atual).", BACKING_STORE);
    }
    if (bs_stat.st_size < (off_t)PAGE_TABLE_SIZE * PAGE_SIZE) {
        fail("%s tem tamanho inválido (esperado %d bytes).", BACKING_STORE, PAGE_TABLE_SIZE * PAGE_SIZE);
    }
    FILE *bs = fopen(BACKING_STORE, "rb");
    if (!bs) {
        fail("não foi possível abrir %s.", BACKING_STORE);
    }

    FILE *out = fopen(OUTPUT_FILE, "w");
    if (!out) {
        fail("não foi possível criar %s.", OUTPUT_FILE);
    }

    for (int k = 0; k < count; k++) {
        int logical_address = addresses[k];
        int page_num = (logical_address >> 8) & 0xFF;
        int offset = logical_address & 0xFF;
        int frame_num = -1;
        int tlb_index = -1;

        total_addresses++;

#if USE_TLB
        tlb_index = search_tlb(page_num, &frame_num);
        if (tlb_index != -1) {
            tlb_hits++;
            tlb[tlb_index].last_used = global_time;
            /* o acesso também conta como uso da página (LRU de páginas) */
            page_table[page_num].last_used = global_time;
        } else {
#endif
            if (page_table[page_num].valid) {
                frame_num = page_table[page_num].frame_number;
                page_table[page_num].last_used = global_time;
            } else {
                frame_num = handle_page_fault(page_num, bs);
            }
#if USE_TLB
            tlb_index = update_tlb(page_num, frame_num);
        }
#endif

        global_time++;

        int physical_address = (frame_num << 8) | offset;
        signed char value = physical_memory[frame_num][offset];

        fprintf(out, "Virtual address: %d TLB: %d Physical address: %d Value: %d\n",
                logical_address, tlb_index, physical_address, value);
    }

    fprintf(out, "Number of Translated Addresses = %d\n", total_addresses);
    fprintf(out, "Page Faults = %d\n", page_faults);
    fprintf(out, "Page Fault Rate = %.3f\n", (float)page_faults / total_addresses);
    fprintf(out, "TLB Hits = %d\n", tlb_hits);
    fprintf(out, "TLB Hit Rate = %.3f\n", (float)tlb_hits / total_addresses);

    fclose(bs);
    if (fclose(out) != 0) {
        fail("erro ao gravar %s.", OUTPUT_FILE);
    }
    free(addresses);

    printf("Execução concluída. Verifique o arquivo '%s'.\n", OUTPUT_FILE);
    return EXIT_SUCCESS;
}