# M_Shared: Unreal port plan for the SC2-style master material

Prototype generation: `./x gen master-materials` ([`./x help gen`](../../x)), using Blender 5.2.1. The editable output is `Art/Materials/MaterialPreview.blend`; previews are `Before-After-{Units,Buildings,Kit}.png`, `Close-Human.png`, `Close-Machine.png`, `FieldView.png` and `FieldView-Flat.png`.

[Built] Unreal port (2026-09-30): `./x gen build-shared-material` creates `M_Shared`, `MF_Triplanar_Local`, `MF_SC2_Wear` and 32 `MI_SC2_*` instances. Mesh imports use `./x gen import-unit-meshes`, `./x gen import-building-meshes` and `./x gen import-environment-kit`; mask read-back uses `./x gen verify-masks`. Procedures: [`./x help gen`](../../x); dependency/output context: [World.md](../../Docs/World.md). The vertex colour property is `vertex_color_import_option` (`VertexColorImportOption.REPLACE`); values arrive linear.

[Candidate] The contracts below preserve the original material proposal. Actual parameter deviations live in `Build/BuildSharedMaterial.py`; no untracked verification report is required to find them.

## 1. What the material does

One master, four layers, all readable at the 50 degree RTS camera:

| Layer | Look | Driven by |
| --- | --- | --- |
| Paint | Flat base colour; `Team` slot uses `TeamColor` | `BaseColor` / `TeamColor`, `UseTeam` |
| Edge | Crisp bevel highlight (paint catches light) and edge wear (paint chipped to bare metal, broken up by a wear texture) | vertex mask `Edge`, `EdgeHighlight`, `EdgeWear`, `BareMetalColor`, wear texture |
| Grime | Soft darkening in crevices, under plates, near the ground and in panel seams | vertex masks `Cavity`, `Ground`, `GrimeAmount`, `GroundGrime`, grime texture |
| Panels | Seam lines darkened plus a shallow height bump from a metal-panel texture | panel texture, `PanelLine`, `PanelStrength` |
| Extras | Pearl grazing tint and clear coat (Machine), emissive `Glow`, self-lit `Team`, procedural hazard stripes | `PearlTint/Amount`, `ClearCoat`, `EmissiveColor/Intensity`, `SelfLit`, `HazardStripes` |

The meshes have no UVs, so every texture is sampled triplanar in object space (section 4) and the three surface
masks are per-vertex data (section 3).

## 2. Master material `M_Shared`

One material, shading model Default Lit, opaque, used by every slot of every unit, building and kit mesh.
Slots differ only by instance parameters.

### 2.1 Parameters

Names are identical to the Blender group inputs of `SC2_Master_baked`, so the tables below transfer 1:1.

| Parameter | Type | Meaning |
| --- | --- | --- |
| `TeamColor` | Vector | **Same name gameplay already sets.** Base colour of the `Team` slot and its emissive tint |
| `UseTeam` | Static Switch | On: colour = `TeamColor`, `SelfLit` emissive. Off: colour = `BaseColor` |
| `BaseColor` | Vector | Paint colour (non-Team slots) |
| `Metallic`, `Roughness` | Scalar | Paint surface |
| `ClearCoat` | Scalar | 0..1 gloss layer (Machine shell, Team paint) |
| `PearlTint`, `PearlAmount` | Vector, Scalar | Grazing-angle tint; `Lerp(colour, PearlTint, Fresnel * PearlAmount)` |
| `BareMetalColor` | Vector | Colour under chipped paint (metal 1, roughness 0.32) |
| `EdgeWear`, `EdgeHighlight` | Scalar | Chip amount and highlight amount on convex edges |
| `GrimeColor`, `GrimeAmount`, `GroundGrime` | Vector, Scalar, Scalar | Multiply-grime colour; crevice amount; ground-splash amount |
| `PanelLine`, `PanelStrength` | Scalar | Seam darkening; bump strength |
| `HazardStripes`, `HazardDark` | Static Switch, Vector | Diagonal stripes: `Frac((x+y+z)*11) > 0.5` in object space, stripe colour `HazardDark` |
| `EmissiveColor`, `EmissiveIntensity` | Vector, Scalar | `Glow` / `Accent` light |
| `SelfLit` | Scalar | `TeamColor * SelfLit` emissive so player colour survives shadow |
| `TileSize` (Vector3: panel, wear, grime, metres) | Vector | Triplanar tile size per texture |
| `TexPanel`, `TexWear`, `TexGrime` | Texture Object | Channel packed or single-channel masks (section 5) |
| `UseBakedMasks` | Static Switch | Off: `Edge = Cavity = Ground = 0` (see the vertex-colour trap in 3.2) |

Static switches (`UseTeam`, `HazardStripes`, `UseBakedMasks`) can only be set on a `MaterialInstanceConstant`, never on the
dynamic instance gameplay creates. Every slot therefore gets its own MIC and gameplay's dynamic instance only overrides
`TeamColor`.

### 2.2 Graph (Unreal-equivalent of the Blender node groups)

Blender group / helper → Unreal:

* **`SC2_Masks_baked`** → `VertexColor` node: `Edge = R`, `Cavity = G`, `Ground = B`, gated by `UseBakedMasks`.
  (`SC2_Masks_live`, the Cycles-only Pointiness + AO variant, has no Unreal equivalent and exists only for comparison.)
* **Triplanar sample** (`_sample()` helper in the Python) → Material Function `MF_Triplanar_Local` (section 4).
* **`SC2_Wear`** → Material Function `MF_SC2_Wear`, inputs `Color, Metallic, Roughness, Edge, Cavity, Ground, WearMask,
  GrimeMask, PanelHeight` plus the wear/grime/panel parameters, outputs `Color, Metallic, Roughness`:

  ```
  chipSignal = Edge * (0.55 + 0.9 * WearMask)
  t          = 0.9 - 0.6 * EdgeWear
  chip       = SmoothStep(t, t + 0.12, chipSignal) * Saturate(EdgeWear * 10)
  highlight  = SmoothStep(0.05, 0.6, Edge) * EdgeHighlight
  seam       = 1 - SmoothStep(0.05, 0.45, PanelHeight)
  grime      = Saturate( Cavity * GrimeAmount * (0.35 + 1.1 * GrimeMask)
                       + Ground * GroundGrime * GrimeMask * 1.3
                       + seam * GrimeAmount * 0.6 )
  lifted     = Lerp(Color, 1, 0.55)
  Color      = Lerp(Color, Screen(Color, lifted), highlight * 0.95)     // Screen(a,b) = 1-(1-a)(1-b)
  Color      = Lerp(Color, BareMetalColor, chip)
  Color      = Lerp(Color, Color * 0.15, seam * PanelLine)
  Color      = Lerp(Color, Color * GrimeColor, grime)
  Metallic   = Lerp(Metallic, 1, chip) * (1 - 0.7 * grime)
  Roughness  = Lerp( Lerp(Roughness, 0.32, chip) - highlight * 0.15, 0.9, grime )
  ```
* **`SC2_Master`** (main graph):
  1. `Colour = Lerp(BaseColor, TeamColor, UseTeam)`, then `Lerp(Colour, HazardDark, Step(0.5, Frac((X+Y+Z)*11)) * HazardStripes)` with
     `X,Y,Z` = object-space position (`TransformPosition` World→Local of `AbsoluteWorldPosition`).
  2. `MF_SC2_Wear` as above.
  3. Pearl: `Lerp(Colour, PearlTint, Fresnel(Exponent 3.0) * PearlAmount)`.
  4. Normal: `NormalFromHeightmap`-style bump of `PanelHeight * PanelStrength`, or **[unverified]** a triplanar sample of the
     panel *Normal* textures with the whiteout blend (needs the same object-space normal as section 4; the Blender
     prototype uses the height map because Blender has no tangent-free triplanar normal).
  5. Roughness: `Roughness` from `MF_SC2_Wear`. Clear coat, option A (recommended, cheaper): Default Lit with
     `Roughness = Lerp(Roughness, 0.08, ClearCoat * Fresnel(Exponent 5.0))`. Option B: Clear Coat shading model with
     `ClearCoat = ClearCoat`, `ClearCoatRoughness = 0.08`; costs an extra lobe on every slot, so gate it behind a
     static switch only if A does not read as gloss in the gallery.
  6. Emissive: `Lerp(EmissiveColor, TeamColor, UseTeam) * (EmissiveIntensity + UseTeam * SelfLit)`.
  Reference for the Blender-side maths: the docstring of `Build/MasterMaterials.py`.

## 3. Surface masks: vertex colours

### 3.1 What is baked

`MasterMaterials.bake_masks(obj, scope)` writes a float colour attribute `SC2Mask` (POINT domain):

| Channel | Meaning | How |
| --- | --- | --- |
| R `Edge` | Convex bevel strips | Vertex whose adjacent faces are all narrow strips (inradius below 1.6 / 3 / 5 cm for unit / building / kit) and whose face normals fan out over more than 20 degrees and whose edges are convex. Flat plates, rivets and coarse cylinders stay 0 |
| G `Cavity` | Occlusion and creases | 20 cosine-weighted rays per vertex (0.35 / 0.6 / 1.0 m) plus concave crease angle |
| B `Ground` | Splash near the ground | `(1 - (z - zmin) / height)^2` with height 0.45 / 0.8 / 1.2 m |

The historical Blender bake measured well under a second per mesh and about 5 s for all 40 meshes plus the preview blend. Prototype generation options are described only in [`./x help gen`](../../x).

### 3.2 Getting the masks into Unreal

* The generators currently export FBXs without the attribute (they are unmodified on purpose). To ship masks, each
  generator's export step needs `MasterMaterials.bake_masks(obj, scope)` before `export_fbx`, and the FBX export needs
  `colors_type='LINEAR'` so mask data is not sRGB-encoded **[unverified: check the exported values against `Edge` in the
  Unreal debug view]**. This is a small edit to three scripts and was not done here.
* Import with `Vertex Color Import Option = Replace` in `ImportUnitMeshes.py` / `ImportEnvironmentKit.py`
  (`FbxStaticMeshImportData.vertex_color_import_option`) **[unverified: property name from memory]**.
* **Trap:** a mesh without vertex colours reads as white `(1,1,1)` in the `VertexColor` node, which would mean
  "every edge worn, everything in a crevice". Keep `UseBakedMasks` a static switch, off by default on the MICs, on only for
  meshes that were re-imported with masks.
* **Limit (true of the Blender prototype too):** the highlight is the bevel strip itself, about 1 cm wide. Vertices are
  sparse across big plates, so a wider wear zone on flat faces would need denser geometry or baked textures with UVs
  (the meshes have none). At the RTS camera the strips read as thin bright lines, which is what the previews show.
* Alternative if bevel strips prove too thin in game: a screen-space edge from the pixel normal derivative
  (`DDX/DDY` of `PixelNormalWS` in a Custom node). Not prototyped, so the strength values above would need retuning.

## 4. Triplanar setup

Meshes have no UVs, so all texture sampling is object-space triplanar, matching Blender's Image Texture `Box` projection
(blend 0.25) driven by `Texture Coordinate > Object`.

`MF_Triplanar_Local(Texture Object, TileSize, Sharpness = 4)`:

```
P = TransformPosition(World → Local, AbsoluteWorldPosition) / (TileSize * 100)     // cm → tiles
N = normalize(TransformVector(World → Local, PixelNormalWS))
w = pow(abs(N), Sharpness); w = w / (w.x + w.y + w.z)
Out = Sample(tex, P.yz).r * w.x + Sample(tex, P.xz).r * w.y + Sample(tex, P.xy).r * w.z
```

Local space is required so textures ride with the unit, are not re-randomised by yaw, and are identical for every copy of a
mesh. Three samples per texture; three textures = nine samples per pixel. Cheaper options:

* pack the three masks into one RGB texture `T_SC2_Detail` (R panel height, G wear, B grime), one triplanar call, three
  samples, but a single `TileSize` (a small script over the CC0 sources below; not written yet);
* drop wear and grime on `Glow` / `Accent` (their instance sets `EdgeWear = GrimeAmount = 0`; add a static switch
  `UseDetail` to skip the samples entirely).

## 5. Texture list (all from `Art/Textures`, CC0 ambientCG)

