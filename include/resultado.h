#ifndef RESULTADO_H
#define RESULTADO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "entrada_log.h"

typedef struct {
    uint32_t codigo;
    uint8_t  subsistema[32];
    uint64_t endereco_falha;
    bool     sucesso;
} ResultadoInicializacao;

// * determinado e falso quando o buffer tem amostras insuficientes
typedef struct {
    double            indice;
    CategoriaEntropia categoria;
    bool              determinado;
    uint64_t          timestamp_calculo;
} ResultadoEntropia;

typedef struct {
    size_t   bytes_escritos;
    uint32_t checksum;
    bool     sucesso;
    uint32_t codigo_erro;
} ResultadoSerializacao;

typedef struct {
    uint32_t celulas_restauradas;
    bool     sucesso;
    uint32_t codigo_erro;
} ResultadoDeserializacao;

#endif /* RESULTADO_H */
