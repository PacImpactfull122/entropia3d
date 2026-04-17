#include "micro_kernel.h"
#include "cpuid_asm.h"
#include "buffer_log.h"
#include "coletor_cpu.h"
#include "calculador_entropia.h"
#include "renderizador_3d.h"
#include "gerenciador_interacao.h"
#include "grade_entropia.h"
#include "resultado.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// * frequencia tsc em hz estimada na inicializacao via calibracao com rdtsc
static uint64_t ciclos_por_segundo = 0;

// * grade compartilhada entre contexto de coleta e contexto de renderizacao
static GradeEntropia grade_compartilhada;

// * estado da camera usado pelo contexto de renderizacao
static EstadoCamera estado_camera;

// * contextos de execucao do micro kernel
static ContextoColeta     contexto_coleta;
static ContextoRenderizacao contexto_renderizacao;

// * flag de controle do loop principal
static bool executando = false;

static inline uint64_t ler_tsc(void) {
    return asm_rdtsc();
}

static void calibrar_tsc(void) {
    // * laco de espera simples para estimar frequencia sem acesso a timer externo
    uint64_t inicio = ler_tsc();
    volatile uint64_t contador = 0;
    while (contador < 10000000ULL) {
        contador++;
    }
    uint64_t fim = ler_tsc();
    uint64_t delta = fim - inicio;
    // * assume que o laco levou aproximadamente dez milissegundos
    ciclos_por_segundo = delta * 100ULL;
}

static inline uint64_t ms_para_ciclos(uint32_t ms) {
    return (ciclos_por_segundo / 1000ULL) * (uint64_t)ms;
}

static inline void grade_iniciar_escrita(GradeEntropia *grade) {
    __sync_fetch_and_add(&grade->versao, 1ULL);
}

static inline void grade_concluir_escrita(GradeEntropia *grade) {
    __sync_fetch_and_add(&grade->versao, 1ULL);
}

static inline void grade_aguardar_leitura_segura(const GradeEntropia *grade) {
    while (grade->versao & 1ULL) {
        __asm__ volatile ("pause");
    }
}

