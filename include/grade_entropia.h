#ifndef GRADE_ENTROPIA_H
#define GRADE_ENTROPIA_H

#include <stdint.h>
#include <stdbool.h>
#include "tipos.h"
#include "entrada_log.h"

// * limite fixo de celulas para alocacao estatica em modo privilegiado, sem malloc disponivel
#define MAX_CELULAS ((uint32_t)4096)

// * historico por celula para janela de tempo longa
#define TAMANHO_HISTORICO_ENTROPIA 3600

#define NUM_CONTADORES_VARIANCIA 6

typedef struct {
    uint8_t nome[32];
    double  variancia;
} ContadorVariancia;

typedef struct {
    Vec3f            coordenada;
    double           indice_entropia;
    CategoriaEntropia categoria;
    uint32_t         id_processo;
    uint32_t         id_thread;
    uint64_t         timestamp_atualizacao;
    double           historico_entropia[TAMANHO_HISTORICO_ENTROPIA];
    ContadorVariancia contadores_variancia[NUM_CONTADORES_VARIANCIA];
    bool             ativa;
} CelulaEntropia;

// * versao e incrementada atomicamente a cada atualizacao para acesso sem bloqueio entre contextos
typedef struct {
    CelulaEntropia celulas[MAX_CELULAS];
    uint32_t       contagem_ativa;
    uint64_t       versao;
} GradeEntropia;

#endif /* GRADE_ENTROPIA_H */
