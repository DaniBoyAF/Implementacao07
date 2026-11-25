# Implementacao07 - Projetando uma Memória Virtual

Gerenciador de memória virtual em C (baseado em *Designing a Virtual Memory
Manager*, Silberschatz et al., 10ª ed.): traduz endereços lógicos de 16 bits em
endereços físicos usando TLB e tabela de páginas, com **128 frames** de memória
física, substituição de página **FIFO** ou **LRU** e substituição da TLB
**FIFO** ou **LRU**. A busca na TLB (16 entradas) usa **uma thread por entrada**.

## Sistema operacional
Desenvolvido para Linux e testado em Ubuntu (WSL) com gcc. Deve funcionar em
qualquer Linux, Unix ou macOS com `gcc`, `make` e `python3` (este último só para os testes).

## Arquivos
- `vm.c`: o programa. Tabela de páginas (256 entradas), TLB (16 entradas),
  tratamento de page fault com substituição FIFO/LRU, busca na TLB com threads,
  validação de argumentos e do arquivo de endereços, e geração do `correct.txt`.
- `Makefile`: `make` gera o executável `vm`; `make clean` remove os arquivos gerados;
  `make test` roda todos os testes.
- `testar.sh`: roda todos os cenários de teste (algoritmos, etapas, erros, repetição e ThreadSanitizer).
- `verificar.py`: verificador independente da saída (compara com uma simulação de referência).
- `gerar_enderecos.py`: gera arquivos de endereços de teste.
- `evidencias.log`: log da sessão de testes (gravado com `script`).

Arquivos do professor (não incluídos): `address.txt` e `BACKING_STORE.bin`.
O `BACKING_STORE.bin` precisa estar na pasta em que o `vm` é executado.

## Como compilar
    make

## Como executar
    ./vm <arquivo_enderecos> <algoritmo_pagina> <algoritmo_tlb>

Exemplo: `./vm address.txt lru fifo` usa LRU na substituição de página e FIFO na TLB.
O resultado é gravado em `correct.txt` (um registro por endereço e, ao final, as
estatísticas: page faults, taxa de page faults, TLB hits e taxa de TLB hits).

O programa encerra com mensagem de erro (código 1) quando: o número de argumentos
não é 3; o algoritmo não é `fifo` nem `lru`; o arquivo de endereços não existe, não é
um arquivo regular, está vazio ou tem formato diferente do esperado (um inteiro de 0 a
65535 por linha); ou o `BACKING_STORE.bin` está ausente ou com tamanho inválido.

## Como testar
    make test                 # gera 3000 endereços de teste e roda tudo
    ./testar.sh address.txt   # ou usando o arquivo de endereços do professor

O `testar.sh` executa: as 4 combinações de algoritmos (página x TLB) conferindo cada
linha da saída com o `verificar.py`; as etapas do desenvolvimento por macros de
compilação (sem TLB com 256 frames, sem TLB com 128 frames FIFO e LRU, TLB sem thread);
os casos de erro; 100 execuções seguidas; e o ThreadSanitizer.

Para compilar uma etapa manualmente:

    make clean && make EXTRA="-DUSE_TLB=0 -DNUM_FRAMES=256"   # sem TLB, 256 frames
    make clean && make EXTRA="-DUSE_THREADS=0"                # TLB sem threads
    make clean && make                                        # versão final

## Como limpar
    make clean