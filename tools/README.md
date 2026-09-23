# RS2EX renderer validation tools

These exist because of what happened in v0.0.9.

A lifecycle migration left behind the line that computed the depth/stencil
clear mask. The per-frame clear stopped clearing depth, and from the second
frame onward every mesh failed the depth test. The user interface still drew.

Every automated check in that release passed. The build was clean, the
dependency scan read zero, the draw calls were provably equivalent to the
baseline, and mesh import was byte-identical. None of them looked at whether
there was still a picture on the screen.

The pixel comparison that covered v0.0.7 and v0.0.8 had been retired for that
release because the test scene is not deterministic: the sky changes colour
with the in-game clock, and the train and camera move, so the same binary
differs from itself in most pixels. Retiring it was reasonable. Not replacing
it was not.

So: two tools here, and a fixture mode in the program itself.

| | |
|---|---|
| `rs2shot.ps1` | launch a build, put the window somewhere reproducible, screenshot it |
| `scenecheck.py` | measure whether a screenshot still has a scene in it |
| `rs2state.ps1` | pin the world a fixture capture starts from |

## WP8 Stage 0 texture probe

`-dx12 -dx12texturesmoke` draws a 4-column by 3-row grid through the public
texture, state and draw APIs for 300 frames. It covers file and resource
textures, bind persistence and unbind, point/linear filtering, alpha test
off/on, colour key, and untextured fallback. The log also checks invalid
sources, 130 public create/destroy cycles (more than the 128 SRV slots),
in-flight descriptor retirement, draw/refusal counts, upload bytes and
live-count baselines. Check its screenshot with:

```
python tools/texture_smoke_check.py path\to\texture-smoke.png
```

The checker verifies 60 interior pixels. The linear-filter panel allows a
one-channel rounding difference; all other samples are exact.

## WP9 real-scene texture detail

Run the accepted fixture with `-dx12 -fixture -dx12sceneaudit -dbf` and capture
it using `rs2shot.ps1`. At shutdown, `RS2D3D12SCENE` lines report texture
attempts/successes, Stage 0 binds and filters, texture/descriptor lifetime,
textured draws, unsupported calls, InfoQueue messages, and final device refs.
To measure the windowed image against the accepted v0.1.1 textureless capture:

```
python tools/texture_scene_check.py path\to\v011-textureless.png path\to\wp9-windowed.png
```

It checks a fixed ground region where the old D3D12 renderer was white and the
Stage 0 renderer shows detailed grass. It does not claim full visual parity.

## WP7 alpha probe

`-dx12 -dx12alphasmoke` draws a 6-column by 3-row grid from a known-alpha
PNG through the public texture and draw APIs. Columns are alpha test off,
ALWAYS, LESS_EQUAL/128, GREATER/128, LESS_EQUAL/0 and GREATER/0; rows sample
alpha bytes 0, 128 and 255. The run submits 300 frames and holds its final
frame briefly for capture. Check a screenshot with:

```
python tools/alpha_probe_check.py path\to\alpha-probe.png
```

The checker compares five interior pixels in each of the 18 regions exactly.

## scenecheck.py

Measures structure rather than colour, so it survives a scene that keeps
moving:

- **edge%** - viewport pixels whose 3x3 gradient exceeds a threshold. Geometry
  has silhouettes, rails, girders, platform edges. A sky gradient has none.
- **colours** - distinct colours after quantising to 5 bits per channel.

Both collapse when geometry stops being drawn, and neither needs the scene to
be identical between runs. Measured against the actual v0.0.9 defect:

| build | edge% | colours |
|---|---|---|
| working | 1.41 - 3.61 | 373 - 410 |
| the defect | 0.17 - 0.18 | 55 - 69 |

Eight times at the narrowest, so this is used as a **floor**, not an equality
test. It answers "is there still a scene", not "is the scene right". For the
second question use the fixture mode below, where frames are reproducible and
can be compared exactly.

```
python tools/scenecheck.py shot.png
python tools/scenecheck.py --min-edge 1.0 --min-colours 200 shot.png
python tools/scenecheck.py --compare before.png after.png
```

Passing a floor makes the exit status meaningful: 0 when every image passes,
1 when any falls below. Needs Pillow and numpy.

## rs2shot.ps1

`CopyFromScreen` reads the screen rather than the window, and
`SetForegroundWindow` is refused to a background process, so a naive capture
photographs whatever happens to be on top. This moves the window to empty
desktop as topmost, which needs no activation rights and does not change the
client size, so it does not trigger a device reset. It also parks the cursor,
because the program draws its own cursor and highlights whatever is under it.

`Config.txt` normally has `FullScreen = yes`, so `-win` is passed by default.

```
powershell -File tools/rs2shot.ps1 -Exe path\to\RailSim2.exe -Out shot.png
powershell -File tools/rs2shot.ps1 -Exe ... -Out ... -ExtraArgs "-fixture"
powershell -File tools/rs2shot.ps1 -Exe ... -Out ... -Fullscreen
```

