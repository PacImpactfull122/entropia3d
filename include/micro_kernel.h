#ifndef MICRO_KERNEL_H
#define MICRO_KERNEL_H

#include <stdint.h>
#include <stdbool.h>

// * contexto de coleta: executa coletor e calculador em ciclos configurados
typedef struct {
    uint32_t intervalo_ms;
    bool     ativo;
} ContextoColeta;

// * contexto de renderizacao: executa renderizador e gerenciador a taxa fixa
typedef struct {
    bool ativo;
} ContextoRenderizacao;

bool micro_kernel_inicializar(void);
void micro_kernel_executar(void);
void micro_kernel_encerrar(void);
void micro_kernel_passo_coleta(void);
void micro_kernel_passo_renderizacao(void);

#endif /* MICRO_KERNEL_H */
