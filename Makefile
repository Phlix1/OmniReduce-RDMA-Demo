.PHONY: clean

#CFLAGS  := -Wall -g -DDEBUG
CFLAGS  := -Wall -g
LD      := cc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client server

all: ${APPS}

client: common.o client.o
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

server: common.o server.o
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