| Role | Asset / map | Used by | Import |
| --- | --- | --- | --- |
| Panel height, riveted plates | `Panel/MetalPlates002` Height | Human Shell, Team, Accent, Glow | Linear, Mask compression |
| Panel height, studded armour | `Panel/MetalPlates006` Height | Human Dark | Linear |
| Panel height, hairline tile grid | `SciFiFloor/Tiles108` Height | all Machine slots | Linear |
| Wear (paint scuffed through) | `PaintedMetal/PaintedMetal014` Roughness | Human and Machine | Linear |
| Grime, cloudy stains | `Metal/Metal046A` Height | Human Shell, Team, Accent | Linear |
| Grime, brushed streaks | `Metal/Metal010` Roughness | Human Dark | Linear |
| Grime, soft haze | `Concrete/Concrete016` Height | Machine slots | Linear |
| Floor (preview only) | `Asphalt/Asphalt026A` BaseColor + Roughness | preview floor, optional for `MI_Asphalt` | BaseColor sRGB, tinted to the `MI_Asphalt` mean albedo (0.045) |

Import notes from `Art/Textures/SOURCES.md` apply: normals are DirectX (Unreal-native), BaseColor sRGB, every other map linear.
Not used: `Metal032`, `Rubber004`, `DiamondPlate001`, `MetalWalkway006`, `Concrete024`, `Gravel004/006`,
`Hazard/PaintedMetal016`. Hazard stripes are procedural because the library has no clean stripe texture (see SOURCES.md), and
the stripes are small and black/yellow as `Docs/World.md` requires. The 2K JPEGs are 2048 x 2048; 1K is enough for the RTS
camera (set `Maximum Texture Size 1024` on the three mask textures to cut memory to about a quarter).

## 6. `TeamColor` and gameplay code

The current code (unchanged by this port) does, per actor:

* `AArmyUnit::OnRep_Appearance` and `AHeadquarters` (appearance update): `Body->GetMaterialIndex("Team")`, then
  `Cast<UMaterialInstanceDynamic>(Body->GetMaterial(slot))` or `CreateAndSetMaterialInstanceDynamic(slot)`, then
  `SetVectorParameterValue("TeamColor", ...)`.
* `CommandBuilding`: the same on slot 0 (the mesh is still the engine cube; when the building meshes are wired, slot 0
  is `Team` by contract).

That keeps working as is, because:

1. `Team` is slot 0 and named `Team` on every generated mesh.
2. The slot's material becomes `MI_SC2_<Faction>_Team_<Scope>`, a MIC of `M_Shared`. `CreateAndSetMaterialInstanceDynamic` makes a dynamic child of
   whatever is in the slot, so the dynamic instance inherits every other parameter and static switch.
3. `M_Shared` exposes a vector parameter literally named `TeamColor`.
4. Dead colours `(0.08)` and `(0.12)` and the construction amber `(0.8, 0.6, 0.18)` stay valid: they drive `BaseColor` and `SelfLit`, so a dead unit goes dark and stops glowing.
5. The Machine's default `TeamColor` is red `(1, 0.08, 0.08)` and the Human default is blue `(0.04, 0.50, 1.0)`; these are the MIC defaults, exactly `DEFAULT_TEAM` in `ImportUnitMeshes.py`.

Keep `/Game/Materials/M_CommandUnit` and its `MI_CommandTeam0..4` untouched: the cube fallbacks and `ACapturePoint` (which also
sets `TeamColor` on its marker) still use them. `M_Shared` does not replace it.

The only gameplay-visible change is the brightness of team colours from `ArmyIndex == 1` (lerp to white 0.45): the highlight
and `SelfLit` add a little, so watch pastel team colours in the gallery for washout.

## 7. Replacing the current instances

