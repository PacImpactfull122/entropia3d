#include "coletor_cpu.h"
#include "cpuid_asm.h"
#include "buffer_log.h"
#include "codigos_erro.h"

#include <string.h>
#include <stddef.h>
#include <stdio.h>

static InfoCPU        s_info_cpu;
static BufferAmostras s_buffers[MAX_NUCLEOS];
static bool           s_inicializado = false;

#define TOTAL_MSR_POR_NUCLEO 6U
static bool s_msr_ativo[MAX_NUCLEOS][TOTAL_MSR_POR_NUCLEO];

// * executa cpuid em assembly inline puro, sem dependencia de cpuid.h
static void cpuid_asm(uint32_t folha, uint32_t sub,
                      uint32_t *eax, uint32_t *ebx,
                      uint32_t *ecx, uint32_t *edx)
{
    asm_cpuid(folha, sub, eax, ebx, ecx, edx);
}

// * retorna o max leaf suportado e preenche a string de fabricante
static uint32_t obter_fabricante(char destino[13])
{
    asm_cpuid_fabricante(destino);
    return asm_cpuid_max_leaf(0x00U);
}

// * leaf 0x80000002 a 0x80000004 formam a brand string
static void obter_marca(char destino[49], uint32_t max_ext)
{
    memset(destino, 0, 49);
    if (max_ext < 0x80000004U) return;
    asm_cpuid_marca_leaf(0x80000002U, destino + 0);
    asm_cpuid_marca_leaf(0x80000003U, destino + 16);
    asm_cpuid_marca_leaf(0x80000004U, destino + 32);
    destino[48] = '\0';
}

static void obter_modelo_familia_stepping_logicos(uint32_t *modelo, uint8_t *familia,
                                                   uint8_t *stepping, uint8_t *nucleos_logicos)
{
    uint32_t eax, ebx, ecx, edx;
    asm_cpuid_leaf1(&eax, &ebx, &ecx, &edx);
    *stepping        = (uint8_t)((eax >> 0) & 0x0FU);
    uint8_t mod_base = (uint8_t)((eax >> 4) & 0x0FU);
    uint8_t mod_ext  = (uint8_t)((eax >> 16) & 0x0FU);
    uint8_t fam_base = (uint8_t)((eax >> 8) & 0x0FU);
    uint8_t fam_ext  = (uint8_t)((eax >> 20) & 0xFFU);
    *modelo  = (fam_base == 6U || fam_base == 15U)
               ? (uint32_t)((mod_ext << 4) | mod_base)
               : (uint32_t)mod_base;
    *familia = (fam_base == 15U) ? (uint8_t)(fam_ext + fam_base) : fam_base;
    *nucleos_logicos = (uint8_t)((ebx >> 16) & 0xFFU);
}

// * preenche features via leaf 1 ecx edx e leaf 7 sub 0 ebx ecx
static void obter_features(FeaturesISA *f, uint32_t max_leaf)
{
    uint32_t eax, ebx, ecx, edx;
    memset(f, 0, sizeof(*f));

    asm_cpuid_leaf1(&eax, &ebx, &ecx, &edx);
    f->sse   = (edx >> 25) & 1U;
    f->sse2  = (edx >> 26) & 1U;
    f->sse3  = (ecx >> 0)  & 1U;
    f->ssse3 = (ecx >> 9)  & 1U;
    f->sse41 = (ecx >> 19) & 1U;
    f->sse42 = (ecx >> 20) & 1U;
    f->aes   = (ecx >> 25) & 1U;
    f->avx   = (ecx >> 28) & 1U;
    f->rdrand = (ecx >> 30) & 1U;

    if (max_leaf < 0x07U) return;
    asm_cpuid_leaf7(&eax, &ebx, &ecx, &edx);
    f->bmi1    = (ebx >> 3)  & 1U;
    f->avx2    = (ebx >> 5)  & 1U;
    f->tsx     = (ebx >> 4)  & 1U;
    f->bmi2    = (ebx >> 8)  & 1U;
    f->avx512f = (ebx >> 16) & 1U;
    f->rdseed  = (ebx >> 18) & 1U;
}

