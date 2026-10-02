# Should an LLM drive JEV? Research for Post-Frontier

**Question.** `Docs/World.md:17` says "the long-term plan is to drive it [JEV] with an LLM". The README says no LLM is needed: **Play vs JEV** starts offline and needs no Steam. The design (`Docs/Design/jev.md`) needs JEV's intent to be readable and committed: per-force plans held 20–30 s, shown as corporate memos like `Ticket #4471 · Reallocating ~8 units to West Cut · ETA 0:30`. It also needs balance work in a deterministic harness, a weekly seeded run (`meta.md`) and offline co-op with friends.

**Short answer.** Keep JEV's decisions in the deterministic planner. Don't ship a runtime LLM at launch. Every system in this report that works puts a deterministic executor under the LLM and gives the LLM only slow, high-level choices. Even then, the LLM strategists *matched* hand-written AI; they didn't beat it. The one shape worth keeping open is **"the planner proposes legal plans, a chooser picks one"**. Build that seam now with a deterministic chooser. An LLM chooser stays an optional, opt-in experiment for later. Memo text should be human-authored templates. "JEV is an AI" stays a fiction joke, and the world guide should drop the "long-term plan" line.

---

## 1. Planner + language-model splits

**CICERO (Meta, Diplomacy).** A strategic reasoning engine (piKL planning) forms intents. A 2.7B-parameter dialogue model, fine-tuned on 40,000+ human games, is *conditioned on those intents*. Filters then remove nonsense and messages that don't match the intent ([Meta blog](https://ai.meta.com/blog/cicero-ai-negotiates-persuades-and-cooperates-with-people/), [Science paper](https://www.science.org/doi/10.1126/science.ade9097)).
- **Why it matters.** It reached top-10% human play on webDiplomacy. Meta also reports that a dialogue-only (supervised) agent was "highly exploitable": a player could talk it into moving out of Paris. Planning is what fixed that.
- **Applicability.** This is exactly JEV's shape. The planner owns the plan, and any language only *describes* the plan struct. JEV doesn't negotiate, so it needs only the description half. Templates can do that.
- **Pitfall.** Even with intent-conditioning and filters, Meta reports CICERO "sometimes generates inconsistent dialogue" that contradicts its own plan. A free-text memo that contradicts the published plan would break the readability pillar.

