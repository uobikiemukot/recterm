CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s
#-O1 -pg -g -rdynamic
LDFLAGS =

HDR = ../recterm.h ../conf.h ../util.h ../pseudo.h \
	../terminal.h ../function.h ../osc.h \
	../dcs.h ../parse.h ../gifsave89.h 
DST = rec recfilter rec2gif

all:  $(DST)

recfilter: recfilter.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

rec: rec.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

rec2gif: rec2gif.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(DST)