// * leaf 4 itera sub-leaves para mapear cache l1d, l1i, l2, l3
static void obter_cache(TopologiaCache *tc, uint32_t max_leaf)
{
    uint32_t eax, ebx, ecx, edx;
    memset(tc, 0, sizeof(*tc));
    if (max_leaf < 0x04U) return;

    for (uint32_t sub = 0; sub < 8U; sub++) {
        asm_cpuid_leaf4(sub, &eax, &ebx, &ecx, &edx);
        uint8_t tipo  = (uint8_t)(eax & 0x1FU);
        uint8_t nivel = (uint8_t)((eax >> 5) & 0x07U);
        if (tipo == 0) break;

        uint32_t ways       = ((ebx >> 22) & 0x3FFU) + 1U;
        uint32_t partitions = ((ebx >> 12) & 0x3FFU) + 1U;
        uint32_t line_size  = (ebx & 0xFFFU) + 1U;
        uint32_t sets       = ecx + 1U;
        uint32_t tamanho    = ways * partitions * line_size * sets;

        if (nivel == 1 && tipo == 1) { tc->l1d = tamanho; tc->assoc_l1d = (uint8_t)ways; }
        if (nivel == 1 && tipo == 2) { tc->l1i = tamanho; }
        if (nivel == 2)              { tc->l2  = tamanho; tc->assoc_l2  = (uint8_t)ways; }
        if (nivel == 3)              { tc->l3  = tamanho; tc->assoc_l3  = (uint16_t)ways; }
    }
}

// * leaf 0x16 disponivel em skylake e posteriores, retorna frequencias em mhz
static void obter_frequencias(uint16_t *base, uint16_t *boost, uint16_t *barramento,
                               uint32_t max_leaf)
{
    *base = *boost = *barramento = 0;
    if (max_leaf < 0x16U) return;
    uint32_t eax, ebx, ecx;
    asm_cpuid_leaf16(&eax, &ebx, &ecx);
    *base       = (uint16_t)(eax & 0xFFFFU);
    *boost      = (uint16_t)(ebx & 0xFFFFU);
    *barramento = (uint16_t)(ecx & 0xFFFFU);
}

// * leaf 0x80000008 fornece nucleos fisicos em amd e intel modernos
static uint8_t obter_nucleos_fisicos(uint32_t max_ext)
{
    uint32_t eax, ebx, ecx, edx;
    asm_cpuid_leaf4(0, &eax, &ebx, &ecx, &edx);
    uint8_t via_leaf4 = (uint8_t)(((eax >> 26) & 0x3FU) + 1U);

    uint8_t via_ext = 0;
    if (max_ext >= 0x80000008U) {
        uint32_t ecx_ext = 0;
        asm_cpuid_ext8(&ecx_ext);
        via_ext = (uint8_t)((ecx_ext & 0xFFU) + 1U);
    }

    if (via_ext > 0) return via_ext;
    return via_leaf4 > 0 ? via_leaf4 : 1;
}

static uint8_t obter_contadores_pmu(uint32_t max_leaf)
{
    if (max_leaf < 0x0AU) return 0;
    uint32_t eax = 0;
    asm_cpuid_leaf_a(&eax);
    return (uint8_t)((eax >> 8) & 0xFFU);
}

