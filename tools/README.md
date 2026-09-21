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

### What it takes to be deterministic

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
