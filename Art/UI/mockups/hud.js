/* Shared mockup builders. Every function returns an HTML string; pages compose them inside .stage.
   Coordinates are the 1400x788 reference (Art/UI/STYLE.md). Data values come from Source/CoopRTS
   (costs, durations, force capacities 6/4/2) and Docs/World.md (names and copy). */
const HUD = (() => {
  const ICON = "../icons/commands/";
  const PORT = "../icons/portraits/";
  const g = (n, extra = "") => `<i class="g" style="--g:url(${ICON}${n}.png);${extra}"></i>`;

  // Force types. cap/cost/secs are ACommandBuilding::GetForceCapacity / GetUnitCost / GetUnitDuration.
  const ROLE = {
    Frontline: { cap: 6, cost: 20, secs: 3.3, hp: 140, human: "Luddite", machine: "SOL 6000", line: "Frontline · Melee · Tanky", c: "var(--r-frontline)", glyph: "role_frontline" },
    Ranged: { cap: 4, cost: 30, secs: 4.3, hp: 90, human: "Offline Ranger", machine: "Autocomplete Drone", line: "Ranged · Rifle · Fragile", c: "var(--r-ranged)", glyph: "role_ranged" },
    Siege: { cap: 2, cost: 50, secs: 6.7, hp: 110, human: "Unplugger", machine: "Hallucinator", line: "Siege · Heavy · Slow", c: "var(--r-siege)", glyph: "role_siege", fee: 180 },
  };
  const unitPic = (role, side = "Human") => `${PORT}units/SM_${side}_${role}.png`;
  const bldPic = (name) => `${PORT}buildings/${name}.png`;
  const barracksPic = (role, side = "Human") => bldPic(`SM_${side}_Barracks${role ? "_" + role : ""}`);
  const num = (n) => String(n);
  const hpColor = (f) => (f > 0.5 ? "var(--ok)" : f > 0.25 ? "var(--warn)" : "var(--bad)");

  /* ---------- top strip ---------- */
  function pip(state) {
    // neutral, captured (no outpost), established (outpost), contested
    const m = {
      neutral: ["#26303b", "#4a5a6b"], captured: ["#0b1118", "var(--amber)"],
      established: ["var(--player)", "#bfe0ff"], contested: ["var(--bad)", "#ffb3aa"],
    }[state];
    return `<i style="display:block;width:12px;height:12px;background:${m[0]};border:2px solid ${m[1]};transform:rotate(45deg);${state === "established" ? "box-shadow:0 0 8px var(--player)" : state === "contested" ? "box-shadow:0 0 8px var(--bad)" : ""}"></i>`;
  }

  function top(o) {
    const o2 = Object.assign({ power: 960, income: 10, sectors: ["neutral", "neutral", "neutral"], secured: 0, bunker: [900, 900], cluster: [900, 900],
      objective: "OBJECTIVE  ·  UNPLUG THE CLUSTER", army: "0 units", spec: "None", team: ["C1"], terminal: "" }, o);
    const chips = o2.team.map((t, i) => `<span class="pill" style="--pc:var(--team-${i + 1});padding:0 5px">${t}</span>`).join("");
    const hq = (label, v, c, fill) => `<div style="flex:1;min-width:0"><div class="bar" style="--c:${c};height:16px"><i style="width:${(v[0] / v[1]) * 100}%"></i><b><span style="margin-right:auto;padding-left:8px">${label}</span><span style="padding-right:8px">${v[0]} / ${v[1]}</span></b></div></div>`;
    return `
    <div class="strip" style="left:8px;width:428px"><div class="row" style="height:32px;gap:20px">
      <div class="row" style="gap:7px"><i style="width:11px;height:11px;background:var(--player);border:1px solid #cfe6ff;box-shadow:0 0 8px var(--player)"></i><div><div class="h3">COMMANDER</div><div class="h2" style="font-size:15px">C1 <span class="muted" style="font:400 11px var(--f-body);letter-spacing:0;text-transform:none">you</span></div></div></div>
      <div><div class="h3">POWER</div><div class="row" style="gap:4px"><span style="color:var(--gold);font-size:14px">${g("power")}</span><span class="big" style="color:var(--gold)">${o2.power}</span><span class="ok" style="font:500 11px var(--f-body)">+${o2.income}/s</span></div></div>
      <div><div class="h3">SECTORS</div><div class="row" style="gap:9px;height:18px;padding-left:2px">${o2.sectors.map(pip).join("")}<span class="muted" style="margin-left:2px;font:500 11px var(--f-body)">${o2.secured}/3</span></div></div>
    </div></div>
    <div class="strip" style="left:${700 - 200}px;width:400px"><div class="col" style="height:32px;justify-content:center;gap:2px">
      <div class="h3" style="text-align:center;color:${o2.terminal ? (o2.terminal.startsWith("VICTORY") ? "var(--ok)" : "var(--bad)") : "var(--text-3)"}">${o2.terminal || o2.objective}</div>
      <div class="row" style="gap:6px">${hq("HQ · THE BUNKER", o2.bunker, "var(--team-1)")}${hq("HQ · THE CLUSTER", o2.cluster, "var(--lens)")}</div>
    </div></div>
    <div class="strip" style="right:8px;width:428px"><div class="row" style="height:32px;gap:20px;justify-content:flex-end">
      <div><div class="h3">ARMY</div><div class="num">${o2.army}</div></div>
      <div><div class="h3">SPECIALIZATION</div><div class="num" style="color:${o2.spec === "None" ? "var(--text-3)" : "var(--text)"}">${o2.spec}</div></div>
      <div><div class="h3">TEAM</div><div class="row" style="gap:3px;height:15px">${chips}</div></div>
    </div></div>`;
  }

  /* ---------- minimap placeholder ---------- */
  function minimap(o = {}) {
    const s = 176;
    const dots = o.dots || [];
    return `
    <div class="frame" style="left:8px;bottom:8px;width:212px;height:212px;background:#060b10">
      <svg width="${s}" height="${s}" viewBox="0 0 ${s} ${s}" style="display:block">
        <defs><pattern id="mg" width="22" height="22" patternUnits="userSpaceOnUse"><path d="M22 0H0V22" fill="none" stroke="#1d2b39" stroke-width="1"/></pattern>
        <radialGradient id="mv" cx="50%" cy="50%" r="75%"><stop offset="0" stop-color="#14202c"/><stop offset="1" stop-color="#070c11"/></radialGradient></defs>
        <rect width="${s}" height="${s}" fill="url(#mv)"/><rect width="${s}" height="${s}" fill="url(#mg)"/>
        <path d="M0 132 Q60 118 96 96 T176 70" stroke="#2a3a4b" stroke-width="7" fill="none"/>
        <rect x="88" y="64" width="34" height="26" fill="#1a2733" stroke="#2b3b4c"/>
        <path d="M150 20 L120 78 M30 118 L120 78" stroke="#1f6a80" stroke-width="1.2" opacity=".8"/>
        ${(o.sectors || [["30","118","#5a6a7b"],["68","150","#5a6a7b"],["120","78","#5a6a7b"]]).map(p => `<circle cx="${p[0]}" cy="${p[1]}" r="9" fill="none" stroke="${p[2]}" stroke-width="2"/><circle cx="${p[0]}" cy="${p[1]}" r="3" fill="${p[2]}"/>`).join("")}
        ${dots.map(d => `<rect x="${d[0] - 2}" y="${d[1] - 2}" width="4" height="4" fill="${d[2]}"/>`).join("")}
        <rect x="12" y="132" width="9" height="9" fill="var(--team-1)" stroke="#dff0ff"/>
        <rect x="150" y="14" width="9" height="9" fill="var(--lens)" stroke="#ffd6d2"/>
        <rect x="26" y="98" width="72" height="42" fill="none" stroke="#f2f6fa" stroke-width="1.5" opacity=".9"/>
      </svg>
    </div>`;
  }

  /* ---------- construct panel ---------- */
  function construct(o = {}) {
    const stash = o.stash != null ? o.stash : 960;
    const cards = [
      ["BARRACKS", "DRILL SHED", "SM_Human_Barracks", 220, 12, "var(--k-barracks)"],
      ["OUTPOST", "TAP POINT", "SM_Human_Outpost", 160, 9, "var(--k-outpost)"],
      ["WORKSHOP", "THE GARAGE", "SM_Human_Workshop", 190, 14, "var(--k-workshop)"],
    ];
    const hov = o.hover != null ? o.hover : -1;
    const html = cards.map((c, i) => {
      const bad = stash < c[3];
      const state = bad ? "bad" : hov === i ? "hover" : "";
      return `<div class="btn ${state}" style="height:52px;--tc:${bad ? "var(--bad)" : c[5]}">
        <div class="abs" style="left:-3px;top:-3px;width:38px;height:38px"><img src="${bldPic(c[2])}" style="width:100%;height:100%;object-fit:contain;filter:drop-shadow(0 2px 2px #000)${bad ? ";opacity:.55" : ""}"></div>
        <div class="abs col" style="left:40px;top:-4px;right:-3px;text-align:left"><div class="row"><span style="font:700 13px/14px var(--f-head);letter-spacing:.06em;color:${bad ? "var(--text-3)" : "var(--text)"}">${c[0]}</span></div>
          <div style="font:700 8.5px/10px var(--f-head);letter-spacing:.1em;color:${c[5]};opacity:${bad ? .55 : .95}">${c[1]}</div></div>
        <div class="abs row" style="left:40px;right:-3px;bottom:-4px;gap:6px;font:500 11px/12px var(--f-body)"><span style="color:${bad ? "var(--bad)" : "var(--gold)"};font-size:10px">${g("power")}</span><span style="color:${bad ? "var(--bad)" : "var(--gold)"}">${c[3]}</span><span class="faint">${c[4]}s</span>
          ${bad ? `<span class="bad-c" style="margin-left:auto;font-size:9px">need ${c[3] - stash}</span>` : ""}</div>
      </div>`;
    }).join("");
    return `<div class="panel" style="left:226px;bottom:8px;width:232px;height:212px"><div class="col" style="gap:4px;height:180px">
      <div class="row" style="height:14px"><span class="h3" style="color:var(--text-2)">CONSTRUCT</span><span class="sp"></span><span class="faint" style="font:400 9.5px var(--f-body)">your stash</span></div>
      ${html}</div></div>`;
  }

  /* ---------- command card ---------- */
  // cells: {g, hk, lb, cost, state:[hover|pressed|disabled|active|warn|bad], ic:color, span}
  function card(cells, tip, machine) {
    const html = cells.map((c) => {
      if (!c) return `<div class="btn cmd slot-off ${machine ? "machine-btn" : ""}" style="opacity:.35"></div>`;
      const st = (c.state || "").split(" ").filter(Boolean).join(" ");
      return `<div class="btn cmd ${st} ${machine ? "machine-btn" : ""}" style="${c.span ? `grid-column:span ${c.span};width:auto;` : ""}${c.tc ? `--tc:${c.tc};` : ""}">
        ${c.hk ? `<span class="hk">${c.hk}</span>` : ""}
        ${c.cost != null ? `<span class="cost ${c.costBad ? "bad" : ""}">${c.cost}</span>` : ""}
        <span class="ico" style="${c.ic ? `--ic:${c.ic};` : ""}">${g(c.g)}</span>
        <span class="lb">${c.lb}</span>
      </div>`;
    }).join("");
    return `<div class="panel ${machine ? "machine" : ""}" style="right:8px;bottom:8px;width:284px;height:212px"><div class="card-grid" ${machine ? 'style="margin:-2px"' : ""}>${html}</div></div>${tip || ""}`;
  }

  function tooltip(o) {
    return `<div class="panel" style="right:8px;bottom:${8 + 212 + 6}px;width:284px;height:${o.h || 112}px;--trim:${o.c || "var(--player)"}"><div class="col" style="gap:5px">
      <div class="row" style="gap:6px"><span class="h2" style="font-size:14px">${o.title}</span>${o.key ? `<span class="key">${o.key}</span>` : ""}</div>
      <div class="hair"></div>
      <div class="muted" style="font:400 10.5px/13.5px var(--f-body)">${o.body}</div>
      ${o.foot ? `<div style="font:500 10.5px var(--f-body);color:${o.footc || "var(--gold)"}">${o.foot}</div>` : ""}</div></div>`;
  }

  /* ---------- info panels ---------- */
  const INFO_L = 464, INFO_W = 638;
  function info(inner, o = {}) {
    return `<div class="panel ${o.machine ? "machine" : ""}" style="left:${INFO_L}px;bottom:8px;width:${INFO_W}px;height:212px;${o.trim ? `--trim:${o.trim}` : ""}"><div class="col" style="height:180px;${o.machine ? "margin:-2px" : ""}">${inner}</div></div>`;
  }

  function header(title, sub, o = {}) {
    const hp = o.hp ? (() => {
      const f = o.hp[0] / o.hp[1];
      return `<div class="bar" style="width:190px;height:16px;--c:${o.hpc || hpColor(f)}"><i style="width:${f * 100}%"></i><b>HP  ${o.hp[0]} / ${o.hp[1]}</b></div>`;
    })() : "";
    return `<div class="row" style="height:30px;gap:10px;align-items:flex-start">
      <div class="col" style="gap:1px"><div class="row" style="gap:8px"><span class="h2" style="${o.machine ? "color:var(--pearl)" : ""}">${title}</span>${o.pill || ""}</div>
      <div class="row" style="gap:6px;font:400 10px var(--f-body);color:var(--text-2)">${sub}</div></div>
      <span class="sp"></span><div class="col" style="align-items:flex-end;gap:2px">${o.status || ""}${hp}</div></div><div class="hair" style="margin:2px 0 6px;${o.machine ? "background:linear-gradient(90deg,rgba(107,230,255,.5),rgba(107,230,255,.05))" : ""}"></div>`;
  }

  const ownerLine = (t) => `<i style="width:9px;height:9px;background:var(--player);display:inline-block"></i><span>C1 · ${t}</span>`;
  const roleChip = (role, extra = "") => `<span class="pill" style="--pc:${ROLE[role].c}">${g(ROLE[role].glyph, "font-size:11px")}${role.toUpperCase()}${extra}</span>`;

  // one roster slot. kind: joined | travel | build | buy | vacant
  function slot(i, w, s, role) {
    const R = ROLE[role];
    const pic = unitPic(role);
    const idx = `<span class="idx">${i + 1}</span>`;
    if (s.k === "joined") {
      const f = s.hp / R.hp;
      return `<div class="slot ${s.sel ? "sel" : ""}" style="width:${w}px;--tc:${R.c}">${idx}<div class="pic"><img src="${pic}"></div>
        <div class="hp"><i style="width:${f * 100}%;--hc:${hpColor(f)}"></i></div></div>`;
    }
    if (s.k === "travel") {
      return `<div class="slot travel" style="width:${w}px">${idx}<div class="pic"><img src="${pic}"></div><div class="chev">›››</div>
        <div class="st" style="color:var(--cyan);bottom:4px">EN ROUTE</div><div class="hp"><i style="width:${s.hp / R.hp * 100}%;--hc:var(--cyan)"></i></div></div>`;
    }
    if (s.k === "build") {
      return `<div class="slot build" style="width:${w}px;--tc:var(--amber)">${idx}<div class="pic"><img src="${pic}"></div>
        <div class="st" style="color:var(--amber);bottom:10px">BUILDING</div><div class="hp" style="background:#1a1408"><i style="width:${s.p * 100}%;--hc:var(--amber)"></i></div></div>`;
    }
    const bad = s.k === "vacant";
    return `<div class="slot vacant ${s.k === "buy" ? "buy" : ""} ${s.hover ? "hover" : ""}" style="width:${w}px;--tc:var(--gold)">${idx}<span class="dash"></span>
      <span class="plus" style="${bad ? "color:var(--text-3)" : ""}">${g("reinforce")}</span>
      <div class="price" style="${bad ? "color:var(--bad)" : ""}">${g("power", "font-size:10px;margin-right:2px")}${R.cost}</div>
      <div class="st" style="bottom:2px;color:${bad ? "var(--text-3)" : "var(--gold)"}">${bad ? "NEED " + s.need : "REINFORCE"}</div></div>`;
  }

  function barracks(o) {
    const R = ROLE[o.role];
    const slots = o.slots;
    const W = 482, gap = 6;
    const w = Math.min(118, Math.floor((W - (R.cap - 1) * gap) / R.cap));
    const joined = slots.filter((s) => s.k === "joined").length, trav = slots.filter((s) => s.k === "travel").length;
    const force = joined + trav;
    const status = o.statusText ? `<span class="pill" style="--pc:${o.statusColor || "var(--ok)"}">${o.statusText}</span>` : "";
    const head = header("BARRACKS · DRILL SHED", ownerLine("your building") + `<span class="faint">·</span><span>${o.frontText}</span>`,
      { hp: [500, 500], pill: `<span class="pill" style="--pc:var(--text-2)">${g("lock", "font-size:10px")}PERMANENT</span>` + roleChip(o.role), status });
    const prog = o.prog;
    const progress = `<div class="row" style="height:28px;gap:10px;margin-top:4px">
      <div class="col" style="flex:1;gap:3px"><div class="row" style="font:500 10.5px var(--f-body);gap:6px">${prog.text}<span class="sp"></span>${prog.right}</div>
      <div class="bar tick" style="--c:${prog.c}"><i style="width:${prog.p * 100}%"></i></div></div></div>`;
    return info(head + `<div class="row" style="gap:12px;align-items:flex-start">
      <div class="col" style="width:112px;gap:2px;align-items:center"><div class="pframe" style="width:112px;height:112px;--kc:var(--k-barracks)"><img src="${barracksPic(o.role)}"></div>
        <div class="row" style="gap:6px;align-items:baseline;margin-top:2px"><span class="h3">FORCE</span><span class="big" style="font-size:19px;color:${force === R.cap ? "var(--ok)" : "var(--text)"}">${force}<span class="faint" style="font-size:14px"> / ${R.cap}</span></span></div></div>
      <div class="col" style="flex:1;gap:3px"><div class="row" style="height:12px;gap:8px"><span class="h3">ROSTER  ·  ${R[o.side || "human"].toUpperCase()} ×${R.cap}</span><span class="sp"></span><span class="faint" style="font:400 9.5px var(--f-body)">${o.rosterNote}</span></div>
        <div class="row" style="gap:${gap}px;justify-content:flex-start;align-items:stretch">${slots.map((s, i) => slot(i, w, s, o.role)).join("")}${o.extra || ""}</div>${progress}</div></div>`);
  }

  function typeCard(role, o) {
    const R = ROLE[role];
    const sel = o.sel === role;
    const pips = Array.from({ length: R.cap }, () => `<i style="display:inline-block;width:11px;height:11px;margin-right:3px;background:${R.c};clip-path:polygon(0 0,70% 0,100% 30%,100% 100%,0 100%);opacity:${sel ? 1 : .55}"></i>`).join("");
    return `<div class="btn ${sel ? "active hover" : ""}" style="flex:1;height:142px;--tc:${R.c}">
      <div class="col" style="gap:5px;height:126px;margin:-3px -2px">
        <div class="row" style="gap:8px;align-items:flex-start"><div class="pframe" style="width:70px;height:70px;--kc:${R.c}"><img src="${barracksPic(role)}"></div>
          <div class="col" style="gap:2px"><span class="h2" style="color:${R.c}">${role.toUpperCase()}</span><span style="font:500 11px var(--f-body)">${R.human} ×${R.cap}</span><span class="muted" style="font:400 9.5px/12px var(--f-body)">${R.line.split(" · ").slice(1).join(" · ")}</span></div></div>
        <div class="hair"></div>
        <div>${pips}</div>
        <div class="row" style="font:500 10.5px var(--f-body);gap:6px"><span class="gold">${g("power", "font-size:10px")} ${R.cost}</span><span class="faint">per unit</span><span class="faint">·</span><span class="muted">${R.secs.toFixed(1)}s each</span></div>
        <div style="font:500 10px var(--f-body);color:${R.fee ? "var(--warn)" : "var(--text-3)"}">${R.fee ? `one-time setup ${R.fee}` : "no setup fee"}</div>
      </div></div>`;
  }

  /* ---------- Machine (enemy intel) ---------- */
  function intelPanel(o) {
    const lines = o.log.map((l, i) => `<div class="mono" style="font-size:10.5px;line-height:14px;color:${i === o.log.length - 1 ? "var(--pearl)" : "#6b8797"};opacity:${i === o.log.length - 1 ? 1 : .55 + i * .12}">${l}</div>`).join("");
    return `<div class="panel machine" style="left:${o.left || 1008}px;top:${o.top || 62}px;width:${o.w || 384}px;height:${o.h || 174}px"><div class="col" style="gap:6px;margin:-2px">
      <div class="row" style="gap:8px;height:18px"><i class="lens"></i><span class="h2" style="color:var(--pearl)">ENEMY INTEL</span><span class="sp"></span><span class="pill" style="--pc:var(--cyan)">SCOUTED</span></div>
      <div class="hair" style="background:linear-gradient(90deg,rgba(107,230,255,.5),rgba(107,230,255,.05))"></div>
      <div class="row" style="gap:8px"><span class="h3" style="color:#7fa7b7">PLAN</span><span class="pill" style="--pc:var(--pearl)">${o.plan}</span><span class="sp"></span><span class="mono" style="font-size:10px;color:#7fa7b7">Thought for ${o.secs}s</span></div>
      <div style="background:rgba(2,9,14,.7);border:1px solid #17394a;padding:6px 8px;min-height:70px">${lines}
        <div class="mono" style="font-size:11.5px;line-height:15px;color:var(--pearl);margin-top:2px"><span style="color:var(--cyan)">›</span> ${o.now}<span class="cursor"></span></div></div>
      <div class="row" style="gap:8px;height:12px"><span class="h3" style="color:#7fa7b7;width:62px">COMMIT</span><div class="bar tick" style="flex:1;--c:var(--cyan)"><i style="width:${o.commit}%"></i></div><span class="mono" style="font-size:10px;color:#7fa7b7">${o.commitText}</span></div>
    </div></div>`;
  }

  /* ---------- mode bar (placement / front) ---------- */
  function modeBar(o) {
    return `<div class="panel" style="left:${INFO_L}px;bottom:8px;width:${INFO_W}px;height:86px;--trim:${o.c}"><div class="row" style="height:54px;gap:12px">
      <div class="pframe" style="width:54px;height:54px;flex:none;--kc:${o.c}"><img src="${o.img}"></div>
      <div class="col" style="flex:1;gap:5px"><div class="row" style="gap:10px"><span class="h2" style="font-size:16px">${o.title}</span><span class="gold" style="font:500 12px var(--f-body)">${g("power", "font-size:11px")} ${o.cost}</span><span class="muted" style="font:400 11px var(--f-body)">${o.time}</span></div>
        <div class="row" style="gap:8px;font:500 11px var(--f-body);color:${o.okc}"><i style="width:9px;height:9px;background:${o.okc};box-shadow:0 0 6px ${o.okc}"></i>${o.reason}</div></div>
      <div class="col" style="gap:4px;align-items:flex-start"><span class="row" style="gap:6px"><span class="key">LMB</span><span class="muted">${o.lmb}</span></span><span class="row" style="gap:6px"><span class="key">RMB / Esc</span><span class="muted">Cancel</span></span></div></div></div>`;
  }

  function feedback(text, left = INFO_L, w = INFO_W, bottom = 8 + 212 + 4, color) {
    return `<div class="feedback" style="left:${left}px;width:${w}px;bottom:${bottom}px;${color ? `border-left-color:${color}` : ""}">${text}</div>`;
  }

  function mount(html, bg) {
    document.querySelector(".stage").style.setProperty("--bg", `url(${bg})`);
    document.querySelector(".stage").innerHTML = html;
  }

  return { g, ROLE, unitPic, bldPic, barracksPic, top, minimap, construct, card, tooltip, info, header, ownerLine, roleChip, slot, barracks, typeCard, intelPanel, modeBar, feedback, mount, pip, INFO_L, INFO_W };
})();
