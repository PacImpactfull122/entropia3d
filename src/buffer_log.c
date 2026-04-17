#include "buffer_log.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// * contador monotono substitui clock de so, incrementado a cada entrada registrada
static uint64_t contador_timestamp = 0;

static EntradaLog entradas[CAPACIDADE_BUFFER_LOG];

// * indice_cabeca aponta para a entrada mais antiga quando o buffer esta cheio
static size_t indice_cabeca = 0;
static size_t indice_cauda  = 0;
static size_t contagem      = 0;

static void escrever_entrada(const EntradaLog *entrada) {
    entradas[indice_cauda] = *entrada;
    indice_cauda = (indice_cauda + 1U) % CAPACIDADE_BUFFER_LOG;

    if (contagem < CAPACIDADE_BUFFER_LOG) {
        contagem++;
    } else {
        // ! buffer cheio, sobrescreve a entrada mais antiga avancando a cabeca
        indice_cabeca = (indice_cabeca + 1U) % CAPACIDADE_BUFFER_LOG;
    }
}

void registrar(NivelLog nivel, const char *subsistema, const char *mensagem) {
    EntradaLog entrada;
    memset(&entrada, 0, sizeof(entrada));

    entrada.timestamp          = contador_timestamp++;
    entrada.nivel              = nivel;
    entrada.codigo_erro        = 0;
    entrada.endereco_instrucao = 0;

    if (subsistema != NULL) {
        strncpy((char *)entrada.subsistema, subsistema, sizeof(entrada.subsistema) - 1U);
    }

    if (mensagem != NULL) {
        strncpy((char *)entrada.mensagem, mensagem, sizeof(entrada.mensagem) - 1U);
    }

    escrever_entrada(&entrada);
}

void registrar_erro(const char *subsistema, uint32_t codigo, uint64_t endereco) {
    EntradaLog entrada;
    memset(&entrada, 0, sizeof(entrada));

    entrada.timestamp          = contador_timestamp++;
    entrada.nivel              = NIVEL_ERRO;
    entrada.codigo_erro        = codigo;
    entrada.endereco_instrucao = endereco;

    if (subsistema != NULL) {
        strncpy((char *)entrada.subsistema, subsistema, sizeof(entrada.subsistema) - 1U);
    }

    // * mensagem permanece vazia conforme especificado para registrar_erro

    escrever_entrada(&entrada);
}

void buffer_log_resetar(void) {
    memset(entradas, 0, sizeof(entradas));
    indice_cabeca      = 0;
    indice_cauda       = 0;
    contagem           = 0;
    contador_timestamp = 0;
}

size_t obter_entradas(EntradaLog *destino, size_t max) {
    if (destino == NULL || max == 0U) {
        return 0U;
    }

    size_t copiadas = (contagem < max) ? contagem : max;

    for (size_t i = 0U; i < copiadas; i++) {
        size_t indice = (indice_cabeca + i) % CAPACIDADE_BUFFER_LOG;
        destino[i] = entradas[indice];
    }

    return copiadas;
}
