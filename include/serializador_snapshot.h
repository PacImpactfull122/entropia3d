#ifndef SERIALIZADOR_SNAPSHOT_H
#define SERIALIZADOR_SNAPSHOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cabecalho_snapshot.h"
#include "grade_entropia.h"
#include "resultado.h"

// * versao atual do formato binario de snapshot
#define VERSAO_FORMATO_SNAPSHOT ((uint16_t)1)

ResultadoSerializacao   serializar(const GradeEntropia *grade, uint8_t *destino, size_t tamanho_destino);
ResultadoDeserializacao deserializar(const uint8_t *origem, size_t tamanho, GradeEntropia *grade_destino);
uint32_t                calcular_crc32(const uint8_t *dados, size_t tamanho);
bool                    validar_checksum(const CabecalhoSnapshot *cabecalho, const uint8_t *dados_celulas, size_t tamanho_dados);

#endif /* SERIALIZADOR_SNAPSHOT_H */
