# Handoff Protocol — Claude Code ⇄ Neo (AIK)

This folder is a **file-based message bus** between two AI agents working on the
CLEARANCE project. They cannot talk to each other directly, so they leave
messages here. Jeremy (the human) triggers each agent to "check the handoff."

## The two channels

| File             | Written by   | Read by      |
|------------------|--------------|--------------|
| `to-neo.md`      | Claude Code  | Neo (AIK)    |
| `to-claude.md`   | Neo (AIK)    | Claude Code  |

Each agent **only writes to its own outbound file** and **only reads the other's**.
Never overwrite the other agent's file — that prevents clobbering.

## Who does what

- **Claude Code** (VS Code): C++ source in `Source/`, `.Build.cs`, `.uproject`,
  `Config/*.ini`, refactors, build/log reading, git. Owns the **code/text layer**.
- **Neo** (Unreal editor): Blueprints, levels, actors, StateTree/Behavior Tree
  assets, materials, UMG, Play-In-Editor testing. Owns the **live-editor layer**.

The handoff line between them is the **C++ ⇄ Blueprint boundary**
(`UFUNCTION(BlueprintCallable)`, `UPROPERTY(EditAnywhere)`, etc.).

## Entry format

Append a new entry to the TOP of your outbound file (newest first). Use this shape:

```markdown
## [YYYY-MM-DD HH:MM] <From> → <To>
STATUS: ready | waiting | blocked | done
TASK: one-line summary
DETAILS:
- bullet points: what changed, what to do, exact class/property/node names
COMPILED: yes | no | n/a        (Claude sets this after C++ changes)
NEEDS BACK: what you want the other agent to confirm or return
```

## How a turn works

1. Agent A finishes its part, appends an entry to its outbound file with `STATUS`
   and a clear `NEEDS BACK`.
2. Jeremy tells Agent B: "check the handoff file."
3. Agent B reads the other's file, does the work, appends its reply.
4. Repeat. There is **no live notification** — the human is the trigger.

## Rules of thumb

- After Claude changes C++, nothing in the editor updates until a **successful
  compile**. Claude marks `COMPILED:` so Neo knows whether to expect new nodes.
- Don't have Neo editing a Blueprint that derives from a class Claude is mid-edit
  on. Finish C++ → compile → then wire it in the editor.
- Keep entries short and concrete. Exact names beat prose.
