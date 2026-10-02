# Agent guide: Post-Frontier (CoopRTS)

Where to find things:

| Need | Read |
|---|---|
| Target game design | [Docs/Design.md](Docs/Design.md), the index. Its topic table points to `Docs/Design/<topic>.md`; read only the file you need. |
| What is built today | `Source/CoopRTS/`. Source wins over docs. [Docs/Design/status.md](Docs/Design/status.md) maps current build to target, area by area. |
| Why a design decision was made | [Docs/Research/](Docs/Research/), with cited sources |
| Measured balance and the simulation harness | [Docs/Balance.md](Docs/Balance.md), `Build/SimulateMatches.py` |
| Map layouts | [Docs/Maps/](Docs/Maps/) |
| Setting, names, tone | [Docs/World.md](Docs/World.md). Its "Names in code and assets" table maps old display names (Bunker, Cluster, Availability Zone…) that code and map data still use to the current ones. |
| Build, package, Steam and network setup | [README.md](README.md). Its mechanics sections describe the current build (checked against source on 2026-10-02). |
| How to verify changes | [.agents/skills/verify-cooprts/SKILL.md](.agents/skills/verify-cooprts/SKILL.md), until `./x` replaces it (migration phase 1) |
| Engineering setup: the single entry point `./x`, test tiers, parallel work, landing, and the migration plan | [Docs/Engineering/Setup.md](Docs/Engineering/Setup.md). Measured current state: [Docs/Engineering/Audit/](Docs/Engineering/Audit/) |

When editing design docs:
- **Each fact lives in exactly one topic file.** Link to it rather than copying numbers.
- **Cross-reference with file links,** never section numbers.
- **Tag every mechanic** with its status: `[Built]`, `[Change]`, `[New]`, `[Candidate]` or `[Later]`.
