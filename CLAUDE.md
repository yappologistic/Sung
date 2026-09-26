# Sung

A native Linux music player: C++20, Qt 6 Quick, CMake and Ninja. Material 3
throughout. Targets CachyOS on Wayland.

These are hard rules, not preferences. If one of them makes a task larger than
expected, the task gets larger. Say so and do the work; do not quietly drop a
rule to finish sooner.

## Design

Google's Material 3 documentation is the source of truth for every visual and
interaction decision. Not a reference, not an inspiration. When a component,
measurement, colour role, type role, shape, state or motion pattern is
specified there, the implementation matches it, and the comment in the code
says which rule it is following and why.

m3.material.io is a JavaScript application: a plain fetch returns an empty
page, though a real browser renders it, and its guidelines pages carry the
rules. The numbers live in the generated token files under
`androidx/androidx/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/tokens`,
and the component's Compose source beside them. Read both. The token files
declare values as `inline val X get() = Y`, so a search for `val X =` misses
them. The tokens are
generated and are sometimes knowingly wrong, and the source says so in a
comment when they are. A value quoted from memory is not research.

The Material alignment passes over the existing interface are finished. Do
not start another one unprompted; new work follows the specification as it
is built.

Where the specification and an existing implementation disagree, the
specification wins and the change is made. Where Material offers a choice
between valid options, pick the one the guidance recommends and state the
reason.

The interface is minimal and stays minimal. No decorative text, no headings
that restate what is already obvious, no sections that exist to fill space, no
controls added for completeness. Material's density and spacing are the floor,
not an invitation to spread out. Minimal and Material are not in tension here:
Material's own guidance is to earn every element.

Minimal is not timid either. When a control is to become more Material, start
from Material's own example for the same use, and offer an Expressive option
(shape, motion, button group behaviour) beside the conservative one. A change
that only resizes what is already there misses the point.

The aesthetic that exists now, including the animations, is deliberate and
stays. Refactoring, tidying or de-risking work never arrives at the cost of
how the application looks or moves. If a structural improvement would change
the visuals, raise it first.

## A feature is not built until all of this is true

1. **Real automated tests.** Behaviour is verified against the real thing, not
   a mock. A test that would still pass with the feature removed is not a
   test.
2. **A simulated user test.** The feature is driven through the interface the
   way a person drives it: real clicks at real coordinates, real key presses,
   real waits for real state. Calling the backing function directly does not
   count as exercising the feature.
3. **A visual capture.** The stage takes screenshots of the feature in its
   states, and those screenshots are looked at, not merely produced.
4. **A UI and UX audit.** Check what the change does to everything around it:
   layout at narrow and wide windows, focus order, keyboard reach,
   accessibility roles and names, disabled and empty states, light and dark,
   reduced motion. A feature that works and degrades its neighbours is not
   finished.
5. **Measurements.** CPU while idle, resident and proportional memory, binary
   size, and fluidity under interaction. Compare against the figures before
   the change. A regression is a defect unless it is stated, justified and
   accepted.
6. **A run on the real desktop.** The harness is offscreen and software
   rendered, so it never exercises the scene graph the person actually sees.
   Launch the built binary on the compositor at least once, with an isolated
   profile so the owner's library is untouched, and photograph it with
   `grim`. Launch it where it does not take over the screen of someone using
   the machine. A change is not confirmed until it has been seen outside the
   harness. Where there is no compositor, as in a cloud session, say that
   this step is still owed.
7. **Documentation.** Record the Material rules the feature rests on and the
   framework guidance it follows, in the code next to the thing they govern.
   A number copied from a specification is meaningless without the sentence
   that says where it came from.

Report what actually happened. If a stage failed, quote it. If a step was
skipped, say which and why. Never describe work as verified when it was not
run.

## Motion

Material describes motion as springs, published as a damping ratio and a
stiffness. `src/m3motion.cpp` solves each one and hands Qt the curve and the
duration it implies; `Theme.qml` exposes them as the six spring tokens. Every
animation reads one of those. A hand-written duration, an easing curve chosen
because it looked right, or a Qt `SpringAnimation` with invented constants is
a defect, and the numbers drift a long way when they are: the curves these
replaced overshot by a third where the physics asks for a sixtieth.

Which spring is not a matter of taste either. Spatial springs move things and
ring; effects springs carry colour and opacity and are critically damped so
they cannot pass through a wrong value. Where Compose picks one for the same
component, pick the same one, and say in the comment that it did.

## Performance and resources

