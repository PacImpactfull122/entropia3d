#ifndef BUFFER_LOG_H
#define BUFFER_LOG_H

#include <stddef.h>
#include <stdint.h>
#include "entrada_log.h"

#define CAPACIDADE_BUFFER_LOG 4096U

void   registrar(NivelLog nivel, const char *subsistema, const char *mensagem);
void   registrar_erro(const char *subsistema, uint32_t codigo, uint64_t endereco);
size_t obter_entradas(EntradaLog *destino, size_t max);

// * exposta apenas para uso em testes, nao deve ser chamada em producao
void   buffer_log_resetar(void);

#endif /* BUFFER_LOG_H */
