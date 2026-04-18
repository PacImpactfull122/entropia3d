#include "renderizador_3d.h"
#include "buffer_log.h"
#include "codigos_erro.h"

#include <string.h>
#include <stdint.h>
#include <math.h>

// * buffer estatico de pixels, sem malloc em anel zero
static uint32_t s_pixels_framebuffer[LARGURA_MAX_FRAMEBUFFER * ALTURA_MAX_FRAMEBUFFER];

static Framebuffer s_framebuffer;
static bool s_inicializado = false;
static MapeamentoEixo s_mapeamentos[3];

ResultadoInicializacao renderizador_inicializar(Resolucao resolucao) {
    ResultadoInicializacao resultado;
    resultado.sucesso        = false;
    resultado.codigo         = 0;
    resultado.endereco_falha = 0;

    const char *nome = "Renderizador3D";

    if (resolucao.largura == 0 || resolucao.altura == 0) {
        registrar_erro(nome, E_FRAMEBUFFER_INDISPONIVEL, 0);
        resultado.codigo = E_FRAMEBUFFER_INDISPONIVEL;
        return resultado;
    }

    uint32_t largura = resolucao.largura;
    uint32_t altura  = resolucao.altura;

    if (largura > LARGURA_MAX_FRAMEBUFFER) largura = LARGURA_MAX_FRAMEBUFFER;
    if (altura  > ALTURA_MAX_FRAMEBUFFER)  altura  = ALTURA_MAX_FRAMEBUFFER;

    s_framebuffer.pixels       = s_pixels_framebuffer;
    s_framebuffer.largura      = largura;
    s_framebuffer.altura       = altura;
    s_framebuffer.tamanho_bytes = (size_t)largura * (size_t)altura * sizeof(uint32_t);

    memset(s_pixels_framebuffer, 0, s_framebuffer.tamanho_bytes);

    s_mapeamentos[EIXO_X].eixo    = EIXO_X;
    s_mapeamentos[EIXO_X].metrica = METRICA_NUCLEO_FISICO;
    s_mapeamentos[EIXO_Y].eixo    = EIXO_Y;
    s_mapeamentos[EIXO_Y].metrica = METRICA_IDENTIFICADOR_PROCESSO;
    s_mapeamentos[EIXO_Z].eixo    = EIXO_Z;
    s_mapeamentos[EIXO_Z].metrica = METRICA_FAIXA_TEMPO;

    s_inicializado = true;
    registrar(NIVEL_INFO, nome, "framebuffer inicializado");

    resultado.sucesso = true;
    return resultado;
}

#define GRID_N   32
#define HIST_LEN GRID_N

static float s_historico[HIST_LEN][GRID_N];
static float s_grid_suavizado[HIST_LEN][GRID_N];
static float s_fase_onda  = 0.0f;
static int   s_hist_ok    = 0;
// * amplitude suavizada entre frames para evitar saltos quando os dados de entropia atualizam
static float s_amp_suav[GRID_N];