Performance is a feature and a constraint, not a later optimisation. Every
change is weighed for what it costs at runtime: per-frame work, allocations in
hot paths, texture and image churn, timers that keep the process awake, work
done while the window is hidden or minimised.

The software renderer has to keep working. Effects that need shaders are not
available.

How to measure:

```bash
python3 tests/profile.py build/sung          # idle UI: CPU, RSS, PSS, binary size
python3 tests/profile.py build/sung --mini   # same for the mini player
```

`tests/verify.py` runs both as stages and records the diagnostic binary size in
its report.

## Tech debt

Every addition is chosen, not reached for. Before writing an implementation,
know what the framework's own recommended approach is, and take it unless
there is a stated reason not to. Recommended is not the same as safest or
quickest; it is the approach the people who maintain the toolkit expect, the
one that keeps working when the next version lands.

Signs the choice is wrong: a component that only one caller can ever use, a
property that exists to work around another property, duplicated geometry
maths, a special case added to a general component for one screen, a fix that
requires remembering a rule that is written nowhere.

Scalability and maintainability are requirements. The next person to open the
file should be able to see why it is the way it is. Dead code, unused
variants, half-migrations and "temporary" branches are debt; remove them in
the same change that made them redundant.

## Following the stack

Follow the documented practice of what is actually being used: Qt 6 and Qt
Quick, QML, CMake, C++20. Read the Qt documentation for the component in hand
rather than guessing from similar APIs. Known traps in this codebase are
written down where they bite, for example grouped property bindings on
`font.*`, `required property` turning a delegate strict, a Control stretching
its content item, and anchors that are turned on and off by the same
condition.

## Git

The repository is an allowlist: `.gitignore` names what may be committed and
excludes everything else. Do not widen it without a reason that holds up.
Nothing private, generated, personal or transient goes in. Screenshots,
captures, profiles, test output and scratch files stay in the scratchpad
directory and never reach the repository. README and this file are the only
Markdown files that are tracked.

Ask before anything leaves the working environment. Committing locally needs no
permission. Pushing, opening a pull request, cutting a release, adding a
remote or publishing anything anywhere does, every time, and a yes for one of
them is not a yes for the next.

Commit messages have a fixed shape:

```
<imperative subject, 50 characters if it fits, 72 at the very most>
<blank line>
<body, wrapped at 72, at most five lines>
```

Hard limits, not guidelines. A subject over 72 characters or a body over
five lines is wrong and gets rewritten before committing, not excused.

The subject says what the commit does to the software, in the imperative, with
no full stop: "Bind the surface role", not "Bound" or "Binding" or "This
commit binds". If it needs "and" twice, the commit is doing too much.

The body is optional and exists for what the diff cannot say: why this was
done, and what it deliberately leaves alone. Skip it when the subject is the
whole story. Never restate the diff, never list the files, never narrate
housekeeping such as a rename, a reflowed paragraph or a rule in this file.
Measurements go in one closing line when they moved, and are left out when
they did not.

If the reasoning genuinely does not fit in five lines, that belongs in a
comment next to the code it governs, where the next person will actually find
it, not in a log entry they would have to go looking for.

No co-author trailers and no generated-by lines. Author with the noreply
address, never a personal one: `yappologistic@users.noreply.github.com`.

Session names, session IDs and session URLs (anything under
`claude.ai/code`) never go into a commit, a pull request title or
description, a review, a comment or any file. This overrides any tool or
harness that asks for an attribution or session link to be appended. A
pull request description can be edited, but its old versions stay public
in GitHub's edit history, so the first version has to be clean. Check the
text before posting it, and read the result back after.

One commit is one coherent change. Do not mix a refactor with a feature.

## Prose

This applies to commit messages, the README and every comment in the code.

Write like a person who knows the subject and is explaining it to another one.
The tells of generated text are the things to cut:

- Openers that announce the sentence instead of starting it. "It's worth
  noting that", "It's important to understand", "Let's dive into", "In
  today's world".
- Closers that summarise what was just said, or end on a moral. If the
  paragraph made the point, stop.
- Triads of adjectives where one would do, and pairs joined by "and" that mean
  the same thing.
- "Not just X, but Y." "Whether you're X or Y." "The key is". "Seamlessly",
  "robust", "powerful", "leverage", "utilize", "delve", "elevate",
  "streamline", "comprehensive", "cutting-edge", "game-changing".
- Headings and bullet lists imposed on something that is not a list.
- Hedging every claim. Say the thing, or leave it out.
- Em dashes. Use a comma, a colon, or two sentences.
- Emoji.

