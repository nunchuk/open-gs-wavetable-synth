# AGENTS.md

Single source of truth for AI collaboration rules in this repository. Read by
Codex and generic agents directly, by Claude Code via `CLAUDE.md` (`@AGENTS.md`),
and by Kiro natively (plus `.kiro/agents/*.json` resources). These instructions
override default behavior. Put shared rules here; tool-specific additions go in
the tool's own entry file.

## Project

A bit-exact C++20 reimplementation of the Microsoft GS Wavetable Synth (the DirectMusic synth
core of Windows' `SysWOW64\wdmaud.drv` 10.0.20348.2849), with an MCI-style MIDI file player and
Windows' 22 050 -> 48 000 Hz converter. README.md states what it does and how it was verified.

### Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
GSWAVETABLE_DLS=/path/to/gm.dls ctest --test-dir build -R golden_gm   # also the gm.dls goldens
```

### Code map

| Path | What |
|---|---|
| `include/gswavetable/engine.hpp`, `src/engine.cpp` | The synth: DLS loader (`load_dls`), `Synth`, the tables. Comments give the field offsets and function RVAs of the Windows binary. |
| `include/gswavetable/sequencer.hpp`, `src/sequencer.cpp` | `Sequencer`: a Standard MIDI File as `mciseq.dll` orders it, scheduled on the exact tempo map. |
| `include/gswavetable/player.hpp`, `src/player.cpp` | `Player`: play / stop / pause / resume, rendering in Windows' 10 ms cycles. |
| `include/gswavetable/resampler.hpp` | The 22 050 -> 48 000 Hz converters (header only): `TransparentResampler` (the default) and `WindowsResampler` (Windows 10's audio engine's). |
| `tools/render.cpp` | `gswavetable-render`, the CLI. |
| `tests/` | `tables_test`, `sequencer_test`, `resampler_test`, `golden_test`. |
| `tests/data/` | `golden.tsv`, the probe and feature MIDI files, `sine.dls`, and the scripts that generated `sine.dls` and `features.mid`. |

### Invariants

- **No change may alter a golden hash** (`tests/data/golden.tsv`, `tables_test`'s hash). They pin
  the output of the engine as it was verified against Windows; a different hash means a different
  sound.
- **A change to the synth's arithmetic needs sample-by-sample evidence** that it matches Windows
  (a recording of the Windows synth, replayed), stated in the change log entry. Plausible-looking
  "fixes" without it are regressions.
- **Never commit `gm.dls`**, other Windows files, or MIDI files that are not ours to distribute.
- Portable C++20 only: no compiler extensions, no dependencies, warning-free with
  `-Wall -Wextra -Wpedantic -Wconversion` (and MSVC `/W4`), no floating-point contraction.
- Every `Synth` or `Player` is used by one thread at a time; a loaded `Bank` is immutable.

## Working principles (from Andrej Karpathy's notes on common LLM coding mistakes)

Source: https://github.com/multica-ai/andrej-karpathy-skills

1. **Think Before Coding** — don't assume, don't hide confusion, surface tradeoffs.
2. **Simplicity First** — the minimum code that solves the problem; nothing speculative.
3. **Surgical Changes** — touch only what you must; clean up only your own mess.
4. **Goal-Driven Execution** — define success criteria, loop until verified.

## Python conventions

Python is used only for the test-data generators in `tests/data/` (standard library only).

- Use `python3` / `pip3` — never bare `pip` / `python`.
- Should a package ever be needed, work inside a virtualenv and pin versions in `requirements.txt`.
- `.venv/` and `.env` are gitignored. Never commit secrets.
- Type hints, PEP 8, docstrings on functions and classes. Use `black` / `flake8` when available.

## Language

- **All code comments, documentation, and CLI output are in English.** (Conversation
  with the user may be in another language; this rule is about the codebase only.)

## Doc & style discipline

- Do not create README/guide/summary files or add emojis unless explicitly requested.

## Change log

- **Every change updates `CHANGELOG.md` in the same commit.** Follow the existing
  `## [x.y.z] - YYYY-MM-DD` + `### Added / Changed / Fixed` format.
- Work that is not part of a release goes under `[Unreleased]`. Do not consume a
  version number ahead of an actual release.
- Record **why**, not just what — especially for a fix. A one-line "fixed X" is
  useless six months later; the diagnosis is the valuable part. Include the
  evidence that identified the root cause (measured numbers, exact error text)
  when there is one, since that is what makes a regression recognisable.

## Git conventions

### Conventional Commits

Format: `<type>: <subject>`, with a body explaining "why / what" when useful.

- Common types: `feat`, `fix`, `docs`, `refactor`, `chore`
- Subject in English, imperative mood, concise; the optional body covers motivation
  and scope
- Examples:

  ```
  docs: add README and repository scaffolding
  feat: add CLI entry point for batch export
  fix: pin arm64 slice so native wheels load
  ```

### Atomic commits

Group by logic — one commit does one kind of change (e.g. docs / core config /
scripts separately). Do not mix unrelated changes into one commit.

### Linear history

Use rebase when integrating upstream changes (`git config pull.rebase true`) to
avoid pointless merge commits.

### Commit timing and safety

- **Only commit / push when the user explicitly asks.** Never commit or push on
  your own initiative.
- **Never force-push** over remote history (unless the user explicitly asks and
  understands the consequences).
- Never commit secrets. `.env` is gitignored — verify with `git status` before
  every commit that it is not staged.
- Pre-push self-check: `git status` clean, `git log --oneline` sensibly grouped.
