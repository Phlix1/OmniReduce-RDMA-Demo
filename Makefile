.PHONY: clean

#CFLAGS  := -Wall -g -DDEBUG -O3 -std=c++11
#CFLAGS  := -Wall -g -O3 -DSTATISTICS
CFLAGS  := -Wall -g -O3 -std=c++11
LD      := g++
#LD      := mpicxx
LDLIBS  := ${LDLIBS} -libverbs -lpthread

APPS    := client server

all: ${APPS}

client: common.cc client.cc
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

server: common.cc server.cc
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
