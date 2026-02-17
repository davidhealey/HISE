# Contributing to HISE

HISE is primarily maintained by one developer (Christoph), but community contributions are welcome and encouraged. Many HISE users are proficient with HISEScript and increasingly comfortable using AI coding tools to work with the C++ codebase. This guide helps you make contributions that are easy to review and safe to merge.

## Getting Started

### Prerequisites

- **Build HISE yourself.** Fork the repo, clone it, and get a working build using Projucer-generated IDE projects. You'll need this to test your changes and, for crash bugs, to run the debugger.
- **Understand the backend/frontend split.** HISE has two main build targets: the HISE IDE (`USE_BACKEND=1`) and exported plugins (`USE_FRONTEND=1`). Code that works in the IDE may behave differently — or not exist at all — in an exported plugin. See `guidelines/development/build-configurations.md` for details.
- **Check the forum and issues first.** Many bugs have been discussed on the [HISE Forum](https://forum.hise.audio) before reaching GitHub. Context from those discussions is valuable.

### Using the MCP Workflow (Recommended)

If you're using an AI coding tool (Claude Code, OpenCode, etc.) with the HISE MCP server, invoke the `contribute` prompt to start a guided workflow. The prompt walks your AI agent through:

1. **Prerequisites** — Verifies your GitHub CLI auth, HISE repo, and fork setup
2. **Assessment** — Searches the codebase for evidence, checks red flags, produces a risk verdict
3. **Fix** — Proposes a fix following established patterns, waits for your build/test confirmation
4. **Submission** — Generates the PR description with risk assessment, creates the PR via `gh`

If the assessment triggers red flags, the agent will create a GitHub issue for maintainer discussion instead of proceeding directly. Once the maintainer responds with guidance, re-invoke the prompt with the issue URL to resume.

### Manual Workflow

1. Fork the repository and create a feature/fix branch from `develop`
2. Make your changes, build in Debug, and test
3. Fill out the **Risk Assessment Checklist** (see below) in your PR description
4. Submit the PR against `develop`
5. Christoph reviews and merges, or provides feedback

## Deciding What to Contribute

Not every bug or feature is equally suited for community contribution. The key question isn't "how hard is the code?" — it's **"could this change affect existing HISE projects in unexpected ways?"**

### The Evidence Test

Before submitting a fix, ask yourself: **can I point to existing code in the HISE codebase that already does what my fix does?**

- **Yes** — You're filling a gap in an established pattern. This is a strong signal that the fix is correct and safe. Proceed.
- **No, but I can reason about why it should work this way** — You're making a design decision, not following a pattern. Flag this in your PR and expect discussion.

This is the single most important guideline. "Link to the precedent, don't make the argument." If you find yourself writing a paragraph justifying why your change is safe rather than pointing to a sibling function that already does the same thing, that's a signal to pause and reconsider.

### Where You Put the Change Matters

The same feature can have drastically different risk profiles depending on which layer you implement it at. Always ask: **is there a layer where this change has zero impact on existing behavior?**

In general, lower layers carry more risk:

| Layer | Risk | Examples |
|-------|------|----------|
| UI / Editor / Floating Tile properties | Lower | Adding a property to a floating tile panel, forwarding LAF draw properties |
| Scripting API (thin wrappers) | Lower | Exposing an existing C++ method to HISEScript |
| Module parameters / attributes | Higher | Adding or reordering attributes changes index-based access for all scripts |
| DSP / rendering pipeline | Higher | Changes here affect how every project sounds or performs |
| Scripting engine internals | Higher | Parser, preprocessor, include system, compilation pipeline |

If you can solve your problem at the UI layer instead of the module layer, do that. If you can wrap an existing C++ function instead of writing new DSP logic, do that.

### Good Candidates for Contribution

These types of changes are typically safe and welcome:

**Pattern consistency fixes** — A function is missing something that all its sibling functions have. A LAF callback doesn't forward a property that's available in scope. An API method doesn't set a flag that every other similar method sets. The surrounding code is your specification.

**Missing persistence** — A setting changes at runtime but isn't saved/loaded. The save/load methods already exist, they just aren't being called.

**Defensive guards** — A null check, bounds check, or file-existence check that prevents a crash or error. The fix adds safety without changing normal behavior.

**Scripting API wrappers** — An existing C++ method that would be useful from HISEScript but isn't exposed yet. The implementation is a thin wrapper following the established `API_METHOD_WRAPPER` / `ADD_API_METHOD` pattern.

**UI-layer additions** — New floating tile properties, LAF property forwarding, editor improvements. These live in `hi_components/` or editor subdirectories and don't affect DSP or the scripting engine.

**Pure additive features (opt-in)** — New functionality that is explicitly enabled via an API call or property, with zero impact on projects that don't use it. No existing code paths are modified, no existing behavior changes.

**HISE IDE improvements** — Minor UI fixes and enhancements to the HISE application itself: keyboard shortcuts for existing functions, visual glitches in the module tree or property editor, layout fixes in backend-only panels, tooltip improvements. These changes live in `USE_BACKEND`-only code and have zero impact on exported plugins or existing scripts.

### Needs Maintainer Input First

These changes should be discussed with Christoph before or alongside the PR:

**Feature additions to the scripting API** — Even if the implementation is small, new API methods extend the behavioral contract. Check the forum or open an issue to discuss the design first.

**DSP module changes** — Even if you're following existing patterns (e.g., adding a modulation chain like the existing ones), modules may be used at scale in ways you can't predict. Adding per-instance overhead to a module that's used 32 times in an additive synth patch is a form of breaking change.

### Always Escalate to Maintainer

These categories require architectural knowledge that goes beyond the immediate code:

- **Parameter index changes** — Any change that shifts, reorders, or reinterprets module attribute indices. Every script, preset, and automation mapping depends on stable parameter indices.
- **Scripting API method signature changes** — Adding, removing, or reordering parameters of existing API methods is a breaking change. HISE validates parameter count at compile time, so adding a parameter to an existing method (e.g., `Console.print(value, newParam)`) will cause every existing script that calls that method to fail compilation. This is stricter than languages like JavaScript where extra arguments are silently ignored. If you need additional parameters, discuss an overload or a separate method with the maintainer.
- **Backend/frontend behavioral differences** — Anything involving code paths that differ between `USE_BACKEND` and `USE_FRONTEND` (file resolution, resource loading, path handling, script compilation).
- **Scripting engine internals** — The parser, preprocessor, include resolution, `{BEGIN}/{END}` markers, `{GLOBAL_SCRIPT_FOLDER}` wildcards. These systems have invariants that must hold across the full export pipeline.
- **Serialization format changes** — Modifications to `exportAsValueTree` / `restoreFromValueTree` that could affect how existing presets or projects are loaded.
- **Audio routing architecture** — Send containers, channel counts, routing matrices, voice management.
- **Plugin/DAW contract** — Automation, parameter persistence, plugin lifecycle, host interface.
- **Established callback behavior** — Changes to when or how control callbacks, timer callbacks, or MIDI callbacks fire in existing features.

### Incomplete Features Create Cascading Problems

If your change introduces a new concept (like a directory hierarchy, a relative path scheme, or a new namespace), trace every system that would need to understand that concept. A fix that works for one part of the pipeline but breaks another part (export, frontend loading, wildcard resolution) creates more problems than it solves. If you can't trace the full chain, describe what you've found and let the maintainer assess the rest.

## Crash Bugs

Crash severity does not determine who should fix the bug. A crash with a clear null pointer dereference is often *easier* to fix safely than a subtle behavioral change.

### Debugger-Assisted Investigation

Since you can build HISE, you have a powerful advantage over AI tools: you can run the debugger.

1. **Reproduce the crash** in a Debug build
2. **Capture the call stack** and inspect local variables at the crash site — what's null? Uninitialized? Out of bounds?
3. **Share this with your AI tool** — paste the stack trace, the variable values, the surrounding code context
4. The AI can then trace *why* that value is wrong and propose a targeted fix
5. **Assess the fix**: If it's a missing null check or guard, proceed. If the root cause reveals deeper architectural issues (lifecycle ordering, cross-module state), report your findings in the issue and let the maintainer handle it.

This workflow is important because without runtime context, AI tools tend to optimize for "make the symptom stop" rather than "fix the root cause." They'll find the right file and the right function but propose a plausible-looking fix that addresses the symptom rather than the underlying problem. Feeding them debugger output closes this gap.

## The Risk Assessment Checklist

Include this in your PR description. Check each item that applies and add a brief rationale for any "yes" answer.

```markdown
### Risk Assessment

**Change type:**
- [ ] Bugfix (fixing obviously broken behavior)
- [ ] Pattern consistency (filling a gap that sibling code already covers)
- [ ] Feature addition (new behavior, opt-in)
- [ ] Feature addition (modifies existing behavior)

**Evidence:**
- [ ] I can link to existing code that already does what my fix does: [link/reference]
- [ ] This is a new pattern (no existing precedent in the codebase)

**Impact check:**
- [ ] Modifies DSP or audio rendering code
- [ ] Modifies module parameter indices or attribute enums
- [ ] Modifies serialization (exportAsValueTree / restoreFromValueTree)
- [ ] Modifies code with USE_BACKEND / USE_FRONTEND guards
- [ ] Modifies scripting engine (parser, preprocessor, include system)
- [ ] Adds per-instance objects to a module (chains, processors, buffers)
- [ ] Could change behavior of existing HISEScript projects
- [ ] Could change how existing projects sound or perform

**Testing:**
- [ ] Tested in Debug build
- [ ] Tested in Release build
- [ ] Tested in exported plugin (if applicable)
- [ ] Reproduced the original bug before applying fix

**Files changed:** [number] files, [additions/deletions] lines
```

If you check any box in the "Impact check" section, add a sentence explaining why you believe the change is still safe — or flag it as needing maintainer review.

## Submitting Imperfect Work

It's better to submit a PR with honest caveats than to not submit at all. If you:

- Realize your change might be breaking — **flag it in the PR title and description**. A well-documented breaking PR is more useful than silence.
- Find the right area but aren't sure about the fix — **submit what you have and explain your reasoning**. The PR becomes documentation even if it's never merged.
- Discover the problem is bigger than expected — **report your findings in the issue**. Debugger output, root cause analysis, and partial investigations save the maintainer significant time.
- Used AI tools and aren't confident in the result — **say so**. There's no shame in it, and it tells the reviewer where to look more carefully.

Closed PRs with good analysis are valuable contributions in their own right. They document attempted approaches, clarify root causes, and save future contributors from repeating the same investigation.

## Code Style

HISE uses C++17 with the JUCE framework. Key conventions:

- **Tabs** for indentation, **Allman brace style**, **120-character** max line width
- **PascalCase** for classes, **camelCase** for methods and members, **ALL_CAPS** for macros
- No member variable prefixes (`m_`, `_`)
- Use `jassert()` for debug assertions
- Add `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName)` to component classes
- Never block the audio thread — no locks, no allocations, no I/O

For full details, see `AGENTS.md` and the `guidelines/` directory, particularly:
- `guidelines/development/cpp-architecture.md` — Architecture and patterns
- `guidelines/development/threading-system.md` — Threading model
- `guidelines/development/build-configurations.md` — Backend/frontend/DLL builds
- `guidelines/style/cpp-ui.md` — UI conventions

## Using AI Tools

AI-assisted contributions are welcome and expected. If you're using an AI coding tool (Claude Code, Cursor, etc.), point it at `guidelines/contributor-agent.md` for HISE-specific instructions on assessing risk and navigating the codebase.

Tips for effective AI-assisted contributions:

- **Give the AI runtime context.** Debugger output, console errors, and specific reproduction steps make AI tools significantly more effective than just pointing them at an issue description.
- **Verify the AI's reasoning.** If the AI says "this is safe because X," check whether X is actually true. Search the codebase for the pattern it claims to be following.
- **Don't let the AI over-engineer.** Simpler data structures, smaller diffs, and fewer abstractions are almost always better. If the AI introduces a `SortedSet` where an `Array` would do, simplify.
- **Be honest about AI involvement.** Note it in the PR description. It helps the reviewer understand the likely failure modes.