| Today | Replaced by | Notes |
| --- | --- | --- |
| `MI_UnitTeam_Human` / `_Machine` (parent `M_CommandUnit`) | `MI_SC2_Human_Team_Unit` / `MI_SC2_Machine_Team_Unit` (parent `M_Shared`) | `TEAM_PARENT` in `ImportUnitMeshes.py` becomes `/Game/Art/Materials/M_Shared`, `UseTeam` on |
| `MI_UnitHumanShell/Dark/Glow`, `MI_UnitMachineShell/Dark/Glow` (`M_Surface` / `M_Glow`) | `MI_SC2_<Faction>_{Shell,Dark,Glow}_Unit` | HQs (`SM_*_HQ`, currently in `/Game/Art/Units`) use the `_Bld` set because they are 3 m boxes |
| `MI_EnvMachine{Shell,Dark,Glow,Accent}`, `MI_EnvHuman{...}`, `MI_EnvClusterShell` | `MI_SC2_{Machine,Human,Cluster}_{Shell,Dark,Glow,Accent}_Env` | The `LOOKS` table of `ImportEnvironmentKit.py` becomes a lookup into these |
| `MI_ContainerBlue`, `MI_Rust`, `MI_Olive`, `MI_Sandbag` (per-actor Shell overrides) | Children of `MI_SC2_Human_Shell_Env` overriding `BaseColor` only | Keeps the campus variety without leaving the master |
| Building meshes (none yet: the actor is a cube with a dynamic `M_CommandUnit`) | `MI_SC2_<Faction>_{Team,Shell,Dark,Glow}_Bld`; scaffolds use the `Construction` variant (Shell `BaseColor (0.50, 0.52, 0.56)`, `Metallic 0.3`) | Team slot is 0 and amber tint comes from gameplay, so no separate Team instance |
| `M_Surface`, `M_Glow` | Kept | Still used by the campus floor and props (`MI_Asphalt`, `MI_Concrete`, `MI_RoadLine`, ...) |

Shared-material generation uses `./x gen build-shared-material` ([`./x help gen`](../../x)); gallery inspection uses `./x editor` ([`./x help editor`](../../x)). `ArtMaterials.shared(name, **parameters)` is the material-instance helper contract; parameter tables below are prototype values, while the generator owns current asset construction.

## 8. Instance parameters (as rendered in the previews)

`Human` and `Machine` slots, prototype values. Colours are linear RGB. Rows not listed are 0 (`ClearCoat`, `PearlAmount`, `HazardStripes`, `SelfLit`).
`TeamColor` is only meaningful on the `Team` slots (default blue / red); it is unused when `UseTeam` is off.

| Parameter | Human Team | Human Shell | Human Dark | Human Glow | Human Accent | Machine Team | Machine Shell | Machine Dark | Machine Glow | Machine Accent |
|---|---|---|---|---|---|---|---|---|---|---|
| `UseTeam` | 1 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 |
| `TeamColor` / `BaseColor` | (0.04, 0.5, 1) | (0.30, 0.40, 0.56) | (0.09, 0.10, 0.12) | (0.05, 0.03, 0.02) | (0.80, 0.55, 0.02) | (1, 0.08, 0.08) | (0.88, 0.90, 0.94) | (0.022, 0.028, 0.04) | (0.02, 0.05, 0.06) | (0.05, 0.005, 0.005) |
| `Metallic` | 0 | 0.22 | 0.5 | 0 | 0.1 | 0 | 0.12 | 0.6 | 0 | 0 |
| `Roughness` | 0.40 | 0.42 | 0.48 | 0.5 | 0.55 | 0.25 | 0.20 | 0.30 | 0.3 | 0.3 |
| `ClearCoat` | 0.2 | 0 | 0 | 0 | 0 | 0.9 | 0.8 | 0.4 | 0 | 0 |
| `PearlTint` / `PearlAmount` | | | | | | | (0.62, 0.8, 1) / 0.30 | | | |
| `BareMetalColor` | (0.62, 0.64, 0.68) | (0.66, 0.68, 0.72) | (0.46, 0.48, 0.52) | | (0.5, 0.5, 0.52) | | (0.85, 0.87, 0.90) | (0.35, 0.40, 0.48) | | |
| `EdgeWear` | 0.4 | 0.7 | 0.55 | 0 | 0.9 | 0 | 0.06 | 0.2 | 0 | 0 |
| `EdgeHighlight` | 0.3 | 0.9 | 0.9 | 0 | 0.5 | 0.5 | 0.65 | 0.8 | 0 | 0 |
| `GrimeColor` | (0.36, 0.32, 0.28) | same | same | same | same | same | (0.62, 0.64, 0.70) | same | same | same |
| `GrimeAmount` | 0.2 | 0.45 | 0.25 | 0 | 0.55 | 0.12 | 0.16 | 0.15 | 0 | 0 |
| `GroundGrime` | 0.3 | 0.55 | 0.4 | 0 | 0.6 | 0 | 0.06 | 0 | 0 | 0 |
| `PanelStrength` | 0.35 | 0.6 | 0.4 | 0 | 0.2 | 0.25 | 0.5 | 0.3 | 0 | 0 |
| `PanelLine` | 0.35 | 0.7 | 0.4 | 0 | 0.2 | 0.35 | 0.6 | 0.3 | 0 | 0 |
| `HazardStripes` | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| `EmissiveColor` | | | | (1, 0.55, 0.15) | | | | | (0.35, 0.85, 1) | (1, 0.05, 0.03) |
| `EmissiveIntensity` (x scope glow) | | | | 1 | | | | | 1 | 1 (kit 1.0, not scaled) |
| `SelfLit` | 0.10 | | | | | 0.35 | | | | |
| `TexPanel` / `TexWear` / `TexGrime` | MetalPlates002 / PaintedMetal014 / Metal046A | same | MetalPlates006 / PaintedMetal014 / Metal010 | same as Team | same as Team | Tiles108 / PaintedMetal014 / Concrete016 | same | same | same | same |

