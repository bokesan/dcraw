OPTIMIZE = -march=native -Ofast
CFLAGS = -Wall $(OPTIMIZE) -DNO_JASPER -I/opt/homebrew/include -L/opt/homebrew/lib

dcraw: dcraw.c
	$(CC) $(CFLAGS) -o dcraw dcraw.c -lm -ljpeg -llcms2

clean:
	$(RM) dcraw
