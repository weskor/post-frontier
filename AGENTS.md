# Agent guide: Post-Frontier (CoopRTS)

Where to find things:

| Need | Read |
|---|---|
| Target game design | [Docs/Design.md](Docs/Design.md), the index. Its topic table points to `Docs/Design/<topic>.md`; read only the file you need. |
| What is built today | `Source/CoopRTS/`. Source wins over docs. [Docs/Design/status.md](Docs/Design/status.md) maps current build to target, area by area. |
| Why a design decision was made | [Docs/Research/](Docs/Research/), with cited sources |
| Measured balance | [Docs/Balance.md](Docs/Balance.md); simulation procedures: `./x help sim` |
| Map layouts | [Docs/Maps/](Docs/Maps/) |
| Setting, names, tone | [Docs/World.md](Docs/World.md). Its "Names in code and assets" table maps old display names (Bunker, Cluster, Availability Zone…) that code and map data still use to the current ones. |
| Current gameplay mechanics and project overview | [README.md](README.md). Its mechanics were checked against source on 2026-10-02. |
| Setup and engine shader patch | `./x help setup` |
| Build, editor, package and launch procedures | `./x help build`, `./x help editor`, `./x help package`, `./x help play` |
| Steam and developer network procedures | `./x help play`, `./x help verify` |
| Content, map and asset generation procedures | `./x help gen` |
| Simulation procedures | `./x help sim` |
| Test scopes, runtime verification and change proof | `./x help test`, `./x help verify`, `./x help check` |
| Run records and landing procedures | `./x help runs`, `./x help land` |
| All procedure reference | `./x help` |
| Splitting work across parallel agents (orchestrator, workers, reviewer in herdr panes) | [.agents/skills/orchestrate/SKILL.md](.agents/skills/orchestrate/SKILL.md); execution and landing procedures: `./x help` |
| Engineering principles, test tiers, parallel work and the migration plan | [Docs/Engineering/Setup.md](Docs/Engineering/Setup.md). Measured current state: [Docs/Engineering/Audit/](Docs/Engineering/Audit/) |

The only proof of a change is `./x check`; landing is only through `./x land`. Procedures live only in `./x help` and its command-specific help, not in copied commands or recipes in these docs.

When editing design docs:
- **Each fact lives in exactly one topic file.** Link to it rather than copying numbers.
- **Cross-reference with file links,** never section numbers.
- **Tag every mechanic** with its status: `[Built]`, `[Change]`, `[New]`, `[Candidate]` or `[Later]`.
