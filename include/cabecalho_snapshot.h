#ifndef CABECALHO_SNAPSHOT_H
#define CABECALHO_SNAPSHOT_H

#include <stdint.h>

// ! valor magico identifica o formato do arquivo, rejeitar snapshot se nao coincidir
#define MAGICO_SNAPSHOT ((uint32_t)0x4D4B453D)

typedef struct {
    uint32_t magico;
    uint16_t versao_formato;
    uint32_t contagem_celulas;
    uint64_t timestamp_criacao;
    uint32_t checksum_crc32;
    uint32_t tamanho_total;
    uint8_t  reservado[16];
} CabecalhoSnapshot;

#endif /* CABECALHO_SNAPSHOT_H */
