# p10m1-past

The p10m1 scene (Mar 2012) driven by the generative music of **past**
(Dec 2003, `old_i2pi/music/past2/past.c`), with **harm**'s convolution and 9-second cycle (Apr 2003, `old_i2pi/dsp/harm3.c`) applied to it. past plays
unmodified into harm's effect networks; the scene reads both programs' own
internal variables -- for what you are hearing at that moment -- and uses them
where p10m1 used a clock.

    make
    ./p10m1-past              # window, with sound; 8 minutes, then it ends
    ./p10m1-past -f           # fullscreen        (-loop: start again at the end)
    ./p10m1-past -mute        # no audio device (visuals still follow the music)
    ./p10m1-past -swing 0.2   # swing amount (default 0.10; 0 = past's original timing)
    ./p10m1-past -shot x.ppm -at 185    # deterministic still at 3:05
    ./tools/render -mix out.wav -swing 0.10   # the audio as played, offline
    ./tools/render past.wav       # past alone

Keys: `f` fullscreen, `Esc` quit.

## What drives what

| music (past_engine.c)                                        | scene |
|---|---|
| the 16 voices' envelopes                                     | cube sizes: each of the 100 cubes belongs to a voice and swells when it sounds |
| `scale[]` pitch classes, `scale2[]`/`scale1[]` octaves        | cube placement (radius, height), eased -- the cloud re-forms when the scales rotate (every 17/19/23 ticks) |
| the key (pitch-class histogram of the scales + sounding notes, Krumhansl-Kessler key profiles, with hysteresis) | the palette, subtly: the circle of fifths swings the hue about +-20 degrees around p10m1's blue (A major = 215; neighbouring keys ~10 degrees apart); minor keys 5 degrees cooler; glides over ~10 s |
| integral of the voices' mean frequency                       | rotation of the cube cloud |
| `tick`                                                       | a slight camera nudge and a faint, slow flash of the fine grid |
| past's slowest terms `cos(i/384972)`, `sin(i/319732)` (54 s / 45 s periods) | the background's radial-warp coefficients r1, r2 (p10m1 used cos(1.5T), sin(T/3)) |
| x feedback gain `0.75 sin(i/749732+tick)+0.26`               | spread between the three glow renders' fields of view |
| `0.47 sin(i/33391)` (the ox3 term)                           | blur kernel centre weight (glow soft <-> crisp) |
| `0.30 sin(i/441000)` (the slow term)                         | feedback decay (trails) |
| output level (eased over 2.5 s); energy of the atan (u,v) voices | brightness; a little saturation |
| the 8-minute arc                                             | a slow dolly in and out |

### harm (dsp/harm3.c) on top