void renderizador_atualizar_grid(const GradeEntropia *grade) {
    if (!s_hist_ok) {
        memset(s_historico,      0, sizeof(s_historico));
        memset(s_grid_suavizado, 0, sizeof(s_grid_suavizado));
        for (int j = 0; j < GRID_N; j++) s_amp_suav[j] = 0.5f;
        s_hist_ok = 1;
    }

    s_fase_onda = fmodf(s_fase_onda + 0.05f, 6.2831853f);

    float ft = s_fase_onda;

    // * coleta amplitude por nucleo a partir dos dados reais
    float amp[GRID_N];
    for (int j = 0; j < GRID_N; j++) amp[j] = 0.5f;

    if (grade != NULL && grade->contagem_ativa > 0) {
        float max_nucleo = 1.0f;
        uint32_t enc = 0;
        for (uint32_t k = 0; k < MAX_CELULAS && enc < grade->contagem_ativa; k++) {
            if (!grade->celulas[k].ativa) continue;
            enc++;
            if (grade->celulas[k].coordenada.x > max_nucleo)
                max_nucleo = grade->celulas[k].coordenada.x;
        }
        enc = 0;
        for (uint32_t k = 0; k < MAX_CELULAS && enc < grade->contagem_ativa; k++) {
            if (!grade->celulas[k].ativa) continue;
            enc++;
            int gj = (int)(grade->celulas[k].coordenada.x / max_nucleo * (float)(GRID_N - 1));
            if (gj < 0) gj = 0;
            if (gj >= GRID_N) gj = GRID_N - 1;
            amp[gj] = (float)grade->celulas[k].indice_entropia;
        }
        // * interpola colunas sem dados
        for (int j = 1; j < GRID_N - 1; j++)
            if (amp[j] == 0.5f)
                amp[j] = (amp[j-1] + amp[j+1]) * 0.5f;
    }

    // * gera superficie 2d com onda real em ambos os eixos
    // * amplitude por coluna reflete a entropia do nucleo correspondente
    // * interpola amplitude suavizada em direcao ao valor alvo, fator baixo para transicao lenta
    for (int j = 0; j < GRID_N; j++) {
        s_amp_suav[j] += (amp[j] - s_amp_suav[j]) * 0.04f;
    }

    for (int i = 0; i < GRID_N; i++) {
        float fi = (float)i / (float)(GRID_N - 1);
        for (int j = 0; j < GRID_N; j++) {
            float fj = (float)j / (float)(GRID_N - 1);
            float a  = s_amp_suav[j];

            float v = a
                    + (1.0f - a) * 0.4f * sinf(fj * 6.28f + fi * 5.50f + ft * 2.0f)
                    + a          * 0.3f * cosf(fj * 9.42f - fi * 6.28f + ft * 2.5f)
                    + 0.08f * sinf((fj * 2.0f + fi * 1.5f) * 7.85f + ft * 1.7f);

            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            s_grid_suavizado[i][j] = v;
        }
    }
}

