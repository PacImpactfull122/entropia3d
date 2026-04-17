#ifndef CODIGOS_ERRO_H
#define CODIGOS_ERRO_H

#include <stdint.h>

#define E_FRAMEBUFFER_INDISPONIVEL ((uint32_t)0x0001)
#define E_MSR_INDISPONIVEL         ((uint32_t)0x0002)
#define E_SNAPSHOT_CORROMPIDO      ((uint32_t)0x0003)
#define E_BUFFER_OVERFLOW          ((uint32_t)0x0004)
#define E_CPUID_FALHOU             ((uint32_t)0x0005)
#define E_MODO_DEGRADADO           ((uint32_t)0x0006)
#define E_SUBSISTEMA_FALHOU        ((uint32_t)0x0007)

#endif /* CODIGOS_ERRO_H */