ResultadoInicializacao inicializar(void)
{
    ResultadoInicializacao resultado;
    memset(&resultado, 0, sizeof(resultado));
    memcpy(resultado.subsistema, "ColetorCPU", 10);

    memset(&s_info_cpu, 0, sizeof(s_info_cpu));
    memset(s_buffers, 0, sizeof(s_buffers));

    // * todos os msrs comecam ativos, falhas individuais os desativam permanentemente
    for (uint32_t n = 0; n < MAX_NUCLEOS; n++) {
        for (uint32_t m = 0; m < TOTAL_MSR_POR_NUCLEO; m++) {
            s_msr_ativo[n][m] = true;
        }
    }

    // * max leaf estendido necessario para brand string e nucleos via 0x80000008
    uint32_t eax_ext, ebx_ext, ecx_ext, edx_ext;
    cpuid_asm(0x80000000U, 0, &eax_ext, &ebx_ext, &ecx_ext, &edx_ext);
    uint32_t max_ext = eax_ext;

    uint32_t max_leaf = obter_fabricante(s_info_cpu.fabricante);

    obter_marca(s_info_cpu.marca, max_ext);
    obter_modelo_familia_stepping_logicos(
        &s_info_cpu.modelo,
        &s_info_cpu.familia,
        &s_info_cpu.stepping,
        &s_info_cpu.numero_nucleos_logicos
    );
    s_info_cpu.numero_nucleos_fisicos = obter_nucleos_fisicos(max_ext);
    obter_cache(&s_info_cpu.cache, max_leaf);
    obter_frequencias(&s_info_cpu.freq_base_mhz, &s_info_cpu.freq_boost_mhz,
                      &s_info_cpu.freq_barramento_mhz, max_leaf);
    obter_features(&s_info_cpu.features, max_leaf);

    uint8_t contadores = obter_contadores_pmu(max_leaf);

    // ! eax zero em leaf dez indica que a instrucao nao e suportada neste modelo
    if (contadores == 0) {
        uint32_t eax_raw, ebx_raw, ecx_raw, edx_raw;
        cpuid_asm(0x0A, 0, &eax_raw, &ebx_raw, &ecx_raw, &edx_raw);

        if (eax_raw == 0) {
            registrar_erro("ColetorCPU", E_CPUID_FALHOU, 0);
            resultado.codigo  = E_CPUID_FALHOU;
            resultado.sucesso = false;
            return resultado;
        }

        // * contadores zero com eax diferente de zero significa cpu sem contadores programaveis
        s_info_cpu.contadores_disponiveis = 0;
        s_info_cpu.modo_degradado         = true;
        registrar_erro("ColetorCPU", E_MODO_DEGRADADO, 0);
    } else {
        s_info_cpu.contadores_disponiveis = contadores;
        s_info_cpu.modo_degradado         = false;
    }

    // * registra resumo da inicializacao no buffer de log
    char mensagem[256];
    snprintf(mensagem, sizeof(mensagem),
        "fabricante=%s marca=%.20s familia=%u modelo=%u stepping=%u "
        "fisicos=%u logicos=%u contadores=%u freq_base=%umhz degradado=%d",
        s_info_cpu.fabricante,
        s_info_cpu.marca,
        (unsigned)s_info_cpu.familia,
        s_info_cpu.modelo,
        (unsigned)s_info_cpu.stepping,
        (unsigned)s_info_cpu.numero_nucleos_fisicos,
        (unsigned)s_info_cpu.numero_nucleos_logicos,
        (unsigned)s_info_cpu.contadores_disponiveis,
        (unsigned)s_info_cpu.freq_base_mhz,
        (int)s_info_cpu.modo_degradado
    );
    registrar(NIVEL_INFO, "ColetorCPU", mensagem);

    s_inicializado    = true;
    resultado.sucesso = true;
    return resultado;
}

// * le um msr pelo endereco fornecido e armazena o valor em valor_saida
// * em modo privilegiado usa rdmsr, em modo de teste retorna um contador simulado
static bool ler_msr(uint32_t endereco, uint64_t *valor_saida)
{
#ifdef RING0_REAL
    uint32_t baixo, alto;
    // ! rdmsr gera gp fault se o msr nao existir neste modelo de cpu
    __asm__ volatile (
        "rdmsr"
        : "=a"(baixo), "=d"(alto)
        : "c"(endereco)
    );
    *valor_saida = ((uint64_t)alto << 32) | (uint64_t)baixo;
    return true;
#else
    // * simulacao para compilacao e testes em userspace
    static uint64_t s_contador_simulado = 0;
    (void)endereco;
    *valor_saida = ++s_contador_simulado;
    return true;
#endif
}

static uint64_t ler_rdtsc(void)
{
    return asm_rdtsc();
}

