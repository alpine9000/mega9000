HOST_CC ?= cc
M68KMAKE_EXE ?= m68kmake

all : m68kops.c

m68kops.c : $(M68KMAKE_EXE)
	./$(M68KMAKE_EXE)

$(M68KMAKE_EXE) : m68kmake.c m68k_in.c
	$(HOST_CC) -o $@ $<


clean:
	$(RM) m68kops.c m68kops.h m68kmake m68kmake.exe *.o