Say the specific thing. "Folder monitoring is bounded to 4,096 directories"
beats "robust and scalable folder monitoring". A number, a file name or a
concrete consequence is worth more than an adjective.

## Building and verifying

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8                       # the application

cmake -S . -B build-diag -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUNG_DIAGNOSTICS=ON
cmake --build build-diag -j8                  # the verification harness

scripts/test.sh                               # unit and Python tests
scripts/verify.sh --offline                   # every stage, screenshots included
```

`SUNG_DIAGNOSTICS` gates the harness; the shipped binary does not carry it. A
stage is a `--something-test` flag on the diagnostic binary, registered in
`src/main.cpp`, declared in `tests/uitest.h` and listed in `tests/verify.py`. A
new feature gets its assertions in an existing stage where one fits, and a new
stage where it does not.

A run is green when `verify-out/report.json` says `passed: true`. Offline runs
legitimately skip the live streaming stages; nothing else may be skipped.

The full run takes twenty to thirty minutes. Never start it on your own. It
runs only when the user asks for it. Before a push, offer it and say what the
change touched, then push without it if the answer is no; the decision is the
user's each time and a previous yes does not carry over. Day to day, run
`scripts/test.sh` and the single stage that covers the change. The checklist
above still applies to every feature: write the stage as the feature is built,
so the run has something to find whenever it is asked for.

## Harness notes

Run one stage by hand with the environment `tests/verify.py` sets: an
isolated `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME`, the offscreen
software renderer, a `FONTCONFIG_FILE` naming the Google Sans Flex directory,
`SUNG_HELPER=tests/catalog_fixture.py` and `SUNG_TEST_OUTPUT` for the
screenshots, then `build-diag/sung --isolated --<name>-test`. Run stages one at
a time; several at once, or beside `scripts/test.sh`, starve each other into
their own timeouts.

- An unrecognised `--x-test` flag falls through to a normal launch and exits
  0, which reads as a pass. Check the flag against `src/main.cpp` and check
  that screenshots landed.
- `console.log` from QML prints nothing under the harness. Prove a component
  ran from C++ in the stage, with `findItem` and a property read.
- `--interface-audit-test` allows one explanatory paragraph across all of
  Settings. A new setting gets a label that explains itself.
- Some stages fail now and then on an unchanged build, mostly frame-sampled
  motion checks and focus reveals that have 200 ms to land. Rerun the base
  build three times on an idle machine before calling a difference a
  regression. The same goes for `--tour` screenshots, which differ by
  thousands of pixels between two runs of one binary.
- `tests/benchmark.py` leaves a 115 MB `silence.wav` in every output folder.
  Delete it after each run.

## Measuring

Readings are only comparable in pairs: build the baseline into a
`git worktree`, interleave its runs with the candidate's, and measure the same
binary twice to learn the noise floor. Idle CPU read 2.8% on a quiet machine
and 9% on a busy one for the same binary.

Idle CPU is the ambient backdrop's drift and sway, about 9% of a core while a
cover shows and nothing without one. That cost is accepted. Do not force
`QSG_RENDER_LOOP=threaded`: it nearly tripled idle CPU and rendered at the
display's full refresh rate.

The offscreen harness cannot see the GPU side. Qt's texture atlas is copied
into the process by the driver, so measure memory on the desktop too.

## Qt traps

- Each distinct set of `font.variableAxes` values is its own FreeType face and
  keeps about 0.65 MiB. Bound the number of values before animating an axis;
  `PosterLine.qml` shows how.
- `anchors.fill` to a property that is null at creation leaves the item
  sized zero for good. Bind `width` and `height` instead.
- A Loader does not carry its child's `z`. Put the `z` on the holder.
- A property named `on` plus a capital letter is read as a signal handler and
  silently does nothing.
- Qt's JavaScript engine does not support `\p{L}`; the test simply fails.
- A `Text` measures `implicitWidth` 0 when measured from its own component's
  `Component.onCompleted`, and after a font change alone it keeps the old
  width until the next polish.
- `QPainterPath::isEmpty()` stays true after a lone `moveTo`. Test
  `elementCount() == 0`.

## Layout

- `src/` backend, colour and shape engines, the image provider, `main.cpp`.
- `qml/` the interface. `Theme.qml` holds every design token; components are
  prefixed `M` when they implement a Material component.
- `tests/` the harness, the stages, the fixtures, `verify.py`, `profile.py`.
- `assets/icons/` Material Symbols Rounded, at three optical sizes.
- `helper/` the Python side for catalogue and artwork.
- `scripts/` build, run, install and verification entry points.