void coletar_amostras(void)
{
    if (!s_inicializado) {
        return;
    }

    uint8_t total_nucleos = s_info_cpu.numero_nucleos_fisicos;

    for (uint8_t nucleo = 0; nucleo < total_nucleos; nucleo++) {
        EntradaAmostra amostra;
        amostra.timestamp = ler_rdtsc();

        // * msr ia32_fixed_ctr0 conta instrucoes retiradas
        if (s_msr_ativo[nucleo][0]) {
            if (!ler_msr(0x309, &amostra.instrucoes_retiradas)) {
                s_msr_ativo[nucleo][0] = false;
                registrar_erro("ColetorCPU", E_MSR_INDISPONIVEL, 0x309);
                amostra.instrucoes_retiradas = 0;
            }
        } else {
            amostra.instrucoes_retiradas = 0;
        }

        // * ciclos_clock usa o mesmo tsc lido no timestamp como aproximacao de ciclos
        amostra.ciclos_clock = amostra.timestamp;

        // * msr ia32_pmc0 configurado para cache miss l1
        if (s_msr_ativo[nucleo][1]) {
            if (!ler_msr(0xC1, &amostra.cache_miss_l1)) {
                s_msr_ativo[nucleo][1] = false;
                registrar_erro("ColetorCPU", E_MSR_INDISPONIVEL, 0xC1);
                amostra.cache_miss_l1 = 0;
            }
        } else {
            amostra.cache_miss_l1 = 0;
        }

        // * msr ia32_pmc1 configurado para cache miss l2
        if (s_msr_ativo[nucleo][2]) {
            if (!ler_msr(0xC2, &amostra.cache_miss_l2)) {
                s_msr_ativo[nucleo][2] = false;
                registrar_erro("ColetorCPU", E_MSR_INDISPONIVEL, 0xC2);
                amostra.cache_miss_l2 = 0;
            }
        } else {
            amostra.cache_miss_l2 = 0;
        }

        // * msr ia32_pmc2 configurado para branch mispredictions
        if (s_msr_ativo[nucleo][3]) {
            if (!ler_msr(0xC3, &amostra.branch_mispredictions)) {
                s_msr_ativo[nucleo][3] = false;
                registrar_erro("ColetorCPU", E_MSR_INDISPONIVEL, 0xC3);
                amostra.branch_mispredictions = 0;
            }
        } else {
            amostra.branch_mispredictions = 0;
        }

        // * msr ia32_fixed_ctr1 conta ciclos de stall do pipeline
        if (s_msr_ativo[nucleo][4]) {
            if (!ler_msr(0x30A, &amostra.ciclos_stall)) {
                s_msr_ativo[nucleo][4] = false;
                registrar_erro("ColetorCPU", E_MSR_INDISPONIVEL, 0x30A);
                amostra.ciclos_stall = 0;
            }
        } else {
            amostra.ciclos_stall = 0;
        }

        // * em modo degradado apenas contadores fixos sao usados, registrar aviso uma vez
        if (s_info_cpu.modo_degradado) {
            registrar(NIVEL_AVISO, "ColetorCPU", "coleta em modo degradado, contadores programaveis indisponiveis");
        }

        // * insere amostra no buffer circular do nucleo
        BufferAmostras *buf = &s_buffers[nucleo];
        buf->entradas[buf->indice_escrita] = amostra;
        buf->indice_escrita = (buf->indice_escrita + 1) % CAPACIDADE_BUFFER_AMOSTRAS;
        if (buf->contagem < CAPACIDADE_BUFFER_AMOSTRAS) {
            buf->contagem++;
        }
    }
}

BufferAmostras *obter_buffer(uint8_t nucleo)
{
    return &s_buffers[nucleo];
}

InfoCPU obter_info_cpu(void)
{
    return s_info_cpu;
}

#ifdef TESTE_INJECAO_FALHA

// * marca o msr indicado como inativo para o nucleo especificado
void coletor_cpu_injetar_falha_msr(uint8_t nucleo, uint8_t indice_msr)
{
    if (indice_msr < TOTAL_MSR_POR_NUCLEO) {
        s_msr_ativo[nucleo][indice_msr] = false;
    }
}

bool coletor_cpu_obter_msr_ativo(uint8_t nucleo, uint8_t indice_msr)
{
    if (indice_msr < TOTAL_MSR_POR_NUCLEO) {
        return s_msr_ativo[nucleo][indice_msr];
    }
    return false;
}

// * restaura todos os msrs para o estado ativo, usado entre iteracoes de teste
void coletor_cpu_resetar_msrs(void)
{
    for (uint32_t n = 0; n < MAX_NUCLEOS; n++) {
        for (uint32_t m = 0; m < TOTAL_MSR_POR_NUCLEO; m++) {
            s_msr_ativo[n][m] = true;
        }
    }
}

#endif /* TESTE_INJECAO_FALHA */