static uint32_t cor_oceano(float v) {
    // * colormap oceano, azul escuro para branco
    static const float p[9][4] = {
        {0.00f,  3.0f,  0.0f, 28.0f},
        {0.10f, 10.0f, 10.0f, 58.0f},
        {0.22f, 13.0f, 59.0f,110.0f},
        {0.38f,  0.0f,119.0f,182.0f},
        {0.52f,  0.0f,180.0f,216.0f},
        {0.65f,144.0f,224.0f,239.0f},
        {0.80f,202.0f,240.0f,248.0f},
        {0.92f,255.0f,232.0f,214.0f},
        {1.00f,255.0f,255.0f,255.0f},
    };
    if (v <= 0.0f) return 0x0003001CU;
    if (v >= 1.0f) return 0x00FFFFFFU;
    for (int i = 0; i < 8; i++) {
        if (v <= p[i+1][0]) {
            float t = (v - p[i][0]) / (p[i+1][0] - p[i][0]);
            uint8_t r = (uint8_t)(p[i][1] + t * (p[i+1][1] - p[i][1]));
            uint8_t g = (uint8_t)(p[i][2] + t * (p[i+1][2] - p[i][2]));
            uint8_t b = (uint8_t)(p[i][3] + t * (p[i+1][3] - p[i][3]));
            return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return 0x00FFFFFFU;
}

static void escrever_pixel(uint32_t *pixels, uint32_t largura, uint32_t altura,
                            int32_t x, int32_t y, uint32_t cor) {
    if (x < 0 || y < 0 || (uint32_t)x >= largura || (uint32_t)y >= altura) return;
    pixels[(uint32_t)y * largura + (uint32_t)x] = cor;
}

static void desenhar_linha(uint32_t *pixels, uint32_t largura, uint32_t altura,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t cor) {
    int32_t dx  =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy  = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int32_t sx  = (x0 < x1) ? 1 : -1;
    int32_t sy  = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        escrever_pixel(pixels, largura, altura, x0, y0, cor);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { if (x0 == x1) break; err += dy; x0 += sx; }
        if (e2 <= dx) { if (y0 == y1) break; err += dx; y0 += sy; }
    }
}

static void projetar_orto(float wx, float wy, float wz,
                           uint32_t largura, uint32_t altura,
                           int32_t *px, int32_t *py) {
    // * projecao ortografica com angulos fixos de elevacao e azimute
    static const float cos_yaw   =  0.7071f;
    static const float sin_yaw   = -0.7071f;
    static const float cos_pitch =  0.8829f;
    static const float sin_pitch = -0.4695f;

    float rx  = wx * cos_yaw  + wz * sin_yaw;
    float ry  = wy;
    float rz  = -wx * sin_yaw + wz * cos_yaw;
    float ry2 = ry * cos_pitch - rz * sin_pitch;

    float fator = 340.0f;
    float cx    = (float)largura  * 0.47f;
    float cy    = (float)altura   * 0.84f;

    *px = (int32_t)(rx  * fator + cx);
    *py = (int32_t)(-ry2 * fator + cy);
}

static void preencher_triangulo(uint32_t *pixels, uint32_t largura, uint32_t altura,
                                 int32_t x0, int32_t y0,
                                 int32_t x1, int32_t y1,
                                 int32_t x2, int32_t y2,
                                 uint32_t cor) {
    if (y0 > y1) { int32_t t; t=y0;y0=y1;y1=t; t=x0;x0=x1;x1=t; }
    if (y0 > y2) { int32_t t; t=y0;y0=y2;y2=t; t=x0;x0=x2;x2=t; }
    if (y1 > y2) { int32_t t; t=y1;y1=y2;y2=t; t=x1;x1=x2;x2=t; }

    int32_t dy_total = y2 - y0;
    if (dy_total == 0) return;

    for (int32_t y = y0; y <= y2; y++) {
        if (y < 0 || (uint32_t)y >= altura) continue;
        float t_total = (float)(y - y0) / (float)dy_total;
        int32_t xa = x0 + (int32_t)((float)(x2 - x0) * t_total);
        int32_t xb;
        int32_t dy1 = y1 - y0;
        int32_t dy2 = y2 - y1;
        if (y <= y1)
            xb = (dy1 == 0) ? x1 : x0 + (int32_t)((float)(x1 - x0) * (float)(y - y0) / (float)dy1);
        else
            xb = (dy2 == 0) ? x2 : x1 + (int32_t)((float)(x2 - x1) * (float)(y - y1) / (float)dy2);
        if (xa > xb) { int32_t t = xa; xa = xb; xb = t; }
        for (int32_t x = xa; x <= xb; x++) {
            if (x >= 0 && (uint32_t)x < largura)
                pixels[(uint32_t)y * largura + (uint32_t)x] = cor;
        }
    }
}

static void preencher_quad(uint32_t *pixels, uint32_t largura, uint32_t altura,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                            uint32_t cor) {
    preencher_triangulo(pixels, largura, altura, x0, y0, x1, y1, x2, y2, cor);
    preencher_triangulo(pixels, largura, altura, x0, y0, x2, y2, x3, y3, cor);
}

static const uint8_t s_fonte[128][8] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['0'] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2'] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    ['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4'] = {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00},
    ['5'] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6'] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    ['7'] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8'] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9'] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    ['a'] = {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    ['c'] = {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00},
    ['d'] = {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    ['e'] = {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    ['h'] = {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    ['i'] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    ['l'] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['m'] = {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00},
    ['n'] = {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    ['o'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    ['p'] = {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    ['r'] = {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},
    ['s'] = {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    ['t'] = {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00},
    ['u'] = {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
};

static void desenhar_char(uint32_t *pixels, uint32_t largura, uint32_t altura,
                           int32_t x, int32_t y, char c, uint32_t cor, int esc) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) return;
    const uint8_t *g = s_fonte[idx];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (g[row] & (0x80u >> col)) {
                for (int sy = 0; sy < esc; sy++)
                    for (int sx = 0; sx < esc; sx++)
                        escrever_pixel(pixels, largura, altura,
                                       x + col*esc + sx, y + row*esc + sy, cor);
            }
        }
    }
}

static void desenhar_texto(uint32_t *pixels, uint32_t largura, uint32_t altura,
                            int32_t x, int32_t y, const char *txt,
                            uint32_t cor, int esc) {
    int32_t cx = x;
    while (*txt) {
        desenhar_char(pixels, largura, altura, cx, y, *txt, cor, esc);
        cx += 8 * esc + 1;
        txt++;
    }
}

static void desenhar_grade_paredes(uint32_t *pixels, uint32_t largura, uint32_t altura) {
    uint32_t cor_gr  = 0x00163050U;
    uint32_t cor_brd = 0x002a6a9aU;
    int n = 8;

    for (int k = 0; k <= n; k++) {
        float fx = 2.0f * (float)k / (float)n;
        float fz = 1.5f * (float)k / (float)n;
        float fy = 1.2f * (float)k / (float)n;
        int32_t ax0, ay0, ax1, ay1;

        // * chao xz
        projetar_orto(fx, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(fx, 0.0f, 1.5f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);

        projetar_orto(0.0f, 0.0f, fz, largura, altura, &ax0, &ay0);
        projetar_orto(2.0f, 0.0f, fz, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);

        // * parede yz em x zero
        projetar_orto(0.0f, fy, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(0.0f, fy, 1.5f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);

        projetar_orto(0.0f, 0.0f, fz, largura, altura, &ax0, &ay0);
        projetar_orto(0.0f, 1.2f, fz, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);

        // * parede xy em z zero
        projetar_orto(fx, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(fx, 1.2f, 0.0f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);

        projetar_orto(0.0f, fy, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(2.0f, fy, 0.0f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_gr);
    }

    // * bordas do box mais brilhantes
    int32_t ax0, ay0, ax1, ay1;
    projetar_orto(0.0f, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
    projetar_orto(2.0f, 0.0f, 0.0f, largura, altura, &ax1, &ay1);
    desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
    projetar_orto(0.0f, 0.0f, 1.5f, largura, altura, &ax1, &ay1);
    desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
    projetar_orto(0.0f, 1.2f, 0.0f, largura, altura, &ax1, &ay1);
    desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
    projetar_orto(2.0f, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
    projetar_orto(2.0f, 1.2f, 0.0f, largura, altura, &ax1, &ay1);
    desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
    projetar_orto(0.0f, 0.0f, 1.5f, largura, altura, &ax0, &ay0);
    projetar_orto(0.0f, 1.2f, 1.5f, largura, altura, &ax1, &ay1);
    desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
}

static void desenhar_eixos_labels(uint32_t *pixels, uint32_t largura, uint32_t altura) {
    uint32_t cor_ax  = 0x002a6a9aU;
    uint32_t cor_txt = 0x0090e0efU;

    int32_t ox, oy;
    projetar_orto(0.0f, 0.0f, 0.0f, largura, altura, &ox, &oy);

    int32_t xx, xy;
    projetar_orto(2.3f, 0.0f, 0.0f, largura, altura, &xx, &xy);
    desenhar_linha(pixels, largura, altura, ox, oy, xx, xy, cor_ax);
    desenhar_linha(pixels, largura, altura, xx, xy, xx-12, xy-6, cor_ax);
    desenhar_linha(pixels, largura, altura, xx, xy, xx-12, xy+6, cor_ax);
    desenhar_texto(pixels, largura, altura, xx+10, xy-8, "nucleo", cor_txt, 2);

    int32_t zx, zy;
    projetar_orto(0.0f, 0.0f, 1.8f, largura, altura, &zx, &zy);
    desenhar_linha(pixels, largura, altura, ox, oy, zx, zy, cor_ax);
    desenhar_linha(pixels, largura, altura, zx, zy, zx+10, zy-6, cor_ax);
    desenhar_linha(pixels, largura, altura, zx, zy, zx+5,  zy+10, cor_ax);
    desenhar_texto(pixels, largura, altura, zx-130, zy+12, "tempo", cor_txt, 2);

    int32_t yx, yy;
    projetar_orto(0.0f, 1.5f, 0.0f, largura, altura, &yx, &yy);
    desenhar_linha(pixels, largura, altura, ox, oy, yx, yy, cor_ax);
    desenhar_linha(pixels, largura, altura, yx, yy, yx-6, yy+12, cor_ax);
    desenhar_linha(pixels, largura, altura, yx, yy, yx+6, yy+12, cor_ax);
    desenhar_texto(pixels, largura, altura, yx-90, yy-24, "entropia", cor_txt, 2);

    static const char *ticks_y[] = {"0.0", "0.5", "1.0"};
    for (int k = 0; k <= 2; k++) {
        float fy = 1.2f * (float)k / 2.0f;
        int32_t tx, ty;
        projetar_orto(0.0f, fy, 0.0f, largura, altura, &tx, &ty);
        desenhar_linha(pixels, largura, altura, tx-5, ty, tx+5, ty, cor_ax);
        desenhar_texto(pixels, largura, altura, tx-62, ty-6, ticks_y[k], cor_txt, 1);
    }

    for (int k = 0; k <= 4; k++) {
        float fx = 2.0f * (float)k / 4.0f;
        int32_t tx, ty;
        projetar_orto(fx, 0.0f, 0.0f, largura, altura, &tx, &ty);
        char buf[3] = {'c', (char)('0' + k), 0};
        desenhar_texto(pixels, largura, altura, tx-8, ty+14, buf, cor_txt, 1);
    }
}

static void desenhar_colorbar(uint32_t *pixels, uint32_t largura, uint32_t altura) {
    uint32_t cor_txt = 0x0090e0efU;
    uint32_t cor_bd  = 0x002a6a9aU;
    int32_t bx     = (int32_t)largura - 160;
    int32_t by_top = 120;
    int32_t by_bot = (int32_t)altura - 120;
    int32_t bw     = 24;
    int32_t bh     = by_bot - by_top;

    for (int32_t y = by_top; y < by_bot; y++) {
        float v = 1.0f - (float)(y - by_top) / (float)bh;
        uint32_t cor = cor_oceano(v);
        for (int32_t x = bx; x < bx + bw; x++)
            escrever_pixel(pixels, largura, altura, x, y, cor);
    }

    desenhar_linha(pixels, largura, altura, bx,    by_top, bx+bw, by_top, cor_bd);
    desenhar_linha(pixels, largura, altura, bx,    by_bot, bx+bw, by_bot, cor_bd);
    desenhar_linha(pixels, largura, altura, bx,    by_top, bx,    by_bot, cor_bd);
    desenhar_linha(pixels, largura, altura, bx+bw, by_top, bx+bw, by_bot, cor_bd);

    static const char *ticks[] = {"1.0", "0.5", "0.0"};
    for (int k = 0; k <= 2; k++) {
        int32_t ty = by_top + bh * k / 2;
        desenhar_linha(pixels, largura, altura, bx+bw, ty, bx+bw+8, ty, cor_bd);
        desenhar_texto(pixels, largura, altura, bx+bw+12, ty-4, ticks[k], cor_txt, 1);
    }

    desenhar_texto(pixels, largura, altura, bx-8, by_top-24, "shannon", cor_txt, 1);
}

void renderizador_renderizar_quadro(GradeEntropia *grade, EstadoCamera *camera) {
    if (!s_inicializado) return;
    (void)camera;

    uint32_t *pixels  = s_framebuffer.pixels;
    uint32_t  largura = s_framebuffer.largura;
    uint32_t  altura  = s_framebuffer.altura;

    uint32_t *p = pixels;
    uint32_t *fim = pixels + (size_t)largura * altura;
    while (p < fim) *p++ = 0x0003001CU;

    renderizador_atualizar_grid(grade);

    desenhar_grade_paredes(pixels, largura, altura);

    float escala_x = 2.0f  / (float)(GRID_N - 1);
    float escala_z = 1.5f  / (float)(GRID_N - 1);
    float escala_y = 1.2f;

    for (int soma = (GRID_N - 2) * 2; soma >= 0; soma--) {
        int i_min = soma - (GRID_N - 2);
        int i_max = soma;
        if (i_min < 0)        i_min = 0;
        if (i_max > GRID_N-2) i_max = GRID_N - 2;

        for (int i = i_max; i >= i_min; i--) {
            int j = soma - i;
            if (j < 0 || j >= GRID_N - 1) continue;

            float wx0 = (float)j       * escala_x;
            float wx1 = (float)(j + 1) * escala_x;
            float wz0 = (float)i       * escala_z;
            float wz1 = (float)(i + 1) * escala_z;

            float v00 = s_grid_suavizado[i][j];
            float v01 = s_grid_suavizado[i][j + 1];
            float v10 = s_grid_suavizado[i + 1][j];
            float v11 = s_grid_suavizado[i + 1][j + 1];

            float avg = (v00 + v01 + v10 + v11) * 0.25f;
            uint32_t cor = cor_oceano(avg);

            int32_t px00, py00, px01, py01, px10, py10, px11, py11;
            projetar_orto(wx0, v00 * escala_y, wz0, largura, altura, &px00, &py00);
            projetar_orto(wx1, v01 * escala_y, wz0, largura, altura, &px01, &py01);
            projetar_orto(wx0, v10 * escala_y, wz1, largura, altura, &px10, &py10);
            projetar_orto(wx1, v11 * escala_y, wz1, largura, altura, &px11, &py11);

            preencher_quad(pixels, largura, altura,
                           px00, py00, px01, py01, px11, py11, px10, py10, cor);

            uint32_t borda = 0x00091520U;
            desenhar_linha(pixels, largura, altura, px00, py00, px01, py01, borda);
            desenhar_linha(pixels, largura, altura, px00, py00, px10, py10, borda);
        }
    }

    // * bordas frontais do box desenhadas sobre a superficie
    {
        uint32_t cor_brd = 0x002a6a9aU;
        int32_t ax0, ay0, ax1, ay1;
        projetar_orto(0.0f, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(2.0f, 0.0f, 0.0f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
        projetar_orto(0.0f, 0.0f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(0.0f, 0.0f, 1.5f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
        projetar_orto(0.0f, 1.2f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(0.0f, 1.2f, 1.5f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
        projetar_orto(0.0f, 1.2f, 0.0f, largura, altura, &ax0, &ay0);
        projetar_orto(2.0f, 1.2f, 0.0f, largura, altura, &ax1, &ay1);
        desenhar_linha(pixels, largura, altura, ax0, ay0, ax1, ay1, cor_brd);
    }

    desenhar_eixos_labels(pixels, largura, altura);
    desenhar_colorbar(pixels, largura, altura);

    uint32_t cor_titulo = 0x004488aaU;
    desenhar_texto(pixels, largura, altura,
                   (int32_t)largura/2 - 88, 30, "entropia 3d", cor_titulo, 2);
}

void renderizador_configurar_mapeamento_eixo(Eixo eixo, MetricaEixo metrica) {
    if (eixo == EIXO_X || eixo == EIXO_Y || eixo == EIXO_Z)
        s_mapeamentos[eixo].metrica = metrica;
}

Framebuffer *renderizador_obter_framebuffer(void) {
    if (!s_inicializado) return (void *)0;
    return &s_framebuffer;
}
