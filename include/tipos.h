#ifndef TIPOS_H
#define TIPOS_H

#include <stdint.h>

typedef struct {
    float x;
    float y;
    float z;
} Vec3f;

typedef struct {
    int32_t x;
    int32_t y;
} Vec2i;

typedef struct {
    uint32_t largura;
    uint32_t altura;
} Resolucao;

// * raio usado no ray casting para selecao de minitops
typedef struct {
    Vec3f origem;
    Vec3f direcao;
} Raio;

#endif /* TIPOS_H */
