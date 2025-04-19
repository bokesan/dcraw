OPTIMIZE = -march=native -O3 -ffast-math
CFLAGS = -Wall $(OPTIMIZE) -DNO_JASPER -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib

dcraw: dcraw.o panasonic.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o dcraw dcraw.o panasonic.o -lm -ljpeg -llcms2

dcraw.o : panasonic.h
panasonic.o : bits.h panasonic.h

bits_test: bits_test.c bits.h
	$(CC) $(CFLAGS) -o bits_test bits_test.c

test: bits_test
	./bits_test

clean:
	$(RM) dcraw bits_test *.o
