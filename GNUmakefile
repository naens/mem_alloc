CFLAGS=-g -O0 -Wall -fstrict-aliasing -Wstrict-aliasing -Wconversion

.PHONY: all
all: mem_test mem_test32

mem_test: mem_test.c mem.c
	gcc $(CFLAGS) mem_test.c mem.c -o mem_test

mem_test32: mem_test.c mem.c
	gcc $(CFLAGS) -m32 mem_test.c mem.c -o mem_test32

.PHONY: clean
clean:
	rm -f *.o mem_test mem_test32

mem.pdf: mem.c
	find . -name mem.c | xargs enscript --color=0 -C -Ecpp -fCourier10 -o - | ps2pdf - code.pdf
	booklet code.pdf
	mv code-booklet.pdf mem.pdf
	rm code.pdf
