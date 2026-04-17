#ifndef GERENCIADOR_INTERACAO_H
#define GERENCIADOR_INTERACAO_H

#include <stdint.h>
#include <stdbool.h>
#include "tipos.h"
#include "renderizador_3d.h"
#include "grade_entropia.h"

// * limite estatico de minitops, um por celula ativa maxima da grade
#define MAX_MINITOPS ((uint32_t)4096)

// * capacidade da fila interna de eventos simulados em modo de teste
#define CAPACIDADE_FILA_EVENTOS ((uint32_t)64)

// * scancodes do conjunto um do teclado para as teclas mapeadas
#define SCANCODE_SETA_CIMA    ((uint8_t)0x48)
#define SCANCODE_SETA_BAIXO   ((uint8_t)0x50)
#define SCANCODE_SETA_ESQ     ((uint8_t)0x4B)
#define SCANCODE_SETA_DIR     ((uint8_t)0x4D)
#define SCANCODE_R            ((uint8_t)0x13)
#define SCANCODE_F            ((uint8_t)0x21)
#define SCANCODE_S            ((uint8_t)0x1F)
#define SCANCODE_L            ((uint8_t)0x26)
#define SCANCODE_D            ((uint8_t)0x20)

#define PASSO_TRANSLACAO      (1.0f)

// * sensibilidade de rotacao: graus por pixel arrastado
#define SENSIBILIDADE_ROTACAO (0.3f)

#define FATOR_ZOOM_SCROLL     (0.1f)

// * numero de quadros para animacao de foco
#define QUADROS_ANIMACAO_FOCO ((uint32_t)15)

typedef enum {
    EVENTO_CLIQUE_BOTAO_ESQ = 0,
    EVENTO_SCROLL_MOUSE     = 1,
    EVENTO_TECLA_PRESSIONADA = 2,
    EVENTO_MOVIMENTO_MOUSE  = 3
} TipoEvento;

typedef struct {
    TipoEvento tipo;
    Vec2i      posicao_cursor;
    int32_t    delta_scroll;
    uint8_t    tecla;
} EventoEntrada;

typedef struct {
    uint32_t id_processo;
    uint32_t id_thread;
    Vec3f    coordenada;
    double   indice_entropia;
    bool     selecionado;
} MiniTop;

typedef struct {
    MiniTop  minitops[MAX_MINITOPS];
    uint32_t contagem;
} ListaMiniTops;

void       gerenciador_interacao_inicializar(EstadoCamera *camera_inicial);
void       processar_eventos(EstadoCamera *camera, GradeEntropia *grade);
ListaMiniTops *obter_selecao(void);
MiniTop   *executar_ray_cast(Vec2i cursor, GradeEntropia *grade, EstadoCamera *camera);

#ifdef TESTE_EVENTOS
// * injecao de eventos para uso exclusivo em testes automatizados
void injetar_evento(EventoEntrada evento);
void limpar_fila_eventos(void);
#endif

#endif /* GERENCIADOR_INTERACAO_H */