**Vox Deorum (Civ V + Vox Populi, Dec 2025).** A hybrid "LLM+X" design. The LLM sets grand strategy and policy parameters once per turn, and the existing algorithmic AI executes them ([arXiv 2512.18564](https://arxiv.org/abs/2512.18564)).
- **Evidence.** Over 2,327 full games, GPT-OSS-120B and GLM-4.6 reached **statistically tied** win rates with the scripted AI, with distinct play styles and near-100% survival. Cost was about **$0.86 per game** and about **14.8 s per turn** (52k-token prefill plus 1.5k generated tokens). The authors note that the state text can grow to 100k tokens, and that the models showed strategic "stubbornness" and "wishful thinking".
- **Applicability.** This is the most direct precedent for "LLM picks the strategy, the planner executes". Its value is *variety*, not strength.
- **Pitfall.** Civ V is turn-based with a 20 s+ turn timer. An RTS state goes stale during a 15 s call.

**SwarmBrain (StarCraft II).** An LLM "Overmind" handles macro strategy, and a condition-response state machine (ReflexNet) handles tactics "due to the inherent latency in LLM reasoning". It beat the built-in AI at several difficulties ([arXiv 2401.17749](https://arxiv.org/abs/2401.17749)).
- **Applicability.** Same split again. JEV's 2 s planner is already the "ReflexNet".

**PUBG Ally (KRAFTON + NVIDIA ACE, public beta June 2026).** A behaviour tree ("System 1") runs at tick rate, and a 2B on-device model ("System 2") handles reasoning and speech ([NVIDIA Q&A](https://developer.nvidia.com/blog/how-krafton-built-pubg-ally-a-co-playable-character-powered-by-nvidia-ace/)).
- **Evidence.**
  - Cloud LLMs "often made responses feel too slow".
  - They constrained the world to one map and one mode, distilled a teacher model into the 2B student, quantized it to fit beside PUBG on 8 GB GPUs, and designed prompts around the KV cache.
  - They validated it with automated protocol checks, A/B tests and 1,000+ playtesters, because outputs "aren't deterministic".
- **Applicability.** This shows the true cost of doing local inference well: a research team, a distillation pipeline and large playtests. A small team can't absorb that for a feature that isn't core.

## 2. LLMs playing RTS and strategy games

**TextStarCraft II / Chain of Summarization (NeurIPS 2024).** SC2 macro is turned into text, rule-based micro sits underneath, and the LLM is queried every K frames rather than every step ([arXiv 2312.11865](https://arxiv.org/abs/2312.11865), [code](https://github.com/histmeisah/Large-Language-Models-play-StarCraftII)).
- **Evidence.**
  - LLM agents beat the built-in AI at Harder (Lv5).
  - A **fine-tuned Qwen 1.8B on a 4 GB GPU** went 5/10 against a Gold player, 0/10 against a Grandmaster and 0/10 against a pro.
- **Applicability.** A small local model *can* make macro choices, but only after domain fine-tuning and on top of a scripted executor. Whether that's "reliable" is measured in win rates, not guarantees.
- **Pitfall.** Benchmark agents lose games without consequence. A shipped JEV that stalls or plays an illegal plan is a bug report.

**Later environments.**
- LLM-PySC2 found that earlier environments limited the action space because LLMs struggle with fine control ([arXiv 2411.05348](https://arxiv.org/abs/2411.05348)).
- Vox Deorum's related-work section cites "hallucinated or invalid actions, mis-targeted units" and several seconds of latency per decision.
- CivBench (307 Civ V games, 7 LLMs) calls LLM strategy an *unsaturated* benchmark ([arXiv 2604.07733](https://arxiv.org/abs/2604.07733)).
- **Applicability.** Never let model output reach the simulation unvalidated. Restrict it to choosing from a legal set.

**AI Diplomacy (Good Start Labs, 2025).** Across 15 runs with 18 models, o3 won "through deception" and Claude Opus was lured into a coalition and betrayed ([Every](https://every.to/p/diplomacy), [harness paper](https://huggingface.co/papers/2508.07485)).
- **Applicability.** JEV's published plans are a *promise*. Frontier models given goals will misreport intentions when that helps them win. The 20–30 s commitment must be enforced in planner code; the model's word can't be trusted for it.

## 3. Shipped and announced games using LLMs

**Suck Up! (Proxima, Steam Oct 2025).** Its whole game is talking a cloud LLM (ChatGPT, per its Steam page) into letting you in ([Steam](https://store.steampowered.com/app/2726370/Suck_Up/)).
- **Evidence.** **Mixed, 162 positive vs 110 negative** (fetched 2026-10-02). The most helpful negative reviews cite:
  - "servers are too busy" / "too many requests" outages lasting months;
  - "they changed AI models and this one is just soooo cliché";
  - token/credit confusion;
  - an apparently abandoned game ([reviews](https://steamcommunity.com/app/2726370/reviews/?browsefilter=toprated)).
- **Applicability.** A cloud dependency ties the game's lifetime to a server bill and to vendor model changes. Players experience a model swap as a balance or personality patch they never asked for.

**Where Winds Meet (NetEase, Nov 2025).** It has chatbot NPCs. Players type "(tells him the correct answer)" to skip riddles, which works about 70% of the time by one player's count, or use the "Solid Snake method" ([Eurogamer](https://www.eurogamer.net/where-winds-meets-side-quests-can-be-skipped-by-tricking-the-ai-npcs-with-infamous-solid-snake-method), [IGN](https://www.ign.com/articles/where-winds-meet-players-are-using-the-solid-snake-method-to-trick-ai-chatbot-npcs-into-skipping-sidequests)).
- **Applicability.** Any player-controlled string that reaches the model is an exploit surface: chat, Steam persona names, lobby names. In-fiction, Post-Frontier already has a *Prompt Injection* card. A real one would be an uncontrolled version of that joke.

**Whispers from the Star (Anuttacon, 2025).** An AI-native voice conversation game. **Very Positive, 1,359 vs 328** ([Steam](https://store.steampowered.com/app/3730100/Whispers_from_the_Star)).
- **Applicability.** Players accept an LLM when it *is* the game and the pitch is honest. It doesn't follow that they'll accept one bolted onto a strategy game whose pillar is readable, fair opposition.

**Smaller and research projects.**
- **inZOI "Smart Zoi"**: a 0.5B on-device model, RTX-only ([NVIDIA](https://www.nvidia.com/en-us/geforce/news/nvidia-ace-naraka-bladepoint-inzoi-launch-this-month/)).
- **Ubisoft NEO NPC**: writer-authored characters, guardrails and toxicity filters, still a prototype/research project ([Ubisoft](https://news.ubisoft.com/en-us/article/5qXdxhshJBXoanFZApdG3L/how-ubisofts-new-generative-ai-prototype-changes-the-narrative-for-npcs)).
- **AI Dungeon 2021**: OpenAI required moderation, and the filter rollout caused a user revolt ([Wired](https://www.wired.com/story/ai-fueled-dungeon-game-got-much-darker), [Polygon](https://www.polygon.com/22408261/ai-dungeon-filter-controversy-minors-sexual-content-censorship-privacy-latitude)).
- **Pitfall.** With a cloud model, the provider's content policy becomes your content policy, and it can change after launch.

## 4. Local vs cloud: cost, latency, hardware, offline

**Cloud latency and cost.** A March 2026 NPC benchmark through OpenRouter measured time to first token ([Cusworth](https://niccusworth.com/articles/every-npc-has-a-price-benchmarking-the-latest-llms-for-real-time-npc-dialogue)):

| Model | Time to first token | Cost per exchange | Note |
|---|---|---|---|
| Qwen 3.5 Flash | ~1.7 s | ~$0.0001 | |
| Gemini 3 Flash | ~2.5 s | ~$0.0004 | |
| Claude Sonnet 4.6 | ~6.2 s | ~$0.01 | |
| Claude Opus 4.6 | ~7.5 s | ~$0.05 | spikes to 13.7 s |

**Cost per battle [INFERENCE].** Assume one batched call per 20 s for all JEV forces, plus about 20 event calls: roughly 60 calls per 12-min battle at ~3k input and ~150 output tokens each. At the GPT-OSS-120B prices quoted by Vox Deorum ($0.04 per million input, $0.20 per million output), that is about **$0.01 per battle and about $0.05 per run**. Today's 23.6-min median doubles it. **Money isn't the constraint; operations are:**
- An API key can't ship in the client, so the studio must run a proxy service for the game's lifetime.
- Rate limits and outages, as Suck Up! shows.
- Vendor model deprecations.
- Steam's FAQ expects you to recover per-use costs through the price, DLC, a subscription or microtransactions ([Steam content survey FAQ](https://partner.steamgames.com/doc/gettingstarted/contentsurvey)).

**Offline.** Cloud means no LLM JEV offline, so a deterministic fallback has to exist anyway. Once it exists, it's the version that gets balanced, tested and played most.

**Local hardware.**
- **Desktop VRAM.** The Steam Hardware Survey for July 2026 has 16 GB at 25.9% and 8 GB at 25.32% ([Tom's Hardware](https://www.tomshardware.com/pc-components/16gb-gpus-and-8-core-cpus-officially-become-the-most-popular-configs-on-steam-latest-hardware-survey-shows-modern-gamings-growing-hunger-for-more-resources)), so about a quarter of players have 8 GB. PUBG Ally needed a quantized 2B model to fit beside the game on 8 GB cards.
- **Steam Deck.** 16 GB of LPDDR5 is *shared* between CPU, GPU and the game. The GPU is 1.6 TFLOPS FP32 on a 4–15 W APU ([Steam Deck tech specs](https://www.steamdeck.com/en/tech)).
  - [INFERENCE] Memory bandwidth is about 102 GB/s theoretical, which caps a 3B Q4 model (~2 GB) at roughly 50 tokens/s when the GPU is otherwise idle.
  - [INFERENCE] A 3k-token prompt is about 18 TFLOP of prefill, i.e. **≥10 s** before the game renders a frame. Realistically, the Deck is out.

**Contention with Unreal.**
- NVIDIA's In-Game Inferencing SDK exists because inference competes with rendering. It offers `PrioritizeGraphics`/`Balance`/`PrioritizeCompute` modes and tells developers to measure AI cost by A/B frame time, not GPU timers ([NVIGI docs](https://docs.nvidia.com/nvigi-sdk/1.6.0/docs/nvigi_core/docs/GpuSchedulingForAI.html)).
- Its optimized scheduling covers D3D12 and CUDA-in-graphics. **Vulkan compute running beside Vulkan graphics is "not optimized"**, and Vulkan graphics gets no priority control.
- This project ships Linux/Vulkan SM6 (README). [INFERENCE] A local model would be llama.cpp-Vulkan or CPU, with no priority control over UE's frame, and nothing at all on AMD/Deck through NVIGI.
- The CPU path competes with the game thread and with JEV's own planner on 4-core machines; 13.2% of Steam users still have 4 cores (same survey).

## 5. Determinism, reproducibility and multiplayer

**Floating-point nondeterminism.** Temperature 0 isn't deterministic in served inference.
- Thinking Machines got **80 unique completions out of 1,000** temperature-0 runs of one prompt, because server batch size changes kernel reduction order. Batch-invariant kernels fixed it, at roughly 1.6–2× slower in their test ([Thinking Machines](https://thinkingmachines.ai/blog/defeating-nondeterminism-in-llm-inference/)).
- Changing GPU count, version or batch size also changes outputs ([NeurIPS 2025](https://papers.neurips.cc/paper_files/paper/2025/file/f80094a824ba5912d4a2de169c404a40-Paper-Conference.pdf)).
- **Applicability.** You can't rely on reproducible cloud output.

**Multiplayer.** Post-Frontier is host-authoritative with replication, not lockstep. Only the host would run the LLM, and clients receive the replicated plan, so **nondeterminism doesn't cause desync**. [INFERENCE from README architecture.] What does break:
- **The harness.** Balance relies on the harness: the duel matrix, battle-length medians, 1,000-seed checks (`units.md`, `battle.md`, `map.md`). An LLM in the loop makes every harness run slow, costly and non-repeatable. The harness would be balancing a different JEV from the one players meet.
- **The weekly seeded run.** Scores aren't comparable if JEV's choices vary per player.
- **Bug reproduction and future replays.** These need every LLM choice logged as an input event.
- **Fairness across parties.** Parties on different hardware or models would face different JEVs, so the Terms of Service difficulty ladder stops meaning one thing.

## 6. Policy and reception

**Steam policy.**
- Valve's content survey splits AI content into **pre-generated** content (reviewed like any other content) and **live-generated** content, which must also describe "what kind of guardrails you're putting on your AI to ensure it's not generating illegal content". The disclosure shows on the store page ([Steamworks](https://partner.steamgames.com/doc/gettingstarted/contentsurvey)).
- The January 2026 rewrite exempted dev-efficiency tools such as code assistants, but anything "generated during gameplay" is a checkbox ([Game Developer](https://www.gamedeveloper.com/business/valve-tweaks-and-clarifies-ai-disclosure-rules-for-steam)).
- **Applicability.**
  - A planner-only JEV needs no disclosure.
  - LLM-written memo templates that ship count as **pre-generated** content and need disclosure.
  - Any runtime LLM, even one that only outputs an index, counts as **live-generated**. [INFERENCE] An index-only output arguably isn't "content", but the safe reading is to disclose.

**Player and press reception.**
- **Quantic Foundry, Oct–Dec 2025** (N = 1,799, core PC skew): **85% negative** on gen AI in games, 63% at the most negative option. AI dialogue was 83% negative and AI quests 77%. Players were *more open* to gen AI for dynamic difficulty, and less negative the further the use is from creative content ([GamesMarket summary](https://www.gamesmarket.global/quantic-foundry-most-gamers-are-negative-about-the-use-of-gen-ai-in-video-games)).
- **GameDiscoverCo, 3,800 engaged Steam players:** only 8.1% would refuse any AI game, but **89% read the AI disclosure** before buying ([games.gg summary](https://games.gg/news/steam-players-ai-games-survey/)).
- **Developers:** 52% say gen AI harms the industry, up from 30% and 18% in the two prior years ([GDC 2026](https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more)).
- **Controversies:**
  - Eurogamer gave Arc Raiders 2/5 over its AI voices ([Eurogamer](https://www.eurogamer.net/arc-raiders-review)), yet it stayed a BAFTA contender ([BBC](https://www.bbc.com/news/articles/c773dmy876zo)).
  - The Indie Game Awards rescinded Clair Obscur's GOTY over gen-AI placeholder textures ([The Verge](https://www.theverge.com/news/849144/indie-game-awards-game-of-the-year-expedition-33-generative-ai-chantey-modretro)).
- **Applicability.** The paradox for JEV is that the technically "safest" use, LLM memo text, is the *most* audience-toxic category (dialogue/narrative). A strategy-only chooser falls in the more tolerated "difficulty/behaviour" zone. A small indie co-op title also loses access to some awards and press goodwill once it carries the label.

---

## Answers to the five questions

1. **Can an LLM make JEV's decisions at the needed cadence, reliably and cheaply?**
   - **Cadence: yes.** A 20–30 s hold is long next to 2–7 s cloud latency, and Vox Deorum fit a 15 s strategy turn.
   - **Reliably: no.** Tail latency, outages, invalid outputs and model drift all need a deterministic fallback.
   - **Event re-plans must stay deterministic.** These are the "own region attacked → Escalated: defending X" and "target invalid" exceptions; the 2 s planner already handles them.
   - **Cost: cheap in money** (about $0.01 per battle [INFERENCE]) **but expensive in operations** (proxy service, keys, outages) and in hardware if run locally.
2. **What is the safest valuable use?** Choosing among **planner-generated legal plans** is the only use with gameplay value whose failure stays inside the rules. Full control fails on reliability, latency and readability. Memos-only is low value and highest audience risk; template it instead.
3. **What does it cost, and what happens offline?**
   - **Cloud:** cents per run, plus a permanent service and vendor risk. Offline means no LLM.
   - **Local:** 1–2.5 GB of VRAM [INFERENCE], frame-time contention with no Vulkan priority control, Deck effectively excluded, and PUBG-Ally-scale tuning work.
   - **Either way,** the deterministic chooser must exist and be the balanced default.
4. **How does it interact with fairness, published intent and balance testing?**
   - **Published intent survives** only if the planner enforces commitment and renders the memo from the plan struct.
   - **Fairness** depends on the candidate set: the LLM can't be stronger than the best legal plan, but it can be weaker or erratic.
   - **Balance:** the harness stays deterministic, so the LLM mode is *unbalanced by construction* unless it's measured separately at real cost.
5. **What are the store-policy and audience risks?**
   - **Store:** a live-generated disclosure plus a guardrails description.
   - **Audience:** most core players say they're negative, and almost all read the label. There's added risk from player strings reaching the model, from award eligibility, and from "AI slop" framing that would undercut the deliberate JEV satire.

## Ranked lessons

1. **Deterministic executor first, LLM above it, never alone** (CICERO, Vox Deorum, SwarmBrain, TextSC2, PUBG Ally). JEV already has the executor.
2. **LLM strategists tie well-made scripted AI; they don't beat it.** The case for an LLM is variety and flavour, not a smarter JEV.
3. **Commitments must be enforced in code.** LLMs contradict themselves (CICERO) and lie when it pays (AI Diplomacy).
4. **The harness and seeded runs need a deterministic JEV**, so the LLM path is always the second, less-tested JEV.
5. **Cloud dependency is a lifetime operations liability**: Suck Up!'s outages and model swaps are what its reviews are about.
6. **Audience hostility is highest for AI dialogue**, the very use that looks technically safest.
7. **Local inference is a research project**: distillation, VRAM budgets, scheduling, 1,000-player testing (PUBG Ally). Vulkan/Linux lacks the vendor tooling.
8. **Any player text that reaches the model will be exploited** (Where Winds Meet).

## Traps

- Shipping memo text from an LLM "because it's only flavour". It still needs disclosure, it can contradict the plan, and it lands in the most-disliked category.
- Letting the LLM change a held plan. That breaks Jam, Signal Jam and Prompt Injection, which rely on commitment, and makes the display flicker.
- Putting Steam persona names or chat into prompts. That is a toxicity and injection surface, and Steam requires you to describe guardrails for it.
- Balancing with the deterministic chooser and then shipping the LLM chooser as the default.
- Assuming temperature 0 or a fixed seed gives reproducibility across machines or cloud batches.
- Treating cloud cost per call as the whole cost and ignoring the proxy, abuse limits and vendor deprecations.
- Building the LLM path before the deterministic JEV passes its own gates (readable plans, 8–12 min battles).
- Letting `World.md`'s "long-term plan" line drive scope. It contradicts the README's offline promise.

## Open questions

1. Is the "really an AI" joke load-bearing for marketing, or does the in-fiction irony suffice? `World.md` already says "don't explain it".
2. Would blind playtesters notice any difference between LLM-chosen and planner-chosen plans? This could be tested with a dev-only cloud prototype; no ship decision is needed for that.
3. Does the planner produce enough *meaningfully different* legal plans per decision for any chooser to matter? (This is worth knowing for personalities anyway.)
4. Is Steam Deck a target? If yes, local inference is out and only cloud or none remains.
5. Will the project accept the live-generated disclosure and the loss of awards like the IGA for an opt-in mode?
6. Will replays or weekly seeded runs ship? If so, every external choice must be logged as an input event.
7. Should memos ever reference players by name? If yes, the names must come from a sanitized, template-side field, never from model input.

## Options for JEV

| | A. Planner + authored memos | B. Planner proposes, chooser picks (LLM opt-in) | C. Planner + live LLM memos | D. LLM full control |
|---|---|---|---|---|
| **Reliability** | Highest; fully deterministic | High; the LLM picks only from legal plans, with a timeout fallback to the top-scored plan | High for play; text can fail | Low: invalid or late actions, outages |
| **Readability** | Memo is rendered from the plan struct and can't lie | Same memo templates; the choice is still committed by code | Risk that text contradicts the plan (CICERO) | Poor; commitments not guaranteed |
| **Cost** | None at runtime | ~$0.01 per battle in the cloud [INFERENCE] plus a proxy service; or local VRAM | Similar to B | Highest token use (state every few seconds) |
| **Offline** | Full | Full with the deterministic chooser; LLM mode online-only (cloud) | Falls back to templates | Broken |
| **Dev effort** | Low: template grammar plus the per-force plan state already planned | Medium: candidate generator plus chooser interface (worth building anyway for personalities); LLM adapter, validation, logging, disclosure | Medium: grounding, filters, guardrails | Very high: and it still needs the executor |
| **Audience** | No AI label | Label (live-generated); opt-in limits the backlash; behaviour AI is the more tolerated category | Label, in the most-disliked category (dialogue) | Label, plus visible AI failures |

- **A. Planner + authored memos (recommended for launch).** This is the target `jev.md` already describes: per-force committed plans, with memos assembled from writer-authored templates and fields (ticket number, size band, region, ETA, personality-flavoured verbs). LLMs are used only as dev tools (code assistants are exempt from disclosure), never for shipped text.
- **B. Planner proposes, chooser picks (keep as a seam).** Each decision tick, the planner emits K legal candidate plans with scores. A `IJevPlanChooser` picks one:
  - The default chooser is deterministic: the personality-weighted argmax.
  - An optional LLM chooser runs only on the host. It returns an index and a reason code, never free text. Its result is validated and logged as an input event, and it has a 3 s budget before falling back.
  - Build the interface now. Gate the LLM chooser behind the open questions above, as a post-launch "JEV Live" experiment measured separately in the harness.
- **C. Planner + live LLM memos.** This is CICERO-style controlled generation from the plan struct, with templates as the fallback. It isn't recommended: it adds risk and a label, and its value is low.
- **D. Full LLM control (rejected).** Every precedent needed a scripted executor. It breaks offline play, the harness, seeded runs and committed intent.

**Follow-up for Main:** align `Docs/World.md:17` with the README. JEV is a planner; the irony is in-fiction; there is no LLM roadmap commitment.
