# PAST HARM

Joshua Reich, Generative Works 1996–2026. past and harm (both before 2003) playing
the p10m1 (2012) cube scene.

- [Technical statement](docs/TECHNICAL_STATEMENT.md): the ideas, the mathematics and the code
- [For musicians](docs/FOR_MUSICIANS.md): melody, harmony, rhythm, timbre and effects, with scores

## Requirements

macOS 11 or later, the Xcode command-line tools (`xcode-select --install`).
`ffmpeg` on the PATH for video rendering.

## Build

    make                    # p10m1-past and tools/render
    make clean

## Run

    ./p10m1-past [-f] [-mute] [-retina] [-samples n] [-swing 0..0.33]

| option | |
|---|---|
| `-f` | start full screen |
| `-mute` | no audio |
| `-retina` | render at the display's full (Retina) resolution |
| `-samples n` | multisampling (default 4) |
| `-swing f` | swing past's off-beats |
| `-shot out.ppm -at s` | save a frame at `s` seconds and exit |

Keys: `f` full screen, `Esc` / `q` quit.

## Render a video

    ./p10m1-past -video out.mp4 [-vsize 4096x2304] [-vsecs 480] [-vfps 30]

Offscreen (no window), faster than real time; the soundtrack is written
alongside as `out.mp4.wav`.

## Render audio only

    tools/render -mix out.wav [seconds]       # the full piece (default 480 s)
    tools/render out.wav [seconds]            # past alone, as originally written
    tools/render -raw out.raw seconds         # past alone, raw 16-bit stereo

## Package the macOS app

    scripts/package_macos.sh                  # build/Past Harm.app, build/past-harm-macos.zip
    SKIP_NOTARIZE=1 scripts/package_macos.sh  # sign only (not distributable)

Universal (Apple silicon + Intel), signed with the Superchromat Developer ID,
notarized and stapled. Needs the certificate and the `scion-notary` keychain
profile (see the script's header).

## Build for the web

    ./web/build.sh          # needs Emscripten (emcc on PATH, or EMSDK=~/emsdk)

Writes `build/web/past-harm.html`, one self-contained page (~420 KB) that runs
from a file or any web server: click to start, `f` or double-click for full
screen. The music runs in an AudioWorklet (audio.c built with
`-DAUDIO_WORKLET`), the picture is main.c on WebGL2 through `web/gl_compat.c`.
Move the pointer (or touch) for play / pause (space).

The canvas is the window's size in CSS pixels, as the macOS app renders in points
(capped at 1280×720 on phones); `?px=2073600` caps it at that many pixels.

## Docs

    python3 -m venv .venv && .venv/bin/pip install verovio     # once
    make tools/musiclog && .venv/bin/python tools/mkdocs.py

Regenerates `docs/` from the source: the code excerpts are copied from the
files; the scores are transcribed from what the engine plays, analysed
(`tools/harmony.py`) and engraved with Verovio (`tools/engrave.py`); the other
figures are rendered with headless Google Chrome (set `CHROME` to its path if
it is elsewhere).
