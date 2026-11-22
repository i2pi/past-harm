# p10m1-past -- macOS (OpenGL/GLUT + AudioToolbox, system frameworks only)
CC      ?= clang
# built for size (-Oz, LTO, dead code stripped, local symbols dropped, no
# __DATA_CONST segment): 149 KB -> 94 KB, with output (frames and audio)
# bit-identical to the -O2 build -- nothing here relaxes floating point
CFLAGS  ?= -Oz -flto -Wall -Wno-deprecated-declarations -DGL_SILENCE_DEPRECATION
LDFLAGS ?= -Wl,-dead_strip,-x,-no_function_starts,-no_data_in_code_info,-no_data_const
# the engine is compiled without FMA contraction, like the 2003 x87 build, so
# its output stays sample-identical to the original program
ENGINE_CFLAGS = -Oz -Wall -ffp-contract=off
LIBS    = -framework OpenGL -framework GLUT -framework AudioToolbox -framework ApplicationServices -framework CoreText -lm

all: p10m1-past tools/render

past_engine.o: past_engine.c past_engine.h
	$(CC) $(ENGINE_CFLAGS) -c -o $@ past_engine.c

harm_fx.o: harm_fx.c harm_fx.h i386conv.h
	$(CC) $(ENGINE_CFLAGS) -c -o $@ harm_fx.c

p10m1-past: main.c audio.c audio.h key.c key.h harm_fx.h reverb.c reverb.h title.c title.h past_engine.o harm_fx.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c audio.c key.c reverb.c title.c past_engine.o harm_fx.o $(LIBS)

tools/render: tools/render.c key.c reverb.c reverb.h past_engine.o harm_fx.o
	$(CC) $(ENGINE_CFLAGS) -o $@ tools/render.c key.c reverb.c past_engine.o harm_fx.o -lm

# the glass alone over a test card: tools/glasslab W H out.ppm [frames dir]
tools/glasslab: tools/glasslab.c main.c audio.c key.c reverb.c title.c past_engine.o harm_fx.o
	$(CC) $(CFLAGS) -DPDIR='"$(CURDIR)"' -I. -o $@ tools/glasslab.c audio.c key.c reverb.c title.c past_engine.o harm_fx.o $(LIBS)

# logs the notes and drums for the musicians' page: tools/musiclog [-noharmony] from_s to_s
tools/musiclog: tools/musiclog.c key.c key.h past_engine.o
	$(CC) $(ENGINE_CFLAGS) -o $@ tools/musiclog.c key.c past_engine.o -lm

clean:
	rm -f p10m1-past tools/render tools/glasslab tools/musiclog past_engine.o harm_fx.o *.ppm

.PHONY: all clean
