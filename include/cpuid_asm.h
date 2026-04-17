#ifndef CPUID_ASM_H
#define CPUID_ASM_H

#include <stdint.h>

void     asm_cpuid(uint32_t folha, uint32_t sub,
                   uint32_t *p_eax, uint32_t *p_ebx,
                   uint32_t *p_ecx, uint32_t *p_edx);

uint64_t asm_rdtsc(void);
uint64_t asm_rdmsr(uint32_t endereco);

uint32_t asm_cpuid_max_leaf(uint32_t folha_base);
void     asm_cpuid_fabricante(char destino[13]);
void     asm_cpuid_marca_leaf(uint32_t folha, char destino_16[16]);

void     asm_cpuid_leaf1(uint32_t *p_eax, uint32_t *p_ebx,
                         uint32_t *p_ecx, uint32_t *p_edx);
void     asm_cpuid_leaf4(uint32_t sub, uint32_t *p_eax, uint32_t *p_ebx,
                         uint32_t *p_ecx, uint32_t *p_edx);
void     asm_cpuid_leaf7(uint32_t *p_eax, uint32_t *p_ebx,
                         uint32_t *p_ecx, uint32_t *p_edx);
void     asm_cpuid_leaf16(uint32_t *p_eax, uint32_t *p_ebx, uint32_t *p_ecx);
void     asm_cpuid_leaf_a(uint32_t *p_eax);
void     asm_cpuid_ext8(uint32_t *p_ecx);

#endif /* CPUID_ASM_H */
