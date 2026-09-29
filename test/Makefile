# Native effect test (noise through every mode/quality, freeze, trigger, state chunk). No device, no Docker.
#   make -C test run
PS = ../third_party/parasites
SRC = fx_test.cc ../src/engine.cc ../src/midi_in.cc \
      $(PS)/clouds/dsp/correlator.cc $(PS)/clouds/dsp/granular_processor.cc $(PS)/clouds/dsp/mu_law.cc \
      $(PS)/clouds/dsp/pvoc/frame_transformation.cc $(PS)/clouds/dsp/pvoc/phase_vocoder.cc \
      $(PS)/clouds/dsp/pvoc/stft.cc $(PS)/clouds/resources.cc \
      $(PS)/stmlib/dsp/atan.cc $(PS)/stmlib/dsp/units.cc $(PS)/stmlib/utils/random.cc
CXXFLAGS = -std=gnu++11 -O2 -g -DTEST -I$(PS) -I../third_party/mpc-vst-plugins/wrapper \
           -fsanitize=address,undefined -Wall -Wno-unused-parameter -Wno-narrowing -Wno-infinite-recursion \
           -Wno-unused-local-typedef

run: fx_test
	./fx_test

fx_test: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -rf fx_test fx_test.dSYM

.PHONY: run clean