Audio (`harm_fx.c`): past's left channel feeds harm's long-tap network (taps
7500/17759/22500/45000/49990 with swinging gains), the right channel the one
with harm's 25000-sample tap -- held still: harm swept it (`25000 + 7500
sin(k/6000)`), which sounds like tape speeding up and down; harm's chaotic q/z counter
fires clear / nofx / nofxfeed events on them. harm made those switches (and
the jump of its gains when its counter wraps) within one sample, which clicked
on past's held tones; here they glide over 30-50 ms, and a clear fades the
echoes out before emptying the network.

(harm's chords and swell were tried, re-pitched into past's key, and removed.)

### title

Bottom left, left-aligned, in D-DIN (bundled in `fonts/`, SIL Open Font
License): **PAST** and **HARM**, large, on two lines, and below them
"Joshua Reich Generative Works 1996-2026".
It is set once with CoreText into a coverage mask and stamped into the frame
before the feedback, and it is eaten by the picture: each text pixel's
integrity only ever falls, wherever the evolving picture differs from the
plain background behind it (raggedly, ramping in over the first ~10 s), and
the damage creeps from bitten edges into the strokes around them. As it is
eaten, its fragments are dragged into the blur and the echoes.

### the opening: sparse and oppressive

A mood runs from 0 (oppressive) at the start to 1 (open), smoothstepped over
about 0:20-3:20. While it is below half (until ~1:50): new notes snap to
Phrygian (minor with a flat 2nd), or with 4+ voices sounding to the in scale
(root, b2, 4, 5, b6), instead of the tracked major / minor; past's short
melodic notes sound an octave lower; and the opening solo sings a low lament
an octave down (root, b2, root, b7 below, root, b2, b6 below, root) instead
of walking up past's scale. Below 0.3 the voice cap is one lower (as few as 1).
The whole mix is darkened by a low-pass from ~900 Hz that opens fully as the
mood lifts.

In the picture, the lattice starts as desaturated orange; its colour comes
back gradually with its variants (a surprise can jump it).

### endless

past stopped at 8:00 (it counted minutes and quit). p10m1-past carries its
maths on indefinitely: the tick count, the scale swaps and every slow
sin(i/...) keep going, as do the key, the arc, the polyrhythm and harm's
counters, so the piece keeps evolving. Three things are bounded so it stays
musical: past's sections (gated by the minute: the bass pairs and choir from
1:00 to 6:00, some triggers only before 4:00) cycle every 8 minutes; its tick
period, which shrinks by 30 samples a minute, stops at 8192; and notes fold
down by octaves above its 8-minute range (the swaps lift the scales about 3
semitones a minute -- the top note would be 177 after an hour, 743 after
four). None of this changes the first 8 minutes. `tools/render -mix` renders
8 minutes unless given a length.

### long swells: choir richness and bass weight

In endless mode two slow sines ride over everything, with coprime periods
so the pair only lines up again every 45 minutes:

| control | period | starts | low (0) | high (1) |
|---|---|---|---|---|
| choir richness | 5 min | at its lowest, rising | narrowed towards mono, 0.7x level | full stereo, a second rank an octave up and a third a fifth up, 1.3x level |
| deep bass weight | 9 min | halfway, rising | 0.35x level, nearly a pure sine, darker (150 Hz low-pass), less drive | 1.35x level, more harmonic body, more drive, 290 Hz low-pass |

The middle of each (0.5) is the sound before they were added, and that is
where both sit outside endless mode. The choir and its bass sing only in
past's sections that call for them (minutes 1-3 of each 8, sparsely in 4-5),
so the swells are heard each time they come back, at a different point on
both sines.

### the rhythm mutates

The polyrhythm is five streams, each a division of past's bar, each with a
drum that guides the ear to it. In endless mode they are genes:

| gene | first | pool | what it times | guide drum |
|---|---|---|---|---|
| 0 | 3 | 3 4 5 7 | the choir's beat (its notes wait for it, it breathes on it) | log drum |
| 1 | 3 | 3 5 7 | past's accents: groups of g steps of its 16 (3+3+3+3+4, 5+5+6, 7+9) | rim |
| 2 | 5 | 3 5 7 9 11 | harm's left delay grid (taps at 1/4, 1/2, 1, 3/2, 7/4 of a bar / g) | bell |
| 3 | 7 | 5 7 9 11 13 | harm's right delay grid (taps at 1/2, 1, 2 of a bar / g) | tick |
| 4 | 11 | 9 11 13 | harm's high tick, against 8 | -- |

About every 7 minutes (randomly 6-8, on a downbeat) one gene, picked at
random, mutates to a new value from its pool that is coprime with the others
(genes 0, 2, 3, 4), so the streams never lock together. Its drum comes back
to full for 4 bars to point the ear at the new rhythm. When a delay grid
changes, the taps glide to their new lengths at most 1% a sample (17 cents:
a slow tape-like bend of the echoes over ~10-40 s, read between samples)
instead of jumping; a tap longer than the buffer folds down an octave. The
first mutation is at ~7:00, so everything before it is unchanged (the first
6 minutes render bit-identical to before). The first hour:

    6:59  right delay grid  7 -> 13      35:31  high tick  9 -> 11
   14:33  accent grouping   3 -> 7       42:47  choir beat 4 -> 3
   21:53  choir beat        3 -> 4       50:01  left delay grid 5 -> 7
   28:55  high tick        11 -> 9       57:06  left delay grid 7 -> 5

### an arc of voices

p10m1-past limits how many of past's 16 voices may sound at once, along a slow
arc: 2 at the start (space), building to as many as 16 and back over ~150 s,
with peaks that vary over ~330 s. A note that would go over the limit is
skipped (a voice counts while held or above 0.15 in its release). The zoom,
reverb and old/new cube mix all follow the voice count, so they ride the arc
too. `tools/render` applies it in `-mix` mode; its plain renders stay past's
original.

### harmony and the choir

New notes snap to the nearest pitch of the key the tracker hears (ties go
down): any note of the major / natural-minor scale while fewer than 4 voices
sound, only the pentatonic once 4 or more do -- no semitones or tritones in
the fuller passages, where past's mixed thirds (scale2's 3s and 4s) and its
shared atan stage made it clash.

The long notes -- the bass pairs that enter at 1:00 -- are sung by a choir
instead of past's timbre. Its sound is built PADsynth-style: a saw's overtone
series (harmonic h at 1/h) in which each harmonic is a gaussian band in the
frequency domain, wider as h rises, with random phases, turned by one inverse
FFT into a seamless ~5.9 s loop (2^18 samples). The ensemble's bands are 45
cents wide (many voices, slightly apart), the opening solo's 12 cents (one
breathy singer). There is a table per octave of base pitch (140/280/560 Hz),
read at the note's pitch with a little vibrato; left and right read half a
loop apart, so they are decorrelated. The tables are built once at start-up
(`past_prepare_choir`, ~6 MB). The result goes through three vowel formants
that drift between "ah" and "oh" with past's slow drive (sin(i/319732)).
The choir sits under past's own notes in the mix. Beneath it is one heavy,
thick bass voice: the lowest choir note three octaves down, folded into
20-50 Hz (it sat an octave higher and overpowered the mix) -- a pure fundamental for weight plus three slightly detuned voices
(-6, 0, +6 cents) carrying the 2nd and 3rd harmonics at a quarter of its
level, beating slowly, lightly saturated and rounded by a ~220 Hz low-pass
(halved and leaned out so past's chords and melody come through); mono,
gliding between notes and following the choir's envelope (not under the
opening solo).
The choir is added after past's atan stage, so it stays clean.

The piece opens with a single voice of the choir: one centred singer working
slowly through past's scale (a note every 6 s, swelling in and fading,
snapped to the key) until the choir proper enters at 1:00. It is not held to
the voice cap.

Both are on in the app and in `tools/render -mix`; plain renders stay past's
original.

### polyrhythm

past's step grid (8192 samples) is grouped into bars of 16 steps (2.97 s) and
4-bar phrases. The music itself carries the cross-rhythms, each stream in its
own number against the bar, all meeting on the downbeat:

| stream | rhythm | how |
|---|---|---|
| past | 3 over 16 | its notes are accented in groups of 3 steps (3+3+3+3+4) across its own 16-step grid; the others are played at half level |
| choir | 3 per bar | its notes wait for the next beat in 3, and it breathes in 3 (a swell on each beat, ~0.35 s; its bass too) |
| harm, left | 5 per bar | the left network's echo taps sit on fifths of the bar (1/20, 1/10, 1/5, 3/10, 7/20), so past echoes in a 5-against-4 trail |
| harm, right | 7 per bar | the right network's taps sit on sevenths (1/14, 1/7, 2/7) |

The opening solo voice sings on the bar (every 2 bars).

The drums guide the ear to those streams. Each doubles one stream, comes in
at full when the arc of voices reaches it, and after 4 bars recedes to a
light doubling, leaving the music to carry the rhythm:

| drum | doubles | enters at | sound, pitch, place |
|---|---|---|---|
| log drum | the choir's 3 | arc 0.18 | tone + 2.76x partial; fifth, 150-300 Hz; a little left |
| rim | past's 3+3+3+3+4 | arc 0.42 | band-passed noise + short tone, root / fifth; 700-1400 Hz; a little right |
| bell | harm left's 5 | arc 0.66 | mellow FM (see below); chord tones, 450-900 Hz; alternating sides |
| tick | harm right's 7 | arc 0.84 | bright tick; ninth, 2.4-4.8 kHz; right |

The bell is mellow and follows its context: a near-harmonic FM ratio that
drifts between 1 (round) and 2 (hollow) with past's slow drive, a low index
and a low-pass that open a little as the arc fills, a soft 8 ms attack, and a
ring of ~2.2 s when sparse down to ~1 s when full. It walks the chord --
third, fifth, octave, fifth, root over its 5 hits, the pattern turning with
each bar of the phrase (only third and fifth while sparse) -- and is softer
off the downbeat.

(A heartbeat drum in 2 held the downbeat at first; harm's thump has taken
over that job -- see below.)

**harm's drums.** harm's own clicks are back, re-timed to past's bar: harm's
clock is stretched by 1.6384 so its 80000-sample period is exactly one bar,
and harm's own numbers then make the patterns. The sounds are harm's
formulas; only the gates are re-timed (with 2 ms ramps), and they go through
harm's slot 4-6 networks, as in harm, in the centre.

| harm element | on past's bar | enters |
|---|---|---|
| click2, low: 5000 sin, while i % 80000 < 5930 | the downbeat thump (~0.22 s), tuned to the root, 55-110 Hz | after 2 bars |
| click2, high: 2500 sin, while i % 10000 < 3000 and i % 7347 < 500 | 10000 -> bar/8, 7347 -> ~bar/11: two gates interfering in 11 against 8; tuned to the fifth | arc 0.5 |
| click1: the ring-modulated blip | once per bar, off the beat at 3/8 | arc 0.3 |
| click3: the chirps in 270000-397000 of harm's 400000 cycle | 400000 x 1.6384 = exactly 5 bars: a sweep up and back down into every fifth downbeat | arc 0.6 |

The camera's nudge now follows the thump.

The drums are added after harm's networks (whose echoes would smear them),
before the reverb. In the picture the heartbeat nudges the camera and each
orange lattice layer pulses -- brighter and thicker -- with the log drum,
rim, bell and tick.

### film (set aside)

Off for now (`film_on = 0` in main.c); the code is kept.

A 3D scalar field of film grain -- a 24^3 grid around the cloud -- is stirred
by the cubes: each frame every cube deposits into it where it is, in
proportion to its speed and size, so the cubes swooshing around leave grainy
wakes; the field spreads to its neighbours and fades over ~2 s. It is
projected through the cubes' camera into a soft screen map (relative to its
recent peak), which sets how grainy each region of the picture is: stronger,
and coarser (half-resolution clumps), where the field is dense.

The film finish is applied to the shown frame only (the feedback keeps the
clean one):

- halation: highlights bleed a warm red-orange glow (strongest in red) into
  their surroundings, as light scattered back through the film base;
- dynamic range compression: a filmic curve with a lifted toe and a soft
  highlight shoulder, applied to luminance with the colour scaled along (so
  hue and saturation are kept), saturation lifted ~25%, warm highlights and
  cool shadows;
- grain: fresh every frame, strongest in the midtones, a little colour in it,
  its strength and size set by the grain field.

### glass cubes

About one cube in nine (a fixed scatter through the cloud) is glass. Glass
cubes are left out of the opaque renders; a separate pass draws them with
each face coloured by its eye-space normal (behind the opaque cubes, which
write depth only), and the compositor uses that to:

- refract: each flat face shows the picture behind it shifted along its
  normal, so every face bends the view by its own amount;
- disperse: red, green and blue are shifted by clearly different amounts
  (blue ~2.6x as far as red, more on bell hits), so edges behind the glass
  split into red / green / blue fringes;
- diffract: rainbow fringes where faces meet and at the silhouette;
- lose a little light: mostly clear, slightly lossy glass (Beer-Lambert
  absorption along the path, longer through faces seen edge-on; red absorbed
  a little more than blue) -- about 84% / 89% / 92% (R / G / B) through a face
  seen square-on, ~60-80% at grazing angles -- with a little light caught at
  grazing faces (Fresnel).

(Mirrors are switched off for now -- `MIRRORS 0` in main.c; those cubes are
plain cubes again. With `MIRRORS 1`:) About a third of the glass cubes are mirrors instead: they reflect what they
see. A second normal pass renders just the mirrors (with the same depth, so
glass, mirrors and opaque cubes occlude each other); for each mirror pixel the
camera's view ray (with the zoom) is bounced off the face's normal and the
reflected direction picks where in the current frame to look, so each face
shows a small, flipped view of the scene that slides as the cube turns. They
have a slightly dark, silvery tint and more light at grazing faces, no
dispersion; and they share the quality range below, so bumpy and fractured
mirrors ripple and shatter their reflections.

Not all the glass is perfect (and the fractured ones are whole for the
first minute, then crack one after another between about 1:00 and 1:15):
the glass cubes are spread evenly over four qualities -- perfect; gently bumpy (a low, rolling ripple); more bumpy
(a deeper but still smooth ripple); fractured (Voronoi shards, each tilted its own way,
with sharp cracks between them that bend the light hard, catch it, and carry
the rainbow fringes). The flaws are tiling, mipmapped textures in each face's
own frame: the surface's slope along the face's two tangents, and its height.
The glass normal pass is a small shader that does tangent-space bump mapping:
it carries each face's eye-space normal and tangents, tilts the normal by the
slope along those tangents (so a bump bends light the way the face is actually
turned, whatever the cube's rotation), renormalises it, and writes the
normal's x, y and the glass height. The glass pass then refracts by Snell's
law (n = 1.5) at that normal, against the true view ray, and scales the bend
and the absorption by the thickness (thicker glass where the surface is high,
grooves along the cracks); the bumps also move the Fresnel edge light and a
small specular glint, so the lumps read as shape, not just as wobble. Each cube
has its own texture scale (a couple of broad swells per face) and offset.
Dispersion is kept near glass-like (each colour's bend differs by 5..11%,
with the bell), so it shows as thin fringes where the bend is large, not as
split copies of everything. The glass and mirror normal passes have their own
half-float multisampled buffer: in 8 bits a bumpy mirror's reflection moved
in ~14 px terraces. `make tools/glasslab` builds a test card: every
glass quality (perfect, gently bumpy, strongly bumpy, fractured, a perfect
mirror and a bumpy one) at three orientations over a checker, grid and stripes, as a still
(`tools/glasslab 1600 900 lab.ppm`) or spinning (`tools/glasslab 960 540 x 150 dir`).

The choir bends the light -- refraction grows with its level (10..26 px at
720p) -- and the bell layer of the polyrhythm (the 5) spreads it: dispersion
pulses with each bell hit. The glass is applied before the orange lattice and
the feedback, so what it bends feeds the blur and echoes too.

### orange lattice in the black

Where the composited picture has gone close to black (the blurred feedback
inside the cubes), an orange triangular lattice shows through -- the darker,
the stronger -- before the frame is fed back, so it feeds the blur and the
echoes along with the black. The lattice is drawn from the score: each of the
(up to) 4 loudest voices sounding gives a layer whose

- spacing is its note against the key over two octaves (6..23 cells across),
  so the layers stand in the chord's own ratios;
- line thickness (~0.7..3 px) and brightness follow its envelope;
- hue is orange, swung +-10 degrees by the key round the circle of fifths
  (minor keys redder), a few degrees apart per layer;

so the repeats build up as voices enter and thin out as they leave. It comes
back different: whenever it has been out of sight for half a second (no dark
to show through, or no voices) and shows again, it takes a new variant,
chosen from the music's tick count -- repetition (~70%: a small step from the
last, a few degrees of turn, a little shear, the neighbouring hue) or surprise
(~30%: up to +-15 degrees of turn, shear and stretch, a strong warp of its
lines, a hue across the family). The family is orange, amber, red-orange,
teal, cyan-blue and blue, and each return is a little less dense than the
last, down to about half. Its
shading is a gradient whose direction turns with harm's 9 s cycle. All of it
eases, so changes morph rather than jump.

### reverb where past is sparse

After harm, a soft stereo reverb (`reverb.c`: 8-line feedback delay network,
damped, unmodulated so nothing wobbles in pitch) follows how much of past is
sounding -- the sum of its voice envelopes, smoothed (thinning over ~2.5 s,
filling over ~0.8 s). When past is full the send is 0.06 with a 2.2 s tail;
as the voices thin out it swells to 0.70 with a 7.5 s tail and fills the
space.

harm's **9-second cycle** (its counter i wraps at 400000 samples): it swings the
networks' wet level +-15% against the dry past, and in the picture breathes
the glow spread and the background a touch.

harm's time shift was tried and taken out. Its clicks were taken out in
their original timing (it doesn't sit with past's beat) and brought back
re-timed to past's bar (see the polyrhythm section). Copied verbatim from harm3.c except: feedback gains are scaled to sum to
<= 0.95 (past is far louder than harm's voices and some of harm's tap sets sum
to 1.77, which runs away), and the mix is soft-limited.

| harm | scene |
|---|---|
| the two networks' taps (delay, gain) | **frame convolution**: a delay line of recent frames read at the same delays (samples -> frames) with the same gains -- left half of the picture from the left network, right half from the right -- fed back like the audio |
| clear event | that delay line is emptied (a cut) |
| nofx / nofxfeed | echoes emphasised / switched off |

### Swing

past's melodic triggers sit on an 8192-sample step grid (186 ms). With swing,
the triggers on every other step fire late by `swing` x 8192 samples (default
0.10 = 19 ms; 0.33 is full triplet swing). The tick-driven chords and bass are
not on that grid and keep their pulse. With `-swing 0` the engine is still
byte-identical to the original.

### the app

`scripts/package_macos.sh` builds `build/Past Harm.app` and
`build/past-harm-macos.zip`: a universal binary (Apple silicon + Intel,
macOS 11+), the fonts and an icon (a frame of the 4K render) in the bundle,
signed with the Superchromat Developer ID (hardened runtime), notarized by
Apple and stapled, so it opens with a double-click on any Mac. Opened from
the Finder it runs full screen with the cursor hidden; Esc or q quits, f
toggles full screen. `SKIP_NOTARIZE=1` signs only.

### GPU, 4096 x 2304

All the compositing runs on the GPU (OpenGL 2.1, GLSL 1.20). The geometry --
the grids, the lattice, the three cube renders, the glass and mirror normal
passes -- renders into a 4x multisampled offscreen buffer that is resolved
into textures, and every per-pixel step is a fragment shader: p10m1's blur,
mattes and feedback (their integer arithmetic, with its truncation, emulated
exactly -- it is part of how the picture drifts to black), the glass and
mirrors, the lattice showing through, the title's eating (its integrity in a
float texture), and harm's frame echoes (an 80-frame ring at half resolution,
at most 960 wide). Only a 128 x 72 count of how much lattice shows is read
back. With `-retina` the window asks for a full-resolution surface, so full screen
(`f`) on a Retina 4096 x 2304 panel (shown as 2048 x 1152) renders every
pixel; without it the frame is the window's size in points. `-samples n`
sets the multisampling. An offscreen 4096 x 2304 frame takes
~35 ms on an M1 Pro, music synthesis included.

### rendering a video

    ./p10m1-past -video out.mp4 [-vsize 4096x2304] [-vsecs 480] [-vfps 30]

renders offline, with no window: the same GPU pipeline in an offscreen GL
context, a fixed 1/30 s step with the music run offline to each frame, the
frames piped to ffmpeg (H.264, VideoToolbox), and the soundtrack captured
from that same run of the music (also kept as out.mp4.wav), muxed with AAC
320k. 8 minutes at 4096 x 2304 takes about a quarter of an hour on an M1 Pro.

### Antialiasing

The cube renders are multisampled (macOS grants 4 samples), and every cube is
composited with a coverage weight that blends the antialiased edges smoothly.
(p10m1's own hard "not white" test -- which also copied the pale halo of the
blurred renders, the white bleed into the glow and echoes -- used to take a
share of the cubes that grew with the number of voices; the code for it is
still there, tagged per cube in the alpha channel, but every cube is now done
the smooth way.) The glass cubes are smoothed too: their pass is
multisampled, its face normals are softened across the seams with a 3x3
blur, and the refracted picture is sampled bilinearly, so the bending and
the colour fringes ease rather than step.

## Verification

`past_engine.c` is past.c's main loop with its locals moved into a struct --
same statements, order, types and constants; additions only *record* values.
`tools/render -raw` output is **byte-identical** to the original program
(built natively, its /dev/dsp stream captured) over the first 180 s
(31,752,000 bytes). The engine is compiled without FMA contraction, like the
2003 x87 build. (`tools/render -raw` renders past alone; the harm layer then
changes the sound on purpose. A full `-mix` render stays below full scale --
peaks ~15000, -24..-42 dBFS RMS.)

The build is for size (`-Oz`, LTO, dead code stripped, local symbols
dropped, no `__DATA_CONST` segment): the app is 94 KB, down from 149 KB.
None of it relaxes floating point, so the output is unchanged: against the
`-O2` build, `tools/render -raw` (180 s) and `-mix` (120 s) are
byte-identical, and so are the app's own `-video` frames and soundtrack
(600 frames at 640x360, captured raw by a stand-in ffmpeg). The engine costs
~6% more CPU (about 4.4% of one core, down from 4.1%).

## Files

- `past_engine.c/.h` -- the music, as a sample engine
- `harm_fx.c/.h`, `i386conv.h` -- harm's networks, events, cycle
- `reverb.c/.h` -- the reverb that swells where past is sparse
- `key.c/.h` -- key tracking (Krumhansl-Kessler profiles + hysteresis)
- `audio.c/.h` -- AudioQueue playback (or a real-time silent thread with
  -mute); publishes a snapshot of the music state every 512 samples; the
  renderer picks the one that is audible now
- `main.c` -- the p10m1 scene (grids, three FOV renders of 100 cubes,
  software blur/matte/mask compositing with feedback),
  re-wired to the music
- `tools/render.c` -- offline WAV/raw render

Built from: ~/src/old_i2pi/p10m1 (port of old_hard_drives/src/p10m1) and
~/src/old_i2pi/music/past2 (port of old_hard_drives/src/music/past2).
