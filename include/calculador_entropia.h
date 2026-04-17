#ifndef CALCULADOR_ENTROPIA_H
#define CALCULADOR_ENTROPIA_H

#include <stdint.h>
#include "resultado.h"
#include "entrada_log.h"

// * regiao de execucao identificada pelo par processo e thread
typedef struct {
    uint32_t id_processo;
    uint32_t id_thread;
} RegiaoExecucao;

// * indice de entropia normalizado entre zero e um
typedef double IndiceEntropia;

ResultadoEntropia recalcular(RegiaoExecucao regiao);
IndiceEntropia    consultar(RegiaoExecucao regiao);
CategoriaEntropia classificar(double indice);
void              calculador_entropia_resetar_cache(void);

#endif /* CALCULADOR_ENTROPIA_H */