## The fixture mode

`-fixture` runs exactly 30 simulation ticks - one simulated second, enough for
the loaded layout to settle - and then stops the world. Every frame after that
renders the same state, so two runs can be compared pixel for pixel, which is
the check `scenecheck.py` deliberately cannot give you.

The tick budget is counted, not timed. The clock can hand over several ticks
at once after a stall, so an unclamped total lands somewhere in a range that
depends on how the frames happened to fall, and the world freezes in a
slightly different state each run. That was measured: a distant train sat a
few pixels apart between two captures before the clamp.

It is a developer switch. It is not in the settings, it is not saved, and it
changes nothing when absent.

### Pinning the world it starts from

`-fixture` makes a run reproducible from wherever the program resumes. It does
not decide where that is: the program saves its state on exit and picks it up
again next time, so every run moves the starting point.

That was found the hard way. Two captures taken a day apart differed in
**99.6%** of pixels with nothing in the renderer changed - the clock had moved
from 20:01 on one day to 07:13 on another, because runs in between had
advanced the world and saved it.

So `rs2state.ps1` snapshots what the program rewrites - `Config.txt` and
`Undo` - and restores it before a capture:

```
powershell -File tools/rs2state.ps1 -Action save    -Snapshot path\to\snapshot
powershell -File tools/rs2state.ps1 -Action restore -Snapshot path\to\snapshot
```

Restore, capture, compare.

Two things about doing that reliably, both found by a capture that disagreed
with four others:

- **Stop the program before restoring.** It writes its state while shutting
  down, so an instance still on its way out overwrites a restore that has
  already happened. That produced one wrong capture in a run of good ones, and
  it looked exactly like the change under test. `rs2state.ps1` now waits for
  the process to be gone before it copies anything.
- **Wait for the program to say it is ready, not for a clock.** A fixed settle
  caught the loading screen about one run in four when the machine was busy,
  and a photograph of the loading screen looks exactly like a renderer that
  lost the scene. `rs2shot.ps1 -WaitForLog` polls the log for a marker
  instead; under `-fixture` the marker is `world stopped`, which only appears
  once the layout is loaded and the world has been stopped.

A short settle after the marker is still wanted: the camera converges on its
target over frames, and capturing immediately left a few dozen pixels
different between runs. Eight seconds is enough here.

Verified: five captures with all of that in place are byte-identical, and stay
so across a normal session run in between.

The snapshot lives outside the repository. It is someone's layout, it is
several megabytes, and it is not source.

### What else it takes to be deterministic

Getting five consecutive runs to agree on every pixel took more than stopping
the simulation. Each of these was measured, not guessed:

| | why it moved |
|---|---|
| the tick budget | the clock hands over several ticks at once after a stall, so an unclamped total froze the world in a different state each run |
| mouse input | the program warps the system cursor to the window centre every frame and reads back the difference, so the first frame reports however far the mouse happened to be - and the camera consumes that delta. **81% of pixels differed between two captures because of this.** In fixture mode the delta is discarded |
| the cursor overlay | its position accumulates those same deltas and its alpha fades over frames; two captures disagreed about whether it was on screen at all. Not drawn while frozen |
| the blink phase | signals and markers blink once per rendered frame, so a capture catches them anywhere in the cycle, and the frame count when the world stops depends on how long start-up took. Pinned to a stated phase |
| the compass overlay | still differs, cause not established - see below. Not drawn while frozen |

With those in place, five runs produce byte-identical viewports: all ten
pairwise comparisons report `identical`.

### The compass, and what is not known about it

The compass and its wind strip are the one thing that kept differing after
everything else agreed, by about 400 pixels in a band near the bottom centre.
It is a heads-up overlay rather than part of the world, so the fixture leaves
it out - but that is a gap in coverage and worth stating plainly.

The obvious explanations were ruled out by measurement:

- `g_FovRatio` and the camera position and direction were logged over more
  than a thousand frozen frames and did not change in any digit;
- freezing the wind strip it scrolls does not help;
- it is not the camera drift the mouse used to cause, which is fixed
  separately and which this band survived.

So the cause is inside the two compass objects. Finding it is open work.

## Checking a change

```
powershell -File tools/rs2shot.ps1 -Exe before.exe -Out before.png -ExtraArgs "-fixture"
powershell -File tools/rs2shot.ps1 -Exe after.exe  -Out after.png  -ExtraArgs "-fixture"
python tools/scenecheck.py --min-edge 1.0 before.png after.png
python tools/scenecheck.py --compare before.png after.png
```

The floor catches a scene that vanished. The comparison catches a scene that
changed, and it should say `identical` - reach for `--tolerance` only when you
have read the bounding box and know what is in it.
