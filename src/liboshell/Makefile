include ../common.mak

PROGRAM=$(BIN)/liboshell$(SOEXT)

ifeq ($(OSNAME),GNU/Linux)
OSHELL=oshell.o
endif

ifeq ($(OSNAME),Beepy)
OSHELL=oshell.o
endif

ifeq ($(OSNAME),Xcode)
OSHELL=oshell.o
endif

ifeq ($(OSNAME),Msys)
OSHELL=wshell.o
endif

OBJS=liboshell.o $(OSHELL)

$(PROGRAM): $(OBJS)
	$(CC) -shared -o $(PROGRAM) $(OBJS) -L$(BIN) -lpit -lutil

clean:
	rm -f $(PROGRAM) $(OBJS)
