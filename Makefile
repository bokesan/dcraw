OPTIMIZE = -march=native -O3 -ffast-math
CFLAGS = -Wall $(OPTIMIZE) -DNO_JASPER -I/opt/homebrew/include -L/opt/homebrew/lib

dcraw: dcraw.c
	$(CC) $(CFLAGS) -o dcraw dcraw.c -lm -ljpeg -llcms2

bits_test: bits_test.c bits.h
	$(CC) $(CFLAGS) -o bits_test bits_test.c

test: bits_test
	./bits_test

clean:
	$(RM) dcraw bits_test *.o
