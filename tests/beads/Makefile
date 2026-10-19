# Beads port tests: render every scenario through the original float code and
# the fixed-point port, then compare.
#
#   make EURORACK=/path/to/eurorack      (eurorack with stmlib cloned inside it)
#   make fetch                           (clones both into ./build/eurorack)

EURORACK ?= build/eurorack
CORE = ../../core/beads
REF_SRC = $(EURORACK)/beads/dsp/trigger_generator.cc $(EURORACK)/beads/dsp/delay_controller.cc \
          $(EURORACK)/beads/dsp/delay_clock.cc $(EURORACK)/beads/dsp/mu_law.cc \
          $(EURORACK)/beads/resources.cc $(EURORACK)/stmlib/dsp/units.cc $(EURORACK)/stmlib/utils/random.cc

.PHONY: all test fetch clean

all: test

fetch:
	mkdir -p build
	test -d $(EURORACK)/beads || git clone --depth 1 https://github.com/pichenettes/eurorack.git $(EURORACK)
	test -f $(EURORACK)/stmlib/stmlib.h || (rm -rf $(EURORACK)/stmlib && git clone --depth 1 https://github.com/pichenettes/stmlib.git $(EURORACK)/stmlib)

build/ref: ref.cc scenarios.h
	mkdir -p build
	g++ -O2 -DTEST -w -I$(EURORACK) -I. ref.cc $(REF_SRC) -o $@

build/port: port.c scenarios.h $(CORE)/bd.c $(CORE)/bd.h $(CORE)/bd_tables.c
	mkdir -p build
	gcc -O2 -std=c99 -Wall -Wextra -Werror -Wno-unused-function -I. port.c $(CORE)/bd.c $(CORE)/bd_tables.c -lm -o $@

test: build/ref build/port
	mkdir -p out
	./build/ref out
	./build/port out
	python3 compare.py out

clean:
	rm -rf build/ref build/port out
