# Makefile — bahasa Inti (interpreter + compiler native)
CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -fno-plt

# interpreter: lex + parse + vm + cli
INTI_SRC := src/lex.c src/parse.c src/vm.c src/main.c
# compiler native: lex + parse (dibagi) + backend x86-64 + elf + cli
CC_SRC   := src/lex.c src/parse.c src/encode.c src/runtime.c \
	    src/codegen.c src/elfw.c src/inticc.c
HDR := src/inti.h src/cg.h

inti: $(INTI_SRC) src/inti.h
	$(CC) $(CFLAGS) -o $@ $(INTI_SRC)

inti-cc: $(CC_SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(CC_SRC)

test: inti inti-cc
	bash tests/run_tests.sh
	bash tests/run_tests_cc.sh
	bash tests/run_examples_cc.sh
	bash tests/audit_native.sh

audit: inti-cc
	bash tests/audit_native.sh

bench: inti
	bash bench/run_bench.sh

bench-native: inti inti-cc
	bash bench/run_bench_native.sh

intios: inti-cc
	bash intios/build.sh

intios-test: intios
	bash intios/test_qemu.sh

clean:
	rm -f inti inti-cc bench/sum_c bench/fib_c bench/mem_c
	rm -rf intios/build intios/intios.img

.PHONY: test audit bench bench-native intios intios-test clean
