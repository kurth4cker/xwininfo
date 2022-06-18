OBJ = clientwin.o dsimple.o xwininfo.o
LDLIBS ?= -lxcb-shape -lxcb-icccm -lxcb

all: xwininfo
xwininfo: $(OBJ)
$(OBJ): config.h clientwin.h dsimple.h
clean:
	$(RM) $(OBJ) xwininfo
