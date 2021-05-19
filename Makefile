ifndef V
QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CXX$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR);
QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR);
endif

CFLAGS?=-std=c++0x $(OPTIMIZATION) -Wall $(PROF)
DEBUG?=-g -ggdb
CCOPT= $(CFLAGS) $(ARCH) $(PROF)

all: libbpt.a

test: 
	@-rm bpt_unit_test
	$(MAKE) TEST="-DUNIT_TEST" bpt_unit_test
	./bpt_unit_test

%.o: %.cc
	$(QUIET_CC)$(CXX) -o $@ -c $(CFLAGS) $(TEST) $(DEBUG) $(COMPILE_TIME) $<

libbpt.a: bpt.o
	ar -r libbpt.a bpt.o

bpt_unit_test:
	$(QUIET_LINK)$(CXX) -o bpt_unit_test $(CCOPT) $(DEBUG) util/unit_test.cc bpt.cc $(TEST) $(CCLINK) 

clean:
	rm -rf libbpt.a bpt.o bpt_unit_test

# Deps (use make dep to generate this)
bpt.o: bpt.cc bpt.h predefined.h