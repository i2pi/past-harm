# PAST HARM

Joshua Reich, Generative Works 1996–2026. past (2003) and harm (2003) playing
the p10m1 (2012) cube scene.

- [Technical statement](docs/TECHNICAL_STATEMENT.md): the ideas, the mathematics and the code
- [For musicians](docs/FOR_MUSICIANS.md): melody, harmony, rhythm, timbre and effects, with scores

## Requirements

macOS 11 or later, the Xcode command-line tools (`xcode-select --install`).
`ffmpeg` on the PATH for video rendering.

## Build

    make                    # p10m1-past and tools/render
    make tools/glasslab     # the glass test card (optional)
    make clean

## Run

    ./p10m1-past [-f] [-mute] [-loop] [-retina] [-samples n] [-swing 0..0.33]

| option | |
|---|---|
| `-f` | start full screen |
| `-mute` | no audio |
| `-loop` | start again when the piece ends |
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
    tools/render out.wav [seconds]            # past alone, as in 2003
    tools/render -raw out.raw seconds         # past alone, raw 16-bit stereo

## Package the macOS app

    scripts/package_macos.sh                  # build/Past Harm.app, build/past-harm-macos.zip
    SKIP_NOTARIZE=1 scripts/package_macos.sh  # sign only (not distributable)

Universal (Apple silicon + Intel), signed with the Superchromat Developer ID,
notarized and stapled. Needs the certificate and the `scion-notary` keychain
profile (see the script's header).

## Glass test card

    tools/glasslab 1600 900 lab.ppm           # a still
    tools/glasslab 960 540 x 150 frames/      # 150 frames, spinning

## Docs

    make tools/musiclog && python3 tools/mkdocs.py

Regenerates `docs/` from the source: the code excerpts are copied from the
files, the scores are transcribed from what the engine plays, and the figures
are rendered with headless Google Chrome (set `CHROME` to its path if it is
elsewhere).
