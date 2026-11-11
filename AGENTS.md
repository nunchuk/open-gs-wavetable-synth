# AGENTS.md

Single source of truth for AI collaboration rules in this repository. Read by
Codex and generic agents directly, by Claude Code via `CLAUDE.md` (`@AGENTS.md`),
and by Kiro natively (plus `.kiro/agents/*.json` resources). These instructions
override default behavior. Put shared rules here; tool-specific additions go in
the tool's own entry file.

## Project

<!-- TODO: one or two paragraphs on what this project is, its stack, and how to run it. -->

## Working principles (from Andrej Karpathy's notes on common LLM coding mistakes)

Source: https://github.com/multica-ai/andrej-karpathy-skills

1. **Think Before Coding** — don't assume, don't hide confusion, surface tradeoffs.
2. **Simplicity First** — the minimum code that solves the problem; nothing speculative.
3. **Surgical Changes** — touch only what you must; clean up only your own mess.
4. **Goal-Driven Execution** — define success criteria, loop until verified.

## Python conventions

- Use `python3` / `pip3` — never bare `pip` / `python`.
- Always work inside the virtualenv; activate it before installing. Pin versions in `requirements.txt`.
- `.venv/` and `.env` are gitignored. Never commit secrets; configuration comes from environment variables (`.env`).
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
