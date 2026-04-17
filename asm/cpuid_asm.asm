; * abi system v amd64: argumentos em rdi rsi rdx rcx r8 r9, retorno em rax
; * rbx e callee-saved, cpuid modifica ebx, salvar e restaurar obrigatorio

section .text

; * asm_cpuid(uint32_t folha, uint32_t sub,
; *           uint32_t *p_eax, uint32_t *p_ebx,
; *           uint32_t *p_ecx, uint32_t *p_edx)
; * rdi=folha  rsi=sub  rdx=p_eax  rcx=p_ebx  r8=p_ecx  r9=p_edx
global asm_cpuid
asm_cpuid:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    ; * preserva ponteiros antes de cpuid sobrescrever rcx e rdx
    mov     r12, rdx
    mov     r13, rcx
    mov     r14, r8
    mov     r15, r9

    mov     eax, edi
    mov     ecx, esi
    cpuid

    mov     dword [r12], eax
    mov     dword [r13], ebx
    mov     dword [r14], ecx
    mov     dword [r15], edx

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; * asm_rdtsc() -> uint64_t
global asm_rdtsc
asm_rdtsc:
    rdtsc
    ; * edx contem a parte alta, eax a parte baixa do contador
    shl     rdx, 32
    or      rax, rdx
    ret

; * asm_rdmsr(uint32_t endereco) -> uint64_t
; ! rdmsr gera gp fault se o msr nao existir ou se executado em userspace sem permissao
global asm_rdmsr
asm_rdmsr:
    mov     ecx, edi
    rdmsr
    shl     rdx, 32
    or      rax, rdx
    ret

; * asm_cpuid_max_leaf(uint32_t folha_base) -> uint32_t
; * retorna o max leaf suportado para a folha base informada
; * rdi=folha_base
global asm_cpuid_max_leaf
asm_cpuid_max_leaf:
    push    rbx
    mov     eax, edi
    xor     ecx, ecx
    cpuid
    ; * eax contem o max leaf apos cpuid com a folha base informada
    pop     rbx
    ret

; * asm_cpuid_fabricante(char destino[13])
; * preenche o vendor string via cpuid leaf zero
global asm_cpuid_fabricante
asm_cpuid_fabricante:
    push    rbx
    push    r12
    mov     r12, rdi
    xor     eax, eax
    xor     ecx, ecx
    cpuid
    ; * ordem ebx edx ecx forma o vendor string
    mov     dword [r12 + 0], ebx
    mov     dword [r12 + 4], edx
    mov     dword [r12 + 8], ecx
    mov     byte  [r12 + 12], 0
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_marca_leaf(uint32_t folha, char destino_16[16])
; * preenche uma das tres folhas da brand string
global asm_cpuid_marca_leaf
asm_cpuid_marca_leaf:
    push    rbx
    push    r12
    mov     r12, rsi
    mov     eax, edi
    xor     ecx, ecx
    cpuid
    mov     dword [r12 + 0],  eax
    mov     dword [r12 + 4],  ebx
    mov     dword [r12 + 8],  ecx
    mov     dword [r12 + 12], edx
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_leaf1(uint32_t *p_eax, uint32_t *p_ebx, uint32_t *p_ecx, uint32_t *p_edx)
; * executa cpuid leaf 1, retorna os quatro registradores
; * rdi=p_eax  rsi=p_ebx  rdx=p_ecx  rcx=p_edx
global asm_cpuid_leaf1
asm_cpuid_leaf1:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rdi
    mov     r13, rsi
    mov     r14, rdx
    mov     r15, rcx
    mov     eax, 1
    xor     ecx, ecx
    cpuid
    mov     dword [r12], eax
    mov     dword [r13], ebx
    mov     dword [r14], ecx
    mov     dword [r15], edx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_leaf4(uint32_t sub, uint32_t *p_eax, uint32_t *p_ebx,
; *                uint32_t *p_ecx, uint32_t *p_edx)
; * executa cpuid leaf 4 com sub-leaf para topologia de cache
; * rdi=sub  rsi=p_eax  rdx=p_ebx  rcx=p_ecx  r8=p_edx
global asm_cpuid_leaf4
asm_cpuid_leaf4:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rsi
    mov     r13, rdx
    mov     r14, rcx
    mov     r15, r8
    mov     eax, 4
    mov     ecx, edi        ; sub-leaf
    cpuid
    mov     dword [r12], eax
    mov     dword [r13], ebx
    mov     dword [r14], ecx
    mov     dword [r15], edx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_leaf7(uint32_t *p_eax, uint32_t *p_ebx, uint32_t *p_ecx, uint32_t *p_edx)
; * executa cpuid leaf 7 sub 0 para extended features
; * rdi=p_eax  rsi=p_ebx  rdx=p_ecx  rcx=p_edx
global asm_cpuid_leaf7
asm_cpuid_leaf7:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rdi
    mov     r13, rsi
    mov     r14, rdx
    mov     r15, rcx
    mov     eax, 7
    xor     ecx, ecx
    cpuid
    mov     dword [r12], eax
    mov     dword [r13], ebx
    mov     dword [r14], ecx
    mov     dword [r15], edx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_leaf16(uint32_t *p_eax, uint32_t *p_ebx, uint32_t *p_ecx)
; * executa cpuid leaf 0x16 para frequencias base boost e barramento
; * rdi=p_eax  rsi=p_ebx  rdx=p_ecx
global asm_cpuid_leaf16
asm_cpuid_leaf16:
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     r12, rdi
    mov     r13, rsi
    mov     r14, rdx
    mov     eax, 0x16
    xor     ecx, ecx
    cpuid
    mov     dword [r12], eax
    mov     dword [r13], ebx
    mov     dword [r14], ecx
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_leaf_a(uint32_t *p_eax) -> contadores pmu
; * executa cpuid leaf 0x0a, retorna apenas eax com info de contadores
; * rdi=p_eax
global asm_cpuid_leaf_a
asm_cpuid_leaf_a:
    push    rbx
    push    r12
    mov     r12, rdi
    mov     eax, 0x0a
    xor     ecx, ecx
    cpuid
    mov     dword [r12], eax
    pop     r12
    pop     rbx
    ret

; * asm_cpuid_ext8(uint32_t *p_ecx)
; * executa cpuid leaf 0x80000008, retorna ecx com contagem de nucleos
; * rdi=p_ecx
global asm_cpuid_ext8
asm_cpuid_ext8:
    push    rbx
    push    r12
    mov     r12, rdi
    mov     eax, 0x80000008
    xor     ecx, ecx
    cpuid
    mov     dword [r12], ecx
    pop     r12
    pop     rbx
    ret
