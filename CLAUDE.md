# SSB64 PC Port — Claude Session Context

PC port of Super Smash Bros. 64 built from the complete decompilation at github.com/VetriTheRetri/ssb-decomp-re. Target integration: libultraship (LUS) + Torch asset pipeline.

## Documentation

Detailed reference material lives under `docs/`. Read the file that matches the task before touching code. When looking for a topic not listed here, run `ls docs/` and `ls docs/bugs/` to see what's available.

| Topic | File |
|-------|------|
| Project status, ROM info, dependencies, source tree layout | `docs/architecture.md` |
| C type system, decomp naming prefixes, code style, macros | `docs/c_conventions.md` |
| RDRAM / RSP / RDP / GBI / audio / threading / controller / endianness | `docs/n64_reference.md` |
| CMake build, reloc stub regen, runtime logs, LP64 compat notes | `docs/build_and_tooling.md` |
| GBI trace capture (port + M64P plugin) and `gbi_diff.py` usage | `docs/debug_gbi_trace.md` |
| IDO BE bitfield layout audit (compile + rabbitizer disasm to verify port struct bit positions) | `docs/debug_ido_bitfield_layout.md` |
| Resolved bugs (index + per-bug root cause / fix write-ups) | `docs/bugs/README.md` |
| Linux menu rendering bug family (CSS/pause/HUD sprites, upstream delta) | `docs/linux_menu_rendering_fixes_2026-05-22.md` |
| Netplay: sim tick authority vs `hr` / VI / admission bias | `docs/netplay_timebase_authority.md` |
| Netplay rollback boundary + decomp gating pattern | `docs/netplay_rollback_refactor_contracts.md` |
| FTStatusVars union overlays, witness, stomp diagnosis | `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md` |
| Android port status + offline/netplay APK CI | `docs/android_port_status_2026-05-01.md` |

Ongoing investigations and handoff notes are loose `.md` files at the top level of `docs/` — check there before starting work on rendering, collision, or animation issues so you don't duplicate prior effort.

When you fix a new significant bug, add an entry under `docs/bugs/` using the slug pattern `<topic>_<YYYY-MM-DD>.md` and link it from `docs/bugs/README.md`.

---

## GitHub Issue Access

When asked to inspect GitHub issues, prefer the GitHub connector. If issue tools
are not visible yet, first run tool discovery for "GitHub issue fetch/view" so
the connector exposes `_fetch_issue` and `_fetch_issue_comments`, then fetch with
`repository_full_name: "JRickey/BattleShip"`.

The local GitHub CLI is also authenticated as `JRickey` and has admin access to
`JRickey/BattleShip`. If the human-formatted `gh issue view` output is blank or
unreliable, use the JSON/template path instead:

```bash
gh issue view <number> -R JRickey/BattleShip --json number,title,state,author,body,url,comments,labels
```

Known-good check from 2026-06-01: issue #209 and its comments were accessible
through both the connector and `gh --json`; #209 had no comments at that time.

---

## Parallel Sessions — Worktree Workflow

Multiple Claude windows working in the same checkout will clobber each other's source edits and build outputs. **Every parallel session works in its own git worktree.**

### Spinning up a new worktree

```bash
./scripts/new-worktree.sh <slug>           # configure only (fast)
./scripts/new-worktree.sh <slug> --build   # configure + full Debug compile
./scripts/new-worktree.sh <slug> --base some-branch --release
```

Output lands at `.claude/worktrees/<slug>` on branch `agent/<slug>`. The script:
1. Creates the worktree and branch.
2. Symlinks `baserom.us.z64` (gitignored, too large to duplicate).
3. **Independently clones `libultraship`, `torch`, and `decomp`** from the main tree's local submodule checkouts (picks up pinned SHAs that may not be pushed to the forks yet), then resets each submodule's `origin` to whatever URL the main tree's submodule uses — usually SSH so pushes work.
4. Regenerates gitignored codegen (`reloc_data.h`, `yamls/us/reloc_*.yml`, credits encodings).
5. Runs `cmake -B build` inside the worktree (and compiles if `--build` given).

### What this gives you

- **Full edit authority everywhere** — any file under `decomp/src/`, `decomp/include/`, `port/`, `libultraship/`, `torch/` is fair game. Submodule checkouts are real independent clones, not symlinks.
- **Zero collision** with other windows on source, build artifacts, or submodule state.
- **Normal git flow for submodule changes**:
  1. Edit and commit inside `<worktree>/decomp/`, `<worktree>/libultraship/`, or `<worktree>/torch/`.
  2. Push to the fork: `git -C <worktree>/<sm> push origin <branch>`.
     - `decomp` → `port-patches` on `JRickey/ssb-decomp-re`
     - `libultraship` → `ssb64` on `JRickey/libultraship`
     - `torch` → `ssb64` on `JRickey/Torch`
  3. In the outer worktree, bump the submodule pointer: `git add <sm> && git commit -m "Bump <sm>: <summary>"`.
  4. When the outer branch lands on main, the pointer update goes with it.

