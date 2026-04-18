#include "gerenciador_interacao.h"
#include "buffer_log.h"
#include "serializador_snapshot.h"
#include "renderizador_3d.h"

#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#define PI_RAIO 3.14159265358979323846f

#define THRESHOLD_SELECAO_PIXELS 8

// * dimensoes usadas como fallback quando framebuffer nao esta disponivel
#define LARGURA_PADRAO_SELECAO 800
#define ALTURA_PADRAO_SELECAO  600

// * tamanho estatico do buffer de snapshot: cabecalho mais espaco para todas as celulas
#define TAMANHO_BUFFER_SNAPSHOT \
    (sizeof(CabecalhoSnapshot) + (size_t)MAX_CELULAS * sizeof(CelulaEntropia))

// * buffer estatico para operacoes de snapshot, sem malloc
static uint8_t s_buffer_snapshot[TAMANHO_BUFFER_SNAPSHOT];

// * camera padrao restaurada pelo reset e usada na inicializacao
static const EstadoCamera CAMERA_PADRAO = {
    .posicao            = { 0.0f, 0.0f, -20.0f },
    .rotacao_horizontal = 0.0f,
    .rotacao_vertical   = 0.0f,
    .fov                = 60.0f,
    .zoom               = 1.0f
};

// * estado interno do modulo, alocacao estatica sem malloc
static ListaMiniTops lista_selecao;

// * fila circular de eventos para modo userspace e testes
static EventoEntrada fila_eventos[CAPACIDADE_FILA_EVENTOS];
static uint32_t      fila_inicio  = 0;
static uint32_t      fila_fim     = 0;
static uint32_t      fila_tamanho = 0;

// * estado do mouse para calculo de delta de arrasto
static bool  botao_esq_pressionado = false;
static Vec2i posicao_mouse_anterior = { 0, 0 };

static bool     animacao_foco_ativa    = false;
static uint32_t quadros_foco_restantes = 0;
static Vec3f    posicao_foco_origem    = { 0.0f, 0.0f, 0.0f };
static Vec3f    posicao_foco_destino   = { 0.0f, 0.0f, 0.0f };

// * ponteiro para camera ativa, definido na inicializacao
static EstadoCamera *camera_ativa = NULL;


static bool fila_vazia(void) {
    return fila_tamanho == 0;
}

static bool fila_cheia(void) {
    return fila_tamanho >= CAPACIDADE_FILA_EVENTOS;
}

static void enfileirar_evento(EventoEntrada ev) {
    if (fila_cheia()) {
        // ! descarta evento mais antigo para nao bloquear o ciclo principal
        fila_inicio  = (fila_inicio + 1) % CAPACIDADE_FILA_EVENTOS;
        fila_tamanho--;
    }
    fila_eventos[fila_fim] = ev;
    fila_fim               = (fila_fim + 1) % CAPACIDADE_FILA_EVENTOS;
    fila_tamanho++;
}

static bool desenfileirar_evento(EventoEntrada *destino) {
    if (fila_vazia()) return false;
    *destino     = fila_eventos[fila_inicio];
    fila_inicio  = (fila_inicio + 1) % CAPACIDADE_FILA_EVENTOS;
    fila_tamanho--;
    return true;
}

#ifdef RING0_REAL

// * leitura direta da porta do controlador de teclado
static uint8_t ler_porta_teclado(void) {
    uint8_t valor;
    __asm__ volatile ("inb $0x60, %0" : "=a"(valor));
    return valor;
}

// * verifica bit zero do registrador de status antes de ler
static bool teclado_tem_dado(void) {
    uint8_t status;
    __asm__ volatile ("inb $0x64, %0" : "=a"(status));
    return (status & 0x01) != 0;
}

static void ler_eventos_hardware(void) {
    while (teclado_tem_dado()) {
        uint8_t scancode = ler_porta_teclado();
        if (scancode & 0x80) continue;
        EventoEntrada ev = {
            .tipo           = EVENTO_TECLA_PRESSIONADA,
            .posicao_cursor = { 0, 0 },
            .delta_scroll   = 0,
            .tecla          = scancode
        };
        enfileirar_evento(ev);
    }
}

