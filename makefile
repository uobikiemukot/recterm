CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s
#-O1 -pg -g -rdynamic
LDFLAGS =

HDR = recterm.h conf.h util.h pseudo.h \
    terminal.h function.h osc.h dcs.h \
    parse.h gifsave89.h gif.h
DST = recterm recfilter rec2gif rec

all:  $(DST)

recterm: recterm.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

recfilter: tools/recfilter.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

rec2gif: tools/rec2gif.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

rec: tools/rec.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

recterm: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(DST)
