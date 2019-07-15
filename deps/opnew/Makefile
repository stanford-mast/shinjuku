
CX = c++
CFLAGS = -g -O2
LIBS =

all: dest/libnew.la newtest
.PHONY: all

test: all
	LD_PRELOAD=$(PWD)/dest/libnew.so ./newtest
.PHONY: test

libnew.la: new.lo
	libtool --mode=link $(CXX) -o $@ new.lo -rpath $(PWD)/dest

dest/libnew.la: libnew.la
	mkdir -p $(PWD)/dest
	libtool --mode=install cp libnew.la $(PWD)/dest

newtest: newtest.o
	$(CXX) -rdynamic -o $@ newtest.o

.SUFFIXES: .cc .o .lo
.cc.o:
	$(CXX) $(CFLAGS) -c -g -rdynamic $<
.cc.lo:
	libtool --mode=compile $(CXX) $(CFLAGS) -c $<

.PHONY: clean
clean:
	rm -rf .libs dest
	rm -f $$(cat .gitignore) .gitignore~