#else

// * em modo userspace a fila e preenchida via injetar_evento
static void ler_eventos_hardware(void) {
    // * nenhuma leitura de porta necessaria fora do modo privilegiado
}

#endif /* RING0_REAL */

#define COR_FUNDO_PAINEL    ((uint32_t)0x00202020)
#define COR_BRANCA          ((uint32_t)0x00FFFFFF)
#define LARGURA_PAINEL      200
#define ALTURA_PAINEL       150
#define RAIO_NO_DESTACADO   6

static void escrever_pixel_painel(uint32_t *pixels, uint32_t largura, uint32_t altura,
                                   int32_t x, int32_t y, uint32_t cor) {
    if (x < 0 || y < 0 || (uint32_t)x >= largura || (uint32_t)y >= altura) {
        return;
    }
    pixels[(uint32_t)y * largura + (uint32_t)x] = cor;
}

static void desenhar_contorno_circulo(uint32_t *pixels, uint32_t largura, uint32_t altura,
                                       int32_t cx, int32_t cy, int32_t r, uint32_t cor) {
    int32_t x = 0;
    int32_t y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        escrever_pixel_painel(pixels, largura, altura, cx + x, cy + y, cor);
        escrever_pixel_painel(pixels, largura, altura, cx - x, cy + y, cor);
        escrever_pixel_painel(pixels, largura, altura, cx + x, cy - y, cor);
        escrever_pixel_painel(pixels, largura, altura, cx - x, cy - y, cor);
        escrever_pixel_painel(pixels, largura, altura, cx + y, cy + x, cor);
        escrever_pixel_painel(pixels, largura, altura, cx - y, cy + x, cor);
        escrever_pixel_painel(pixels, largura, altura, cx + y, cy - x, cor);
        escrever_pixel_painel(pixels, largura, altura, cx - y, cy - x, cor);
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

static void renderizar_painel_detalhes(Framebuffer *fb, MiniTop *mt, uint32_t offset_x) {
    if (fb == NULL || mt == NULL || fb->pixels == NULL) {
        return;
    }

    uint32_t ox = offset_x;
    uint32_t oy = 0;

    uint32_t px, py;
    for (py = oy; py < oy + ALTURA_PAINEL && py < fb->altura; py++) {
        for (px = ox; px < ox + LARGURA_PAINEL && px < fb->largura; px++) {
            fb->pixels[py * fb->largura + px] = COR_FUNDO_PAINEL;
        }
    }

    // * renderiza linhas de informacao como barras de pixels brancos em posicoes fixas
    uint32_t col;
    uint32_t comprimento_id_proc = mt->id_processo % (LARGURA_PAINEL - 20) + 5;
    for (col = ox + 10; col < ox + 10 + comprimento_id_proc && col < fb->largura; col++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)col, (int32_t)(oy + 10), COR_BRANCA);
    }

    uint32_t comprimento_id_thread = mt->id_thread % (LARGURA_PAINEL - 20) + 5;
    for (col = ox + 10; col < ox + 10 + comprimento_id_thread && col < fb->largura; col++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)col, (int32_t)(oy + 25), COR_BRANCA);
    }

    uint32_t comprimento_entropia = (uint32_t)(mt->indice_entropia * (double)(LARGURA_PAINEL - 20));
    for (col = ox + 10; col < ox + 10 + comprimento_entropia && col < fb->largura; col++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)col, (int32_t)(oy + 40), COR_BRANCA);
    }

    uint32_t bx, by;
    for (bx = ox; bx < ox + LARGURA_PAINEL && bx < fb->largura; bx++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)bx, (int32_t)oy, COR_BRANCA);
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)bx, (int32_t)(oy + ALTURA_PAINEL - 1), COR_BRANCA);
    }
    for (by = oy; by < oy + ALTURA_PAINEL && by < fb->altura; by++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)ox, (int32_t)by, COR_BRANCA);
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)(ox + LARGURA_PAINEL - 1), (int32_t)by, COR_BRANCA);
    }
}

