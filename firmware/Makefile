# Own MPC Sample firmware, phase 1. `make` cross-builds the device binary, `make test` runs the host self-test.
CORE = engine.c seq.c params.c app.c mode_sample.c mode_midi.c mode_audio.c mode_seq.c mode_roll.c ui.c wav.c files.c
WARN = -Wall -Wextra

mpcfw: main.c hw.c $(CORE) *.h ../panel-demo/surface.h
	zig cc -target aarch64-linux-musl -static -O2 -s $(WARN) -I../panel-demo -o $@ main.c hw.c $(CORE)

test: test.c $(CORE) *.h
	cc -std=c11 -D_DEFAULT_SOURCE -g $(WARN) -fsanitize=address,undefined -pthread -I../panel-demo -o fw-test test.c $(CORE) -lm
	./fw-test $(SHOT)

font.h: ../tools/bdf2c.py ../assets/fonts/ter-u12n.bdf ../assets/fonts/ter-u16b.bdf
	python3 -I ../tools/bdf2c.py $@ FONT_S=../assets/fonts/ter-u12n.bdf FONT_L=../assets/fonts/ter-u16b.bdf

.PHONY: test
