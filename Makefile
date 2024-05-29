CC=gcc
# CFLAGS=-g -Wall -I/usr/include/x86_64-linux-gnu
CFLAGS=-g -Wall
LIBS0=-lavformat -lavcodec -lavutil
LIBS1=-lswresample
LIBS2=-lswresample -lswscale
EXECUTABLES=decode_audio decaud0 transcode_aac taac0 tmp30


# ok this is the minimal compilation prog
decode_audio: decode_audio.c
	${CC} ${CFLAGS} -o $@ $^ ${LIBS0}


# ok I try fiddling
decaud0: decaud0.c
	${CC} ${CFLAGS} -o $@ $^ ${LIBS0}

transcode_aac: transcode_aac.c
	${CC} ${CFLAGS} -o $@ $^ ${LIBS0} ${LIBS1}
taac0: taac0.c
	${CC} ${CFLAGS} -o $@ $^ ${LIBS0} ${LIBS1}
tmp30: tmp30.c
	${CC} ${CFLAGS} -o $@ $^ ${LIBS0} ${LIBS1}

.PHONY: clean

clean:
	rm -f ${EXECUTABLES}