static void renderizar_sobreposicao_log(void) {
    Framebuffer *fb = renderizador_obter_framebuffer();
    if (fb == NULL || fb->pixels == NULL) {
        return;
    }

    static EntradaLog entradas_log[64];
    size_t total = obter_entradas(entradas_log, 64);

    // * preenche fundo da sobreposicao de log na metade inferior da tela
    uint32_t inicio_y = fb->altura / 2;
    uint32_t px, py;
    for (py = inicio_y; py < fb->altura; py++) {
        for (px = 0; px < fb->largura; px++) {
            fb->pixels[py * fb->largura + px] = COR_FUNDO_PAINEL;
        }
    }

    // * renderiza cada entrada como barra de pixels na posicao correspondente
    size_t i;
    for (i = 0; i < total && i < 32; i++) {
        uint32_t linha_y = inicio_y + (uint32_t)i * 14 + 5;
        if (linha_y + 2 >= fb->altura) {
            break;
        }
        // * comprimento da barra proporcional ao nivel do log
        uint32_t comprimento = 20 + (uint32_t)(entradas_log[i].nivel) * 40;
        if (comprimento > fb->largura - 10) {
            comprimento = fb->largura - 10;
        }
        for (px = 5; px < 5 + comprimento && px < fb->largura; px++) {
            escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)px, (int32_t)linha_y, COR_BRANCA);
        }
    }

    for (px = 0; px < fb->largura; px++) {
        escrever_pixel_painel(fb->pixels, fb->largura, fb->altura, (int32_t)px, (int32_t)inicio_y, COR_BRANCA);
    }
}

static void renderizar_paineis_selecao(EstadoCamera *cam) {
    Framebuffer *fb = renderizador_obter_framebuffer();
    if (fb == NULL || fb->pixels == NULL || cam == NULL) {
        return;
    }

    uint32_t largura = fb->largura;
    uint32_t altura  = fb->altura;

    float fov_rad = cam->fov * (PI_RAIO / 180.0f);
    float fator   = ((float)largura * 0.5f) / tanf(fov_rad * 0.5f);

    float yaw_rad   = cam->rotacao_horizontal * (PI_RAIO / 180.0f);
    float pitch_rad = cam->rotacao_vertical   * (PI_RAIO / 180.0f);
    float cos_yaw   = cosf(yaw_rad);
    float sin_yaw   = sinf(yaw_rad);
    float cos_pitch = cosf(pitch_rad);
    float sin_pitch = sinf(pitch_rad);

    uint32_t painel_offset = 0;
    uint32_t i;
    for (i = 0; i < lista_selecao.contagem; i++) {
        MiniTop *mt = &lista_selecao.minitops[i];
        if (!mt->selecionado) {
            continue;
        }

        // * projeta coordenada do minitop para espaco da camera
        float tx = mt->coordenada.x - cam->posicao.x;
        float ty = mt->coordenada.y - cam->posicao.y;
        float tz = mt->coordenada.z - cam->posicao.z;

        float rx  = tx * cos_yaw + tz * sin_yaw;
        float ry  = ty;
        float rz  = -tx * sin_yaw + tz * cos_yaw;
        float ry2 = ry * cos_pitch - rz * sin_pitch;
        float rz2 = ry * sin_pitch + rz * cos_pitch;

        if (rz2 > 0.0f) {
            float px_f = (rx / rz2) * fator + (float)largura  * 0.5f;
            float py_f = -(ry2 / rz2) * fator + (float)altura * 0.5f;
            float zoom = cam->zoom;
            px_f = (px_f - (float)largura  * 0.5f) * zoom + (float)largura  * 0.5f;
            py_f = (py_f - (float)altura * 0.5f) * zoom + (float)altura * 0.5f;

            // * destaca o no selecionado com circulo e contorno branco
            desenhar_contorno_circulo(fb->pixels, largura, altura,
                                      (int32_t)px_f, (int32_t)py_f,
                                      RAIO_NO_DESTACADO, COR_BRANCA);
        }

        // * renderiza painel de detalhes deslocado para suportar multipla selecao
        renderizar_painel_detalhes(fb, mt, painel_offset);
        painel_offset += LARGURA_PAINEL + 5;
        if (painel_offset + LARGURA_PAINEL > largura) {
            break;
        }
    }
}

