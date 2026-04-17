#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "micro_kernel.h"
#include "renderizador_3d.h"
#include "gerenciador_interacao.h"

#define LARGURA_JANELA 1280
#define ALTURA_JANELA   720

static SDL_Window   *janela   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *textura  = NULL;

static uint8_t traduzir_tecla(SDL_Keycode sdl_key) {
    switch (sdl_key) {
        case SDLK_UP:    return SCANCODE_SETA_CIMA;
        case SDLK_DOWN:  return SCANCODE_SETA_BAIXO;
        case SDLK_LEFT:  return SCANCODE_SETA_ESQ;
        case SDLK_RIGHT: return SCANCODE_SETA_DIR;
        case SDLK_r:     return SCANCODE_R;
        case SDLK_f:     return SCANCODE_F;
        case SDLK_s:     return SCANCODE_S;
        case SDLK_l:     return SCANCODE_L;
        case SDLK_d:     return SCANCODE_D;
        default:         return 0;
    }
}

static void processar_eventos_sdl(bool *continuar) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                *continuar = false;
                micro_kernel_encerrar();
                break;

            case SDL_KEYDOWN: {
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    *continuar = false;
                    micro_kernel_encerrar();
                    break;
                }
                uint8_t sc = traduzir_tecla(ev.key.keysym.sym);
                if (sc != 0) {
                    EventoEntrada entrada;
                    memset(&entrada, 0, sizeof(entrada));
                    entrada.tipo  = EVENTO_TECLA_PRESSIONADA;
                    entrada.tecla = sc;
                    injetar_evento(entrada);
                }
                break;
            }

            case SDL_MOUSEBUTTONDOWN: {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    EventoEntrada entrada;
                    memset(&entrada, 0, sizeof(entrada));
                    entrada.tipo             = EVENTO_CLIQUE_BOTAO_ESQ;
                    entrada.posicao_cursor.x = ev.button.x;
                    entrada.posicao_cursor.y = ev.button.y;
                    injetar_evento(entrada);
                }
                break;
            }

            case SDL_MOUSEMOTION: {
                if (ev.motion.state & SDL_BUTTON_LMASK) {
                    EventoEntrada entrada;
                    memset(&entrada, 0, sizeof(entrada));
                    entrada.tipo             = EVENTO_MOVIMENTO_MOUSE;
                    entrada.posicao_cursor.x = ev.motion.x;
                    entrada.posicao_cursor.y = ev.motion.y;
                    injetar_evento(entrada);
                }
                break;
            }

            case SDL_MOUSEWHEEL: {
                EventoEntrada entrada;
                memset(&entrada, 0, sizeof(entrada));
                entrada.tipo         = EVENTO_SCROLL_MOUSE;
                entrada.delta_scroll = ev.wheel.y;
                injetar_evento(entrada);
                break;
            }

            default:
                break;
        }
    }
}

static bool inicializar_sdl(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "sdl_init falhou: %s\n", SDL_GetError());
        return false;
    }

    janela = SDL_CreateWindow(
        "micro kernel entropia 3d",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LARGURA_JANELA, ALTURA_JANELA,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (janela == NULL) {
        fprintf(stderr, "sdl_createwindow falhou: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(
        janela, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (renderer == NULL) {
        fprintf(stderr, "sdl_createrenderer falhou: %s\n", SDL_GetError());
        return false;
    }

    // * textura no tamanho maximo do framebuffer interno, escala para a janela
    textura = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        (int)LARGURA_MAX_FRAMEBUFFER,
        (int)ALTURA_MAX_FRAMEBUFFER
    );
    if (textura == NULL) {
        fprintf(stderr, "sdl_createtexture falhou: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

static void encerrar_sdl(void) {
    if (textura)  SDL_DestroyTexture(textura);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (janela)   SDL_DestroyWindow(janela);
    SDL_Quit();
}

int main(void) {
    if (!inicializar_sdl()) {
        return 1;
    }

    if (!micro_kernel_inicializar()) {
        fprintf(stderr, "micro_kernel_inicializar falhou\n");
        encerrar_sdl();
        return 1;
    }

    Framebuffer *fb = renderizador_obter_framebuffer();
    if (fb == NULL || fb->pixels == NULL) {
        fprintf(stderr, "framebuffer nao disponivel\n");
        encerrar_sdl();
        return 1;
    }

    printf("micro kernel entropia 3d\n");
    printf("arrastar mouse: rotacionar | scroll: zoom | setas: translacao\n");
    printf("r: reset camera | f: focar no selecionado | esc: sair\n");

    bool continuar       = true;
    uint32_t ultimo_coleta = SDL_GetTicks();

    while (continuar) {
        processar_eventos_sdl(&continuar);

        uint32_t agora = SDL_GetTicks();
        if (agora - ultimo_coleta >= 100) {
            ultimo_coleta = agora;
            micro_kernel_passo_coleta();
        }

        micro_kernel_passo_renderizacao();

        SDL_UpdateTexture(
            textura,
            NULL,
            fb->pixels,
            (int)(fb->largura * sizeof(uint32_t))
        );

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, textura, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    encerrar_sdl();
    return 0;
}
