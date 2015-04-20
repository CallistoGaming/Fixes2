# This file demonstrates how to compile the fixes project on Linux.
#
# To compile FIXES do:
#
# make FIXES
#

GPP = g++
GCC = gcc
FIXES_OUTFILE = "../plugins/fixes2.so"

COMPILE_FLAGS = -m32 -fPIC -c -O3 -w -D LINUX -D PROJECT_NAME=\"fixes2\" -I ./SDK/amx/

FIXES = -D FIXES $(COMPILE_FLAGS)

all: FIXES

clean:
	-rm -f *~ *.o *.so

FIXES: clean
	$(GPP) $(FIXES) ./SDK/amx/*.c -lrt
	$(GPP) $(FIXES) ./SDK/*.cpp -lrt
	$(GPP) $(FIXES) *.cpp -lrt
	$(GPP) -m32 -O2 -fshort-wchar -shared -o $(FIXES_OUTFILE) -lrt *.o
