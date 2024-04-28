include ../common.mak

PROGRAM=$(BIN)/libbeepy$(SOEXT)

OBJS=libbeepy.o

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(BIN) -lpit -lpumpkin

clean:
	rm -f $(PROGRAM) $(OBJS)
