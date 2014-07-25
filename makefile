CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s
#-O1 -pg -g -rdynamic
LDFLAGS =

HDR = color.h conf.h dcs.h draw.h function.h \
	gifsave89.h osc.h parse.h pseudo.h recterm.h terminal.h util.h \
	glyph/mplus.h glyph/milkjf.h
SRC = recterm.c
DST = recterm

all:  $(DST)

recterm: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(DST)
