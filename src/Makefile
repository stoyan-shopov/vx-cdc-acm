BINARY = usb-cdc-acm

OPENCM3_DIR = ../libopencm3/
LDSCRIPT = stm32f103.ld

CFLAGS += -I./sforth/

# sforth engine stack depth, in words
STACK_DEPTH ?= 32
# sforth engine core size, in words
CORE_CELLS_COUNT ?= 2 * 1024

CFLAGS += -DENGINE_32BIT -DENABLE_HDUMP=0 \
	 -DSTACK_DEPTH="$(STACK_DEPTH)" \
	 -DCORE_CELLS_COUNT="$(CORE_CELLS_COUNT)"

SFORTH_OBJECTS = sforth/engine.o sforth/sf-arch-default.o  \
		sforth/sf-opt-file.o \
		sforth/sf-opt-prog-tools.o \
		sforth/sf-opt-string.o

OBJS += vx-sf-arch.o
OBJS += $(SFORTH_OBJECTS)

include ../libopencm3.target.mk
