CC      = gcc
NASM    = nasm
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Iinclude \
          -DTESTE_EVENTOS \
          $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2) -lm

ASM_OBJ = asm/cpuid_asm.o

SRCS = src/buffer_log.c \
       src/coletor_cpu.c \
       src/calculador_entropia.c \
       src/renderizador_3d.c \
       src/gerenciador_interacao.c \
       src/serializador_snapshot.c \
       src/micro_kernel.c \
       src/visualizador_sdl.c

BIN = entropia3d

.PHONY: all clean

all: $(BIN)

asm/cpuid_asm.o: asm/cpuid_asm.asm
	$(NASM) -f elf64 -o $@ $<

$(BIN): $(ASM_OBJ) $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJ) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(BIN) $(ASM_OBJ)