### Merging back to main

The outer worktree is a normal branch (`agent/<slug>`). Merge or PR it into `main` like any other branch. Submodule pointer bumps ride along in the commits.

### Cleanup

```bash
git worktree remove .claude/worktrees/<slug>
git branch -D agent/<slug>
```

Stale worktrees under `.claude/worktrees/` from past sessions are fine to remove — check `git worktree list` and prune anything you don't recognize.

### Gotchas

- **Never use relative `build` paths in Bash tool calls** — Claude Code resets cwd between `Bash` calls. `cmake --build build` from the project root builds the main tree, not the worktree. Always use absolute paths: `cmake --build <worktree>/build ...`.
- **Cap build parallelism at `-j 4` on the M1 16 GB machine.** Bare `-j` lets make spawn one clang per logical core; libultraship's Debug C++ TUs hold 1–2 GB resident each, so 8 in parallel push the laptop into swap and pin the fans for the whole compile. Always: `cmake --build <worktree>/build --target ssb64 -j 4`. Push to `-j 6` only when you know nothing else heavy is open. Worktree first-builds are full from-scratch (no shared cache with main tree), so this matters most the first time.
- The binary loads `BattleShip.o2r` (ROM-derived, user-extracted) and `f3d.o2r` (shaders) from its CWD at launch; without them it exits with `archive ... does not exist`. `new-worktree.sh` symlinks both from the main tree's `build/` into the worktree's `build/`. If the main tree has never been extracted, run `cmake --build <main-tree>/build --target ExtractAssets` there first so the symlinks resolve. (`ssb64.o2r` was an early-development port-asset archive; it is no longer produced or loaded — the build never contains ROM-derived data beyond the user's own first-run `BattleShip.o2r`.)

---

## Agent Directives

### Pre-Work

1. **THE "STEP 0" RULE**: Before any structural refactor on a file >300 LOC, first remove dead code, unused exports, unused imports, and debug logs. Commit cleanup separately.

2. **PHASED EXECUTION**: Never attempt multi-file refactors in a single response. Break work into phases. Complete Phase 1, run verification, wait for approval before Phase 2. Max 5 files per phase.

### Code Quality

3. **THE SENIOR DEV OVERRIDE**: If architecture is flawed, state is duplicated, or patterns are inconsistent — propose and implement structural fixes. Ask: "What would a senior, experienced, perfectionist dev reject in code review?" Fix all of it.

4. **FORCED VERIFICATION**: Do not report a task complete until you have run the build and fixed all errors. If no build is configured yet, state that explicitly.

5. **DECOMP PRESERVATION — preserve behavior, not byte-matching**: The decomp describes the *game*, not the build. Keep IDO idioms (goto, odd casts, temp variables) that encode original N64 semantics — those are load-bearing and must not be "modernized." But don't preserve **compiler compat shims** (warning suppressions, permissive flags, header shortcuts) that hurt port stability just to avoid touching decomp source. If a suppressed diagnostic is masking real bugs on modern LP64 toolchains (e.g., `-Wno-implicit-function-declaration` silently truncating 64-bit pointer returns to `int`), fix the root cause — add the missing include, wrap a port fix in `#ifdef PORT`, or adjust the decomp file itself — rather than keeping the suppression. **Accuracy to game behavior > accuracy to ROM bytes.** When choosing between stability and ROM-matching, choose stability and document the deviation in `docs/bugs/`.

6. **FTSTATUSVARS / UNION STOMP — structural fix, not mirrors**: `FTStruct.status_vars` is a union of per-status overlays (`entry`, `catchwait`, `rebirth`, …) that all alias the same bytes. A **union stomp** is when code reads/writes one overlay while a different overlay is live for the current `status_id`, silently corrupting gameplay state. This class of bug is amplified in netplay because rollback snapshots `memcpy` the whole blob with no overlay tag.

   **Do not add new out-of-union mirror fields** (e.g. caching a union value in a separate `FTStruct` field and preferring the mirror in getters) as the primary fix. Existing mirrors (`hit_lr`, `shuffle_tics`) are legacy band-aids — leave them alone unless removing them as part of the migration, but **never introduce new ones**. `dead_gate_wait` is netmenu-only (`PORT && SSB64_NETMENU`); offline uses JRickey union `dead.wait` only.

   **The approved fix path (Approach C):**
   1. **Accessors** — route all `status_vars.common.*` reads/writes through `decomp/src/ft/ftstatusvars.h` (`ftStatusVarsEntry()`, `ftStatusVarsCatchWait()`, …). Never add raw union access in new or touched code.
   2. **Witness** — diagnose stomps with `SSB64_NETPLAY_STATUSVARS_WITNESS=1`. Compare `accessed` vs `expected` overlay from the ownership table; integrity checks flag union/mirror divergence. Extend `syNetplayStatusVarsWitnessFillRange` for false negatives before guessing.
   3. **Parallel storage (C2)** — the end-state is per-overlay storage that eliminates aliasing so mirrors can be deleted. Snapshot serialization gets overlay tags in the same milestone.

   When a gameplay or netplay bug smells like stale/wrong per-status state (premature throw, wrong facing on Appear, rebirth halo drift, etc.), read `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md` first, migrate the call site to accessors, run the witness, then fix the stomping writer — not another mirror.

7. **OFFLINE vs NETMENU — we do not patch the offline build**:

   **Offline (`SSB64_NETMENU=OFF`)** = JRickey’s maintained PC port at the release decomp SHA. Trust official offline sim; do not add fork “fixes,” wrappers, mirrors, or debug tooling. **Netmenu** = all fork netplay/rollback work unless JRickey promotes a change upstream.

   | Layer | Mechanism | Offline binary | Netmenu binary |
   |-------|-----------|----------------|----------------|
   | **Product** | CMake `SSB64_NETMENU` | JRickey release parity | JRickey + fork netplay |
   | **Compile** | `#if defined(PORT) && defined(SSB64_NETMENU)` | Fork net blocks **preprocessed out** | Fork net code compiled |
   | **Runtime** | `syNetplayRollbackSemanticsActive()` | N/A (code absent) | Active VS/resim only |

   - **`#ifdef PORT` alone** — only for code **already in JRickey `port-patches`** at the release baseline (reloc/LUS paths JRickey ships). Any **fork-only** `#ifdef PORT` block is assumed netplay work → gate `PORT && SSB64_NETMENU` or revert until JRickey review.
   - **`SSB64_NETMENU`** — netplay compiled and linked (`port/net/**`, `decomp/src/netplay/**`, libcurl, `debug_tools/`).
   - **Promotion** — offline benefit from a netplay fix requires JRickey review; widen IFDEF or upstream merge, never self-ship in offline.

   **Required decomp pattern** (fighters, stages, items):

   ```c
   #if defined(PORT) && defined(SSB64_NETMENU)
   /* SSB64_NETMENU: stripped from offline builds. Runtime: active VS/resim only. */
   /* Netplay rollback only: <one-line why>. See docs/bugs/<slug>.md. */
   if (syNetplayRollbackSemanticsActive() != FALSE)
   {
       ... /* rollback policy */
       return; /* Mario-style: netplay branch then vanilla below */
   }
   #endif
   /* JRickey vanilla forward sim — offline binary AND offline modes in netmenu binary. */
   ```

   - **Snapshot apply** in `port/net/sys/*` runs only during rollback load — no forward-sim gate.
   - Offline must **not** link `port/net/**`, rollback TUs, or `debug_tools/acmd_trace/`; use `port/stubs/net_port_glue_offline.c` + `netsync_hash_stubs.c` + `port/stubs/acmd_trace_stubs.c`. **`debug_tools/gbi_trace/`** links in all PORT builds (`SSB64_GBI_TRACE=1` at runtime).
   - Tracked checklist: `docs/decomp_upstream_divergence_audit_2026-06-03.md`. Full contract: `docs/netplay_rollback_refactor_contracts.md`.

### Context Management

8. **SUB-AGENT SWARMING**: For tasks touching >5 independent files, launch parallel sub-agents. Each agent gets its own context window.

9. **CONTEXT DECAY AWARENESS**: After 10+ messages, re-read any file before editing. Do not trust memory of file contents.

10. **FILE READ BUDGET**: For files over 500 LOC, use offset and limit parameters to read in chunks.

11. **EDIT INTEGRITY**: Before every edit, re-read the file. After editing, verify the change applied correctly. Never batch >3 edits to the same file without a verification read.

12. **NO SEMANTIC SEARCH**: When renaming or changing any function/type/variable, search separately for: direct calls, type references, string literals, dynamic references, re-exports, and tests.
