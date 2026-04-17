#ifndef ENTRADA_LOG_H
#define ENTRADA_LOG_H

#include <stdint.h>

// * categorias de entropia definidas pelos limiares baixo, medio e alto
typedef enum {
    CATEGORIA_BAIXA        = 0,
    CATEGORIA_MEDIA        = 1,
    CATEGORIA_ALTA         = 2,
    CATEGORIA_INDETERMINADA = 3
} CategoriaEntropia;

typedef enum {
    NIVEL_INFO    = 0,
    NIVEL_AVISO   = 1,
    NIVEL_ERRO    = 2,
    NIVEL_CRITICO = 3
} NivelLog;

// * tamanho total da entrada calculado para alinhar em cache line
typedef struct {
    uint64_t timestamp;
    NivelLog nivel;
    uint8_t  subsistema[32];
    uint8_t  mensagem[192];
    uint32_t codigo_erro;
    uint64_t endereco_instrucao;
} EntradaLog;

#endif /* ENTRADA_LOG_H */
