# agents-md

A GitHub template that keeps one set of AI coding rules for Codex, Claude Code
and Kiro. [AGENTS.md](https://agents.md) is the single source of truth; every
other tool-specific file points back to it instead of duplicating it.

## Layout

```
.
├── AGENTS.md              # The rules. Read directly by Codex and generic agents.
├── CLAUDE.md              # Claude Code entry: `@AGENTS.md` + Claude-only additions.
├── CHANGELOG.md           # Required by the change-log rule in AGENTS.md.
└── .kiro/agents/dev.json  # Kiro custom agent that loads AGENTS.md as a resource.
```

## How each tool picks up the rules

| Tool | Mechanism |
|---|---|
| Codex / generic agents | Read `AGENTS.md` at the repo root natively. |
| Claude Code | Reads `CLAUDE.md`, whose first line `@AGENTS.md` imports the rules. |
| Kiro (IDE / CLI / Web) | Reads root `AGENTS.md` natively. The `dev` agent also lists `file://AGENTS.md` in `resources`, so the rules still load when `chat.disableInheritingDefaultResources` is on. |

Kiro `steering/`, `specs/` and `hooks/` are intentionally omitted: specs are
generated on demand, and the other two are only needed for Kiro-specific
behavior. Add them when you need them.

## Usage

1. Click **Use this template** on GitHub, or copy the files into an existing repo.
2. Fill in the `## Project` section of `AGENTS.md`, and drop the sections that
   do not apply (e.g. `Python conventions`).
3. Put shared rules in `AGENTS.md` only. Tool-specific additions go in
   `CLAUDE.md` or `.kiro/agents/*.json`.
4. Replace this README with your project's own.

## Verify

```bash
kiro-cli agent validate --path .kiro/agents/dev.json
kiro-cli chat --agent dev        # then: /context show
claude                           # then: /memory
```

Note: in Kiro agent configs, `resources` paths resolve from the workspace root
(`file://AGENTS.md`), while `prompt` paths resolve from the config file's
directory.
