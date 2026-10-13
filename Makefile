CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

unico: unico.c
	$(CC) $(CFLAGS) -o $@ unico.c -lm

piccolo: unico.c
	$(CC) -std=c99 -Os -s -o unico unico.c -lm

test: unico
	./unico test/test.uc
	sh test/cli.sh ./unico

stress: unico.c
	$(CC) -std=c99 -O1 -g -DUC_GC_STRESS -fsanitize=address,undefined -o unico-stress unico.c -lm
	ASAN_OPTIONS=detect_leaks=0 ./unico-stress test/test.uc

pulisci:
	rm -f unico unico-stress

.PHONY: piccolo test stress pulisci
