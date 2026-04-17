#include "calculador_entropia.h"
#include "coletor_cpu.h"
#include "buffer_log.h"
#include "codigos_erro.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#define NUM_BINS 16U

// * log2 de num_bins e usado para normalizar a entropia de cada metrica
#define LOG2_NUM_BINS 4.0

#define MIN_AMOSTRAS_DETERMINADAS 10U

// * numero de metricas por amostra, excluindo o timestamp
#define NUM_METRICAS 6U

// * cache de resultados por nucleo, indexado pelo indice do nucleo fisico
static ResultadoEntropia cache_resultados[MAX_NUCLEOS];

// * controla quais entradas do cache ja foram preenchidas ao menos uma vez
static int cache_valido[MAX_NUCLEOS];

static double calcular_entropia_normalizada(const uint32_t frequencias[NUM_BINS], uint32_t total) {
    if (total == 0U) {
        return 0.0;
    }

    uint32_t bins_distintos = 0U;
    for (uint32_t i = 0U; i < NUM_BINS; i++) {
        if (frequencias[i] > 0U) {
            bins_distintos++;
        }
    }

    // * distribuicao deterministica ou com apenas um bin ocupado
    if (bins_distintos <= 1U) {
        return 0.0;
    }

    double entropia = 0.0;
    for (uint32_t i = 0U; i < NUM_BINS; i++) {
        if (frequencias[i] == 0U) {
            continue;
        }
        double p = (double)frequencias[i] / (double)total;
        entropia -= p * log2(p);
    }

    return entropia / LOG2_NUM_BINS;
}

static double entropia_de_metrica(const EntradaAmostra *entradas, uint32_t contagem, uint32_t offset_campo) {
    if (contagem == 0U) {
        return 0.0;
    }

    // * determina o range dos valores para definir o tamanho de cada bin
    uint64_t valor_min = UINT64_MAX;
    uint64_t valor_max = 0U;

    for (uint32_t i = 0U; i < contagem; i++) {
        uint64_t valor;
        // * acessa o campo correto da amostra via offset em bytes
        memcpy(&valor, (const uint8_t *)&entradas[i] + offset_campo, sizeof(uint64_t));
        if (valor < valor_min) {
            valor_min = valor;
        }
        if (valor > valor_max) {
            valor_max = valor;
        }
    }

    uint32_t frequencias[NUM_BINS];
    memset(frequencias, 0, sizeof(frequencias));

    uint64_t range = valor_max - valor_min;

    for (uint32_t i = 0U; i < contagem; i++) {
        uint64_t valor;
        memcpy(&valor, (const uint8_t *)&entradas[i] + offset_campo, sizeof(uint64_t));

        uint32_t bin;
        if (range == 0U) {
            // * todos os valores iguais, cai no bin zero
            bin = 0U;
        } else {
            uint64_t pos = (valor - valor_min) * (uint64_t)(NUM_BINS - 1U);
            bin = (uint32_t)(pos / range);
            if (bin >= NUM_BINS) {
                bin = NUM_BINS - 1U;
            }
        }
        frequencias[bin]++;
    }

    return calcular_entropia_normalizada(frequencias, contagem);
}

ResultadoEntropia recalcular(RegiaoExecucao regiao) {
    ResultadoEntropia resultado;
    memset(&resultado, 0, sizeof(resultado));
    resultado.determinado       = 0;
    resultado.categoria         = CATEGORIA_INDETERMINADA;
    resultado.indice            = 0.0;
    resultado.timestamp_calculo = 0U;

    InfoCPU info = obter_info_cpu();
    uint8_t numero_nucleos = info.numero_nucleos_fisicos;
    if (numero_nucleos == 0U) {
        numero_nucleos = 1U;
    }

    uint8_t nucleo = (uint8_t)((regiao.id_processo + regiao.id_thread) % (uint32_t)numero_nucleos);

    BufferAmostras *buf = obter_buffer(nucleo);
    if (buf == NULL) {
        registrar_erro("calculador_entropia", E_SUBSISTEMA_FALHOU, 0U);
        return resultado;
    }

    if (buf->contagem < MIN_AMOSTRAS_DETERMINADAS) {
        cache_resultados[nucleo] = resultado;
        cache_valido[nucleo]     = 1;
        return resultado;
    }

    // * offsets dos seis campos de metrica dentro de EntradaAmostra
    // * timestamp e ignorado, apenas os seis contadores sao usados
    static const uint32_t offsets_metricas[NUM_METRICAS] = {
        (uint32_t)offsetof(EntradaAmostra, ciclos_clock),
        (uint32_t)offsetof(EntradaAmostra, instrucoes_retiradas),
        (uint32_t)offsetof(EntradaAmostra, cache_miss_l1),
        (uint32_t)offsetof(EntradaAmostra, cache_miss_l2),
        (uint32_t)offsetof(EntradaAmostra, branch_mispredictions),
        (uint32_t)offsetof(EntradaAmostra, ciclos_stall),
    };

    uint32_t contagem = buf->contagem;
    if (contagem > CAPACIDADE_BUFFER_AMOSTRAS) {
        contagem = CAPACIDADE_BUFFER_AMOSTRAS;
    }

    double soma_entropias = 0.0;
    for (uint32_t m = 0U; m < NUM_METRICAS; m++) {
        soma_entropias += entropia_de_metrica(buf->entradas, contagem, offsets_metricas[m]);
    }

    double indice = soma_entropias / (double)NUM_METRICAS;

    // * garante que o indice permanece no intervalo fechado entre zero e um
    if (indice < 0.0) {
        indice = 0.0;
    }
    if (indice > 1.0) {
        indice = 1.0;
    }

    resultado.indice            = indice;
    resultado.determinado       = 1;
    resultado.categoria         = classificar(indice);
    resultado.timestamp_calculo = 0U;

    cache_resultados[nucleo] = resultado;
    cache_valido[nucleo]     = 1;

    return resultado;
}

IndiceEntropia consultar(RegiaoExecucao regiao) {
    InfoCPU info = obter_info_cpu();
    uint8_t numero_nucleos = info.numero_nucleos_fisicos;
    if (numero_nucleos == 0U) {
        numero_nucleos = 1U;
    }

    uint8_t nucleo = (uint8_t)((regiao.id_processo + regiao.id_thread) % (uint32_t)numero_nucleos);

    if (cache_valido[nucleo]) {
        return cache_resultados[nucleo].indice;
    }

    // * sem cache previo, calcula agora e armazena
    ResultadoEntropia resultado = recalcular(regiao);
    return resultado.indice;
}

CategoriaEntropia classificar(double indice) {
    if (indice < 0.33) {
        return CATEGORIA_BAIXA;
    }
    if (indice <= 0.66) {
        return CATEGORIA_MEDIA;
    }
    return CATEGORIA_ALTA;
}

void calculador_entropia_resetar_cache(void) {
    // * zera o cache para permitir testes isolados sem estado residual
    for (uint32_t i = 0U; i < MAX_NUCLEOS; i++) {
        cache_valido[i] = 0;
    }
}