Cluster (`SM_Env_ClusterPylon`) Shell is the Machine Shell with `BaseColor (0.90, 0.92, 0.95)`, `Roughness 0.14`, `ClearCoat 1.0`,
`EdgeHighlight 0.55`, `GrimeAmount 0.18`.

Scope values (asset class; the material instance set for each):

| Scope | Used by | `TileSize` panel / wear / grime (m) | Glow multiplier (`EmissiveIntensity` x) | Grime multiplier | Bake: band / AO ray / ground height |
| --- | --- | --- | --- | --- | --- |
| Unit | six units | 0.9 / 0.7 / 1.3 | 2.0 (equals the current `MI_Unit*Glow` intensity) | 1.0 | 1.6 cm / 0.35 m / 0.45 m |
| Bld | HQs, buildings, scaffolds | 2.4 / 1.6 / 3.0 | 2.5 | 0.8 | 3 cm / 0.6 m / 0.8 m |
| Env | kit | 3.6 / 2.4 / 4.5 | 3.0 (was 4.0 to 5.0; with bloom and the Standard view transform the cyan clipped to white in the previews) | 0.55 | 5 cm / 1.0 m / 1.2 m |

## 9. Readability check (`FieldView.png`)

Campus Zero asphalt, dusk lighting from `GenerateUnitMeshes.py`, 85 mm lens at 130 m: a unit is about 40 to 50 px tall on a
2000 px wide frame. Object pixels are found by differencing against a floor-only render; "vivid" is saturation times value.
Among the 3 % most vivid object pixels:

| | Team hues (blue or red) | Amber | Cyan | Mean vividness, saturated Team px / other saturated px |
| --- | --- | --- | --- | --- |
| Before (flat) | 71 % | 18 % | 10 % | 0.16 / 0.37 |
| After (M_Shared) | 79 % | 16 % | 1 % | 0.25 / 0.31 |

Team colour is the strongest read in both, more so after: the shell is darker and less saturated and the bevel highlights
stay neutral. In the 1:1 crop of the unit cluster no noise is visible; the panel and grime textures only show as a slight
darkening at this distance.

Before shipping, repeat it in the game camera with the five team colours; only blue and red were rendered, and
`Docs/World.md` warns that black-and-yellow hazard stripes must not read as the yellow team.

## 10. Risks and open decisions

* **Bevel masks need a generator change** (3.2). Without it the material still works with `UseBakedMasks` off: paint, panel
  seams, pearl, glow and hazard, but no edge highlight, wear or crevice grime.
* **Vertex-mask resolution:** highlights are one bevel strip wide. If the art owner wants broader wear, the meshes need UVs.
* **Cost:** nine texture samples plus ten lerps per pixel. Texture packing could reduce this. Gallery profiling uses `./x editor` ([`./x help editor`](../../x)); GPU measurements are needed before choosing a cheaper graph.
* **Bloom:** the previews use a compositor Glare (threshold 0.7, strength 0.5) so `Glow` reads as light. Unreal needs the
  project post-process bloom at a similar strength, otherwise emissive strips look flatter than the previews.
* **Machine `Team` red share** is still undecided (`Docs/World.md`): the previews use red inlays plus the lens dome, matching
  the meshes; `SelfLit 0.35` makes the lens glow and can be lowered without touching the graph.
