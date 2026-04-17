#ifndef RENDERIZADOR_3D_H
#define RENDERIZADOR_3D_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tipos.h"
#include "resultado.h"
#include "grade_entropia.h"

#define LARGURA_MAX_FRAMEBUFFER  ((uint32_t)1920)
#define ALTURA_MAX_FRAMEBUFFER   ((uint32_t)1080)

// * formato de pixel rgb sem canal alpha
typedef struct {
    uint32_t *pixels;
    uint32_t  largura;
    uint32_t  altura;
    size_t    tamanho_bytes;
} Framebuffer;

typedef struct {
    Vec3f posicao;
    float rotacao_horizontal;
    float rotacao_vertical;
    float fov;
    float zoom;
} EstadoCamera;

typedef enum {
    METRICA_NUCLEO_FISICO          = 0,
    METRICA_IDENTIFICADOR_PROCESSO = 1,
    METRICA_IDENTIFICADOR_THREAD   = 2,
    METRICA_FAIXA_TEMPO            = 3,
    METRICA_CARGA_TRABALHO         = 4
} MetricaEixo;

typedef enum {
    EIXO_X = 0,
    EIXO_Y = 1,
    EIXO_Z = 2
} Eixo;

typedef struct {
    Eixo       eixo;
    MetricaEixo metrica;
} MapeamentoEixo;

ResultadoInicializacao renderizador_inicializar(Resolucao resolucao);
void                   renderizador_renderizar_quadro(GradeEntropia *grade, EstadoCamera *camera);
void                   renderizador_configurar_mapeamento_eixo(Eixo eixo, MetricaEixo metrica);
Framebuffer           *renderizador_obter_framebuffer(void);
void                   renderizador_atualizar_grid(const GradeEntropia *grade);

#endif /* RENDERIZADOR_3D_H */
