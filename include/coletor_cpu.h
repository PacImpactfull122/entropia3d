#ifndef COLETOR_CPU_H
#define COLETOR_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include "resultado.h"

#define CAPACIDADE_BUFFER_AMOSTRAS 1000U

#define MAX_NUCLEOS 256U

typedef struct {
    uint64_t timestamp;
    uint64_t ciclos_clock;
    uint64_t instrucoes_retiradas;
    uint64_t cache_miss_l1;
    uint64_t cache_miss_l2;
    uint64_t branch_mispredictions;
    uint64_t ciclos_stall;
} EntradaAmostra;

// * buffer circular de amostras por nucleo, indice_escrita aponta para a proxima posicao livre
typedef struct {
    EntradaAmostra entradas[CAPACIDADE_BUFFER_AMOSTRAS];
    uint32_t       indice_escrita;
    uint32_t       contagem;
} BufferAmostras;

// * flags de features detectadas via cpuid leaf 1 ecx edx e leaf 7 ebx
typedef struct {
    bool sse;
    bool sse2;
    bool sse3;
    bool ssse3;
    bool sse41;
    bool sse42;
    bool avx;
    bool avx2;
    bool avx512f;
    bool aes;
    bool rdrand;
    bool rdseed;
    bool bmi1;
    bool bmi2;
    bool tsx;
} FeaturesISA;

// * topologia de cache por nivel, tamanho em bytes
typedef struct {
    uint32_t l1d;
    uint32_t l1i;
    uint32_t l2;
    uint32_t l3;
    uint8_t  assoc_l1d;
    uint8_t  assoc_l2;
    uint16_t assoc_l3;
} TopologiaCache;

// * detectado via string de fabricante do cpuid leaf 0
typedef enum {
    FAB_INTEL      = 0,
    FAB_AMD        = 1,
    FAB_DESCONHECIDO = 2
} FabricanteID;

// * geracoes zen identificadas via familia cpuid para selecao de msrs corretos
typedef enum {
    ZEN_DESCONHECIDO = 0,
    ZEN_1            = 1,  /* familia 0x17 modelo < 0x30 */
    ZEN_2            = 2,  /* familia 0x17 modelo >= 0x30 */
    ZEN_3            = 3,  /* familia 0x19 modelo < 0x50 */
    ZEN_4            = 4   /* familia 0x19 modelo >= 0x50 */
} GeracaoZen;

typedef struct {
    char          fabricante[13];
    char          marca[49];
    uint32_t      modelo;
    uint8_t       familia;
    uint8_t       stepping;
    uint8_t       numero_nucleos_fisicos;
    uint8_t       numero_nucleos_logicos;
    uint8_t       contadores_disponiveis;
    uint16_t      freq_base_mhz;
    uint16_t      freq_boost_mhz;
    uint16_t      freq_barramento_mhz;
    TopologiaCache cache;
    FeaturesISA   features;
    bool          modo_degradado;
    FabricanteID  fabricante_id;
    GeracaoZen    geracao_zen;
} InfoCPU;

typedef struct {
    bool ativo;
} EstadoMSR;

ResultadoInicializacao inicializar(void);
void                   coletar_amostras(void);
BufferAmostras        *obter_buffer(uint8_t nucleo);
InfoCPU                obter_info_cpu(void);

#ifdef TESTE_INJECAO_FALHA
// * funcoes exclusivas para testes, nao disponiveis em compilacao de producao
void coletor_cpu_injetar_falha_msr(uint8_t nucleo, uint8_t indice_msr);
bool coletor_cpu_obter_msr_ativo(uint8_t nucleo, uint8_t indice_msr);
void coletor_cpu_resetar_msrs(void);
#endif /* TESTE_INJECAO_FALHA */

#endif /* COLETOR_CPU_H */