static void ciclo_coleta(void) {
    uint64_t tsc_inicio_ciclo = ler_tsc();

    // * coleta amostras de todos os nucleos via rdmsr
    coletar_amostras();

    // * recalcula entropia imediatamente apos coleta para manter latencia baixa
    InfoCPU info = obter_info_cpu();
    uint8_t num_nucleos = info.numero_nucleos_fisicos;
    if (num_nucleos == 0) {
        num_nucleos = 1;
    }

    // * static para evitar stack overflow, historico por celula e grande
    static CelulaEntropia celulas_novas[MAX_CELULAS];
    uint32_t contagem_nova = 0;

    for (uint8_t n = 0; n < num_nucleos; n++) {
        BufferAmostras *buf = obter_buffer(n);
        if (buf == NULL || buf->contagem == 0) {
            continue;
        }

        RegiaoExecucao regiao;
        regiao.id_processo = (uint32_t)n;
        regiao.id_thread   = 0;

        ResultadoEntropia resultado = recalcular(regiao);

        if (!resultado.determinado) {
            continue;
        }

        if (contagem_nova >= MAX_CELULAS) {
            break;
        }

        CelulaEntropia *celula = &celulas_novas[contagem_nova];
        memset(celula, 0, sizeof(CelulaEntropia));
        celula->coordenada.x          = (float)n;
        celula->coordenada.y          = 0.0f;
        celula->coordenada.z          = 0.0f;
        celula->indice_entropia       = resultado.indice;
        celula->categoria             = resultado.categoria;
        celula->id_processo           = regiao.id_processo;
        celula->id_thread             = regiao.id_thread;
        celula->timestamp_atualizacao = resultado.timestamp_calculo;
        celula->ativa                 = true;
        contagem_nova++;
    }

    // * atualiza grade compartilhada com lock-free via campo versao
    grade_iniciar_escrita(&grade_compartilhada);
    for (uint32_t i = 0; i < contagem_nova; i++) {
        grade_compartilhada.celulas[i] = celulas_novas[i];
    }
    grade_compartilhada.contagem_ativa = contagem_nova;
    grade_concluir_escrita(&grade_compartilhada);

    // * exporta dados de entropia para o visualizador via arquivo json
    {
        static uint8_t contador_export = 0;
        contador_export++;
        if (contador_export >= 1) {
            contador_export = 0;
            FILE *f = fopen("/tmp/entropia_data.json", "w");
            if (f) {
                InfoCPU inf = obter_info_cpu();
                uint8_t num_nucleos = inf.numero_nucleos_fisicos;
                if (num_nucleos == 0) num_nucleos = 1;

                // * cabecalho com metadados completos da cpu para o visualizador
                fprintf(f,
                    "{"
                    "\"nucleos\":%u,"
                    "\"nucleos_logicos\":%u,"
                    "\"fabricante\":\"%s\","
                    "\"marca\":\"%s\","
                    "\"familia\":%u,"
                    "\"modelo\":%u,"
                    "\"stepping\":%u,"
                    "\"freq_base\":%u,"
                    "\"freq_boost\":%u,"
                    "\"freq_barramento\":%u,"
                    "\"cache_l1d\":%u,"
                    "\"cache_l1i\":%u,"
                    "\"cache_l2\":%u,"
                    "\"cache_l3\":%u,"
                    "\"assoc_l1d\":%u,"
                    "\"assoc_l2\":%u,"
                    "\"assoc_l3\":%u,"
                    "\"contadores\":%u,"
                    "\"sse\":%d,\"sse2\":%d,\"sse3\":%d,\"ssse3\":%d,"
                    "\"sse41\":%d,\"sse42\":%d,\"avx\":%d,\"avx2\":%d,"
                    "\"avx512f\":%d,\"aes\":%d,\"rdrand\":%d,\"rdseed\":%d,"
                    "\"bmi1\":%d,\"bmi2\":%d,\"tsx\":%d,"
                    "\"degradado\":%d,"
                    "\"celulas\":[",
                    (unsigned)num_nucleos,
                    (unsigned)inf.numero_nucleos_logicos,
                    inf.fabricante,
                    inf.marca,
                    (unsigned)inf.familia,
                    (unsigned)inf.modelo,
                    (unsigned)inf.stepping,
                    (unsigned)inf.freq_base_mhz,
                    (unsigned)inf.freq_boost_mhz,
                    (unsigned)inf.freq_barramento_mhz,
                    (unsigned)inf.cache.l1d,
                    (unsigned)inf.cache.l1i,
                    (unsigned)inf.cache.l2,
                    (unsigned)inf.cache.l3,
                    (unsigned)inf.cache.assoc_l1d,
                    (unsigned)inf.cache.assoc_l2,
                    (unsigned)inf.cache.assoc_l3,
                    (unsigned)inf.contadores_disponiveis,
                    (int)inf.features.sse,   (int)inf.features.sse2,
                    (int)inf.features.sse3,  (int)inf.features.ssse3,
                    (int)inf.features.sse41, (int)inf.features.sse42,
                    (int)inf.features.avx,   (int)inf.features.avx2,
                    (int)inf.features.avx512f,(int)inf.features.aes,
                    (int)inf.features.rdrand,(int)inf.features.rdseed,
                    (int)inf.features.bmi1,  (int)inf.features.bmi2,
                    (int)inf.features.tsx,
                    (int)inf.modo_degradado
                );

                uint32_t primeiro = 1;
                for (uint32_t k = 0; k < MAX_CELULAS; k++) {
                    if (!grade_compartilhada.celulas[k].ativa) continue;
                    if (!primeiro) fprintf(f, ",");
                    BufferAmostras *buf = obter_buffer(
                        (uint8_t)grade_compartilhada.celulas[k].id_processo);
                    uint32_t amostras = buf ? buf->contagem : 0;
                    uint64_t ciclos   = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].ciclos_clock
                        : 0;
                    uint64_t instr    = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].instrucoes_retiradas
                        : 0;
                    uint64_t miss_l1  = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].cache_miss_l1
                        : 0;
                    uint64_t miss_l2  = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].cache_miss_l2
                        : 0;
                    uint64_t branch   = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].branch_mispredictions
                        : 0;
                    uint64_t stall    = buf && buf->contagem > 0
                        ? buf->entradas[(buf->indice_escrita + CAPACIDADE_BUFFER_AMOSTRAS - 1)
                                        % CAPACIDADE_BUFFER_AMOSTRAS].ciclos_stall
                        : 0;
                    fprintf(f,
                        "{\"nucleo\":%u,\"entropia\":%.6f,\"categoria\":%d"
                        ",\"amostras\":%u,\"ciclos\":%llu,\"instrucoes\":%llu"
                        ",\"miss_l1\":%llu,\"miss_l2\":%llu"
                        ",\"branch_miss\":%llu,\"stall\":%llu}",
                        grade_compartilhada.celulas[k].id_processo,
                        grade_compartilhada.celulas[k].indice_entropia,
                        (int)grade_compartilhada.celulas[k].categoria,
                        amostras,
                        (unsigned long long)ciclos,
                        (unsigned long long)instr,
                        (unsigned long long)miss_l1,
                        (unsigned long long)miss_l2,
                        (unsigned long long)branch,
                        (unsigned long long)stall);
                    primeiro = 0;
                }
                fprintf(f, "]}");
                fclose(f);
            }
        }
    }

    uint64_t tsc_fim_ciclo = ler_tsc();
    uint64_t ciclos_5ms    = ms_para_ciclos(5);
    if ((tsc_fim_ciclo - tsc_inicio_ciclo) > ciclos_5ms) {
        registrar(NIVEL_AVISO, "micro_kernel", "ciclo de coleta excedeu 5ms");
    }
}

