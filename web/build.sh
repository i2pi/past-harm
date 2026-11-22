#!/bin/sh
# web/build.sh: PAST HARM as one self-contained page, build/web/past-harm.html
#   needs Emscripten (emcc on PATH, or EMSDK pointing at an emsdk checkout)
set -e
cd "$(dirname "$0")/.."
if ! command -v emcc >/dev/null 2>&1; then
	: "${EMSDK:?emcc not found: install emsdk and set EMSDK}"
	. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
fi
OUT=build/web
mkdir -p "$OUT"

# the music, for the AudioWorklet: a standalone module (no JS runtime), the
# engine without FMA contraction as in the macOS build
emcc -O3 -ffp-contract=off -DAUDIO_WORKLET \
	audio.c past_engine.c harm_fx.c key.c reverb.c \
	-sSTANDALONE_WASM --no-entry -sFILESYSTEM=0 \
	-sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=256MB -sSTACK_SIZE=1MB \
	-sEXPORTED_FUNCTIONS=_aw_init,_aw_render,_aw_snaps,_aw_snap,_aw_snap_size,_aw_seek_exact,_aw_install,_aw_state_slot,_aw_state_size \
	-o "$OUT/engine.wasm"

# the picture: main.c on WebGL2
emcc -O3 -Wno-deprecated-declarations \
	main.c web/gl_compat.c web/bsd_random.c web/audio_main.c web/title_web.c \
	-DAUDIO_WORKLET -ffp-contract=off audio.c past_engine.c harm_fx.c key.c reverb.c \
	-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
	-sALLOW_MEMORY_GROWTH=1 -sINVOKE_RUN=0 -sENVIRONMENT=web -sSINGLE_FILE=1 \
	-sEXPORTED_FUNCTIONS=_main,_malloc,_aw_slot,_aw_push,_aw_swing,_aw_main_snap_size,_web_pause,_web_now,_aw_warp,_aw_state_size \
	-sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAP32 \
	--shell-file web/shell.html -o "$OUT/main.html"

# one page: the engine and the fonts inlined
python3 - "$OUT" <<'PY'
import base64, sys
out = sys.argv[1]
b64 = lambda p: base64.b64encode(open(p, 'rb').read()).decode()
page = open(out + '/main.html').read()
page = page.replace('__ENGINE_WASM__', b64(out + '/engine.wasm'))
page = page.replace('__FONT_REGULAR__', b64('fonts/D-DIN.otf')).replace('__FONT_BOLD__', b64('fonts/D-DIN-Bold.otf'))
page = page.replace('__TOUCH_ICON__', b64('web/touch-icon.jpg'))
open(out + '/past-harm.html', 'w').write(page)
print('%s/past-harm.html: %d KB' % (out, len(page) // 1024))
PY
rm -f "$OUT/main.html"
