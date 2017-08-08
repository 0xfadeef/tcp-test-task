PROGS	= client server
OBJS	= $(addsuffix .o,$(PROGS))

#CFLAGS = -g -Wall
LDFLAGS = -pthread

.PHONY: all clean

all: $(PROGS)

$(OBJS): defs.h

clean:
	-rm -v $(PROGS) $(OBJS)
