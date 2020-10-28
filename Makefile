.PHONY: clean

#CFLAGS  := -Wall -g -DDEBUG -O3
#CFLAGS  := -Wall -g -O3 -DSTATISTICS
CFLAGS  := -Wall -g -O3
LD      := g++
LDLIBS  := ${LDLIBS} -libverbs -lpthread

APPS    := client server

all: ${APPS}

client: common.cc client.cc
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

server: common.cc server.cc
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