static void sincronizar_minitops(GradeEntropia *grade) {
    if (grade == NULL) {
        return;
    }

    uint32_t contagem = grade->contagem_ativa;
    if (contagem > MAX_MINITOPS) {
        contagem = MAX_MINITOPS;
    }

    uint32_t i;
    uint32_t idx = 0;
    for (i = 0; i < MAX_CELULAS && idx < contagem; i++) {
        if (!grade->celulas[i].ativa) {
            continue;
        }
        // * preserva estado de selecao se o minitop ja existia
        bool sel_anterior = false;
        if (idx < lista_selecao.contagem) {
            sel_anterior = lista_selecao.minitops[idx].selecionado;
        }
        lista_selecao.minitops[idx].id_processo    = grade->celulas[i].id_processo;
        lista_selecao.minitops[idx].id_thread      = grade->celulas[i].id_thread;
        lista_selecao.minitops[idx].coordenada     = grade->celulas[i].coordenada;
        lista_selecao.minitops[idx].indice_entropia = grade->celulas[i].indice_entropia;
        lista_selecao.minitops[idx].selecionado    = sel_anterior;
        idx++;
    }
    lista_selecao.contagem = idx;
}






static void processar_evento_individual(EventoEntrada *ev, EstadoCamera *cam, GradeEntropia *grade) {

    switch (ev->tipo) {
        case EVENTO_MOVIMENTO_MOUSE:
            posicao_mouse_anterior = ev->posicao_cursor;
            break;

        case EVENTO_CLIQUE_BOTAO_ESQ:
            botao_esq_pressionado  = true;
            posicao_mouse_anterior = ev->posicao_cursor;
            // * executa ray cast ao clicar para selecionar minitop
            if (grade != NULL) {
                executar_ray_cast(ev->posicao_cursor, grade, cam);
            }
            break;

        case EVENTO_TECLA_PRESSIONADA:
            switch (ev->tecla) {
                case SCANCODE_S:
                    if (grade != NULL) {
                        serializar(grade, s_buffer_snapshot, TAMANHO_BUFFER_SNAPSHOT);
                        registrar(NIVEL_INFO, "GerenciadorInteracao", "snapshot serializado");
                    }
                    break;
                case SCANCODE_L:
                    if (grade != NULL) {
                        deserializar(s_buffer_snapshot, TAMANHO_BUFFER_SNAPSHOT, grade);
                        registrar(NIVEL_INFO, "GerenciadorInteracao", "snapshot deserializado");
                    }
                    break;
                case SCANCODE_D:
                    renderizar_sobreposicao_log();
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void avancar_animacao_foco(EstadoCamera *cam) {
    if (!animacao_foco_ativa || quadros_foco_restantes == 0) {
        animacao_foco_ativa = false;
        return;
    }

    // * interpolacao linear ao longo dos quadros de animacao
    float t = 1.0f - ((float)(quadros_foco_restantes - 1) / (float)QUADROS_ANIMACAO_FOCO);
    cam->posicao.x = posicao_foco_origem.x + t * (posicao_foco_destino.x - posicao_foco_origem.x);
    cam->posicao.y = posicao_foco_origem.y + t * (posicao_foco_destino.y - posicao_foco_origem.y);
    cam->posicao.z = posicao_foco_origem.z + t * (posicao_foco_destino.z - posicao_foco_origem.z);

    quadros_foco_restantes--;
    if (quadros_foco_restantes == 0) {
        animacao_foco_ativa = false;
    }
}

void gerenciador_interacao_inicializar(EstadoCamera *camera_inicial) {
    camera_ativa = camera_inicial;
    if (camera_inicial != NULL) {
        *camera_inicial = CAMERA_PADRAO;
    }
    fila_inicio            = 0;
    fila_fim               = 0;
    fila_tamanho           = 0;
    botao_esq_pressionado  = false;
    animacao_foco_ativa    = false;
    quadros_foco_restantes = 0;
    lista_selecao.contagem = 0;
    registrar(NIVEL_INFO, "GerenciadorInteracao", "inicializado");
}

void processar_eventos(EstadoCamera *cam, GradeEntropia *grade) {
    ler_eventos_hardware();

    // * sincroniza lista de minitops com o estado atual da grade antes de processar eventos
    sincronizar_minitops(grade);

    EventoEntrada ev;
    while (desenfileirar_evento(&ev)) {
        processar_evento_individual(&ev, cam, grade);
    }

    avancar_animacao_foco(cam);

    // * renderiza paineis de selecao apos processar todos os eventos do ciclo
    renderizar_paineis_selecao(cam);
}

ListaMiniTops *obter_selecao(void) {
    return &lista_selecao;
}

// * implementacao de ray casting via projecao 2d com threshold de pixels
// * seleciona o minitop mais proximo da camera entre os que se sobrepoem no cursor
MiniTop *executar_ray_cast(Vec2i cursor, GradeEntropia *grade, EstadoCamera *camera) {
    if (grade == NULL || camera == NULL) {
        return NULL;
    }

    uint32_t largura = LARGURA_PADRAO_SELECAO;
    uint32_t altura  = ALTURA_PADRAO_SELECAO;
    Framebuffer *fb  = renderizador_obter_framebuffer();
    if (fb != NULL && fb->largura > 0 && fb->altura > 0) {
        largura = fb->largura;
        altura  = fb->altura;
    }

    float fov_rad = camera->fov * (PI_RAIO / 180.0f);
    float fator   = ((float)largura * 0.5f) / tanf(fov_rad * 0.5f);

    float yaw_rad   = camera->rotacao_horizontal * (PI_RAIO / 180.0f);
    float pitch_rad = camera->rotacao_vertical   * (PI_RAIO / 180.0f);

    float cos_yaw   = cosf(yaw_rad);
    float sin_yaw   = sinf(yaw_rad);
    float cos_pitch = cosf(pitch_rad);
    float sin_pitch = sinf(pitch_rad);

    MiniTop *selecionado    = NULL;
    float    menor_prof     = 0.0f;
    bool     encontrou      = false;

    uint32_t i;
    for (i = 0; i < lista_selecao.contagem; i++) {
        MiniTop *mt = &lista_selecao.minitops[i];

        Vec3f coord = mt->coordenada;

        float tx = coord.x - camera->posicao.x;
        float ty = coord.y - camera->posicao.y;
        float tz = coord.z - camera->posicao.z;

        float rx  = tx * cos_yaw + tz * sin_yaw;
        float ry  = ty;
        float rz  = -tx * sin_yaw + tz * cos_yaw;

        float ry2 = ry * cos_pitch - rz * sin_pitch;
        float rz2 = ry * sin_pitch + rz * cos_pitch;

        // * ponto atras da camera, ignorar
        if (rz2 <= 0.0f) {
            continue;
        }

        float px_f = (rx / rz2) * fator + (float)largura  * 0.5f;
        float py_f = -(ry2 / rz2) * fator + (float)altura * 0.5f;

        float zoom = camera->zoom;
        px_f = (px_f - (float)largura  * 0.5f) * zoom + (float)largura  * 0.5f;
        py_f = (py_f - (float)altura * 0.5f) * zoom + (float)altura * 0.5f;

        float dx = px_f - (float)cursor.x;
        float dy = py_f - (float)cursor.y;
        float dist_px = sqrtf(dx * dx + dy * dy);

        if (dist_px > (float)THRESHOLD_SELECAO_PIXELS) {
            continue;
        }

        // * profundidade e o z apos rotacao da camera
        if (!encontrou || rz2 < menor_prof) {
            menor_prof  = rz2;
            selecionado = mt;
            encontrou   = true;
        }
    }

    if (selecionado != NULL) {
        for (i = 0; i < lista_selecao.contagem; i++) {
            lista_selecao.minitops[i].selecionado = false;
        }
        selecionado->selecionado = true;
    }

    return selecionado;
}

#ifdef TESTE_EVENTOS
void injetar_evento(EventoEntrada evento) {
    enfileirar_evento(evento);
}

void limpar_fila_eventos(void) {
    fila_inicio  = 0;
    fila_fim     = 0;
    fila_tamanho = 0;
}
#endif /* TESTE_EVENTOS */