static void ciclo_renderizacao(void) {
    // * aguarda versao par para garantir leitura consistente da grade
    grade_aguardar_leitura_segura(&grade_compartilhada);

    renderizador_renderizar_quadro(&grade_compartilhada, &estado_camera);
    processar_eventos(&estado_camera, &grade_compartilhada);
}

bool micro_kernel_inicializar(void) {
    calibrar_tsc();

    memset(&grade_compartilhada, 0, sizeof(GradeEntropia));

    // * posicao e angulo isometrico para mostrar os tres planos da grade
    estado_camera.posicao.x          = -5.0f;
    estado_camera.posicao.y          = -8.0f;
    estado_camera.posicao.z          = -22.0f;
    estado_camera.rotacao_horizontal = -35.0f;
    estado_camera.rotacao_vertical   = 25.0f;
    estado_camera.fov                = 50.0f;
    estado_camera.zoom               = 1.0f;

    contexto_coleta.intervalo_ms = 16;
    contexto_coleta.ativo        = true;
    contexto_renderizacao.ativo  = true;

    // * buffer_log nao tem dependencias, inicializado implicitamente pelo primeiro uso

    ResultadoInicializacao res_coletor = inicializar();
    if (!res_coletor.sucesso) {
        registrar_erro("coletor_cpu", res_coletor.codigo, res_coletor.endereco_falha);
        return false;
    }
    registrar(NIVEL_INFO, "coletor_cpu", "inicializado");

    // * calculador_entropia depende de coletor_cpu, sem inicializacao propria necessaria
    registrar(NIVEL_INFO, "calculador_entropia", "inicializado");

    Resolucao resolucao;
    resolucao.largura = LARGURA_MAX_FRAMEBUFFER;
    resolucao.altura  = ALTURA_MAX_FRAMEBUFFER;
    ResultadoInicializacao res_render = renderizador_inicializar(resolucao);
    if (!res_render.sucesso) {
        registrar_erro("renderizador_3d", res_render.codigo, res_render.endereco_falha);
        return false;
    }
    registrar(NIVEL_INFO, "renderizador_3d", "inicializado");

    // * gerenciador_interacao depende de renderizador_3d
    gerenciador_interacao_inicializar(&estado_camera);
    registrar(NIVEL_INFO, "gerenciador_interacao", "inicializado");

    // * serializador_snapshot nao tem dependencias de inicializacao
    registrar(NIVEL_INFO, "serializador_snapshot", "inicializado");

    executando = true;
    registrar(NIVEL_INFO, "micro_kernel", "inicializacao concluida");
    return true;
}

void micro_kernel_executar(void) {
    uint64_t ciclos_coleta       = ms_para_ciclos(contexto_coleta.intervalo_ms);
    uint64_t ciclos_renderizacao = ms_para_ciclos(8);

    uint64_t ultimo_tsc_coleta     = ler_tsc();
    uint64_t ultimo_tsc_renderizacao = ler_tsc();

    while (executando) {
        uint64_t agora = ler_tsc();

        if (contexto_coleta.ativo &&
            (agora - ultimo_tsc_coleta) >= ciclos_coleta) {
            ciclo_coleta();
            ultimo_tsc_coleta = agora;
        }

        agora = ler_tsc();

        if (contexto_renderizacao.ativo &&
            (agora - ultimo_tsc_renderizacao) >= ciclos_renderizacao) {
            ciclo_renderizacao();
            ultimo_tsc_renderizacao = agora;
        }
    }
}

void micro_kernel_encerrar(void) {
    executando = false;
    registrar(NIVEL_INFO, "micro_kernel", "encerramento solicitado");
}

void micro_kernel_passo_coleta(void) {
    ciclo_coleta();
}

void micro_kernel_passo_renderizacao(void) {
    ciclo_renderizacao();
}
