#include "serializador_snapshot.h"
#include "codigos_erro.h"
#include "buffer_log.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// * tabela de lookup para crc32 com polinomio ieee refletido
static uint32_t tabela_crc32[256];
static bool     tabela_inicializada = false;

static void inicializar_tabela_crc32(void) {
    uint32_t i;
    uint32_t j;
    uint32_t crc;

    for (i = 0; i < 256; i++) {
        crc = i;
        for (j = 0; j < 8; j++) {
            if (crc & 1u) {
                // * polinomio refletido equivale ao crc32 ieee
                crc = (crc >> 1) ^ (uint32_t)0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        tabela_crc32[i] = crc;
    }
    tabela_inicializada = true;
}

uint32_t calcular_crc32(const uint8_t *dados, size_t tamanho) {
    uint32_t crc;
    size_t   i;

    if (!tabela_inicializada) {
        inicializar_tabela_crc32();
    }

    crc = (uint32_t)0xFFFFFFFF;
    for (i = 0; i < tamanho; i++) {
        crc = tabela_crc32[(crc ^ dados[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ (uint32_t)0xFFFFFFFF;
}

bool validar_checksum(const CabecalhoSnapshot *cabecalho, const uint8_t *dados_celulas, size_t tamanho_dados) {
    uint32_t crc_calculado;

    if (cabecalho == NULL || dados_celulas == NULL) {
        return false;
    }

    crc_calculado = calcular_crc32(dados_celulas, tamanho_dados);
    return crc_calculado == cabecalho->checksum_crc32;
}

ResultadoSerializacao serializar(const GradeEntropia *grade, uint8_t *destino, size_t tamanho_destino) {
    ResultadoSerializacao resultado;
    CabecalhoSnapshot     cabecalho;
    uint32_t              contagem_ativas;
    size_t                tamanho_necessario;
    size_t                offset_celulas;
    uint32_t              i;
    uint8_t              *ptr_celulas;

    resultado.bytes_escritos = 0;
    resultado.checksum       = 0;
    resultado.sucesso        = false;
    resultado.codigo_erro    = 0;

    if (grade == NULL || destino == NULL) {
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    contagem_ativas = 0;
    for (i = 0; i < MAX_CELULAS; i++) {
        if (grade->celulas[i].ativa) {
            contagem_ativas++;
        }
    }

    tamanho_necessario = sizeof(CabecalhoSnapshot) + (size_t)contagem_ativas * sizeof(CelulaEntropia);

    if (tamanho_destino < tamanho_necessario) {
        resultado.codigo_erro = E_BUFFER_OVERFLOW;
        return resultado;
    }

    // * preencher cabecalho com valores fixos, checksum sera calculado apos gravar celulas
    memset(&cabecalho, 0, sizeof(CabecalhoSnapshot));
    cabecalho.magico            = MAGICO_SNAPSHOT;
    cabecalho.versao_formato    = VERSAO_FORMATO_SNAPSHOT;
    cabecalho.contagem_celulas  = contagem_ativas;
    cabecalho.timestamp_criacao = 0;
    cabecalho.tamanho_total     = (uint32_t)tamanho_necessario;

    offset_celulas = sizeof(CabecalhoSnapshot);
    ptr_celulas    = destino + offset_celulas;

    size_t offset_escrita = 0;
    for (i = 0; i < MAX_CELULAS; i++) {
        if (grade->celulas[i].ativa) {
            memcpy(ptr_celulas + offset_escrita, &grade->celulas[i], sizeof(CelulaEntropia));
            offset_escrita += sizeof(CelulaEntropia);
        }
    }

    // ! checksum calculado sobre os dados das celulas, nao inclui o cabecalho
    cabecalho.checksum_crc32 = calcular_crc32(ptr_celulas, offset_escrita);

    memcpy(destino, &cabecalho, sizeof(CabecalhoSnapshot));

    resultado.bytes_escritos = tamanho_necessario;
    resultado.checksum       = cabecalho.checksum_crc32;
    resultado.sucesso        = true;
    resultado.codigo_erro    = 0;

    return resultado;
}

ResultadoDeserializacao deserializar(const uint8_t *origem, size_t tamanho, GradeEntropia *grade_destino) {
    ResultadoDeserializacao resultado;
    const CabecalhoSnapshot *cabecalho;
    const uint8_t           *ptr_celulas;
    size_t                   tamanho_celulas;
    uint32_t                 i;

    resultado.celulas_restauradas = 0;
    resultado.sucesso             = false;
    resultado.codigo_erro         = 0;

    if (origem == NULL || grade_destino == NULL) {
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    if (tamanho < sizeof(CabecalhoSnapshot)) {
        registrar_erro("serializador", E_SNAPSHOT_CORROMPIDO, 0);
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    cabecalho = (const CabecalhoSnapshot *)origem;

    if (cabecalho->magico != MAGICO_SNAPSHOT) {
        registrar_erro("serializador", E_SNAPSHOT_CORROMPIDO, 0);
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    if (tamanho < (size_t)cabecalho->tamanho_total) {
        registrar_erro("serializador", E_SNAPSHOT_CORROMPIDO, 0);
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    ptr_celulas     = origem + sizeof(CabecalhoSnapshot);
    tamanho_celulas = (size_t)cabecalho->contagem_celulas * sizeof(CelulaEntropia);

    if (!validar_checksum(cabecalho, ptr_celulas, tamanho_celulas)) {
        registrar_erro("serializador", E_SNAPSHOT_CORROMPIDO, 0);
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    if (cabecalho->contagem_celulas > MAX_CELULAS) {
        registrar_erro("serializador", E_SNAPSHOT_CORROMPIDO, 0);
        resultado.codigo_erro = E_SNAPSHOT_CORROMPIDO;
        return resultado;
    }

    // * zerar grade antes de restaurar para que posicoes nao preenchidas fiquem inativas
    memset(grade_destino->celulas, 0, sizeof(grade_destino->celulas));

    for (i = 0; i < cabecalho->contagem_celulas; i++) {
        memcpy(&grade_destino->celulas[i], ptr_celulas + (size_t)i * sizeof(CelulaEntropia), sizeof(CelulaEntropia));
    }

    grade_destino->contagem_ativa = cabecalho->contagem_celulas;

    resultado.celulas_restauradas = cabecalho->contagem_celulas;
    resultado.sucesso             = true;
    resultado.codigo_erro         = 0;

    return resultado;
}
