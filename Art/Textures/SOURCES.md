# Texture sources

All assets are CC0 1.0 from ambientCG (https://ambientcg.com); no attribution required.
`./x gen fetch-textures` fetches texture inputs and can regenerate this source table; procedure: [`./x help gen`](../../x). `./x gen render-texture-contact-sheet` produces the labelled material-sphere overview using the same help reference.

- Files live at `Art/Textures/<Category>/<AssetId>/<AssetId>_<Map>.jpg`.
- **Normals are DirectX (green down), Unreal's native convention.** Import with the default NormalMap compression and no green flip.
- Import BaseColor as sRGB; Normal, Roughness, Metallic, AO and Height as linear (non-sRGB).
- Metallic is only present where the source set ships one; otherwise treat the material as dielectric (paint) or drive metalness from the master material.
- Height is kept only where its 2K file is 3 MB or smaller.
- No clean CC0 hazard-stripe material exists on ambientCG or checked alternatives; `PaintedMetal016` is black/yellow stripes heavily rusted through, so use it for derelict/worn trim only and build crisp hazard stripes procedurally in the master material.
- `ContactSheet.png` shows every material as a labelled lit sphere.

| Asset | Source URL | License | Maps kept | Resolution | Normal convention | Intended use |
| --- | --- | --- | --- | --- | --- | --- |
| PaintedMetal/Metal032 | https://ambientcg.com/a/Metal032 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Clean smooth grey-blue painted steel: human shell base, flat armour plates; tint toward pearl for the Machine |
| PaintedMetal/PaintedMetal014 | https://ambientcg.com/a/PaintedMetal014 | CC0 | BaseColor, Normal, Roughness, Metallic, AO, Height | 2K JPG | DirectX | Worn grey-blue painted plate: human shell, repaired/scuffed panels |
| Metal/Metal010 | https://ambientcg.com/a/Metal010 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Brushed blue-grey steel: trim, pipes, pistons, barrel bands |
| Metal/Metal046A | https://ambientcg.com/a/Metal046A | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Dark gunmetal: Dark slot mechanical parts, joints, engine housings |
| Panel/MetalPlates002 | https://ambientcg.com/a/MetalPlates002 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Bolted plate grid with seams and rivets: building walls, roofs, vehicle hulls (normal detail) |
| Panel/MetalPlates006 | https://ambientcg.com/a/MetalPlates006 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Dark studded sci-fi plating: Machine-side and heavy-armour surfaces (normal detail) |
| Tread/DiamondPlate001 | https://ambientcg.com/a/DiamondPlate001 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Diamond tread plate: catwalks, vehicle decks, ramps |
| Tread/MetalWalkway006 | https://ambientcg.com/a/MetalWalkway006 | CC0 | BaseColor, Normal, Roughness, Metallic, Height | 2K JPG | DirectX | Open steel grating: walkways, vents, radiators (use with opacity or as a visual-only albedo) |
| Rubber/Rubber004 | https://ambientcg.com/a/Rubber004 | CC0 | BaseColor, Normal, Roughness, Height | 2K JPG | DirectX | Dark smooth rubber: tyres, tracks, hoses, seals |
| Hazard/PaintedMetal016 | https://ambientcg.com/a/PaintedMetal016 | CC0 | BaseColor, Normal, Roughness, Metallic, AO, Height | 2K JPG | DirectX | Rusted black/yellow hazard stripes: worn trim only; crisp hazard stripes will be procedural (keep small, see colour language) |
| Concrete/Concrete024 | https://ambientcg.com/a/Concrete024 | CC0 | BaseColor, Normal, Roughness, Height | 2K JPG | DirectX | Clean smooth concrete: Campus Zero pads, plazas, building bases |
| Concrete/Concrete016 | https://ambientcg.com/a/Concrete016 | CC0 | BaseColor, Normal, Roughness, Height | 2K JPG | DirectX | Weathered stained blue-grey concrete: walls, barriers, old slabs |
| Asphalt/Asphalt026A | https://ambientcg.com/a/Asphalt026A | CC0 | BaseColor, Normal, Roughness, AO, Height | 2K JPG | DirectX | Dark cracked asphalt: Campus Zero roads and parking |
| Ground/Gravel004 | https://ambientcg.com/a/Gravel004 | CC0 | BaseColor, Normal, Roughness, AO, Height | 2K JPG | DirectX | Grey chunky gravel: Campus Zero verges, rooftops, rubble ground |
| Ground/Gravel006 | https://ambientcg.com/a/Gravel006 | CC0 | BaseColor, Normal, Roughness, AO, Height | 2K JPG | DirectX | Dark compacted dirt/gravel: bare ground and worn paths |
| SciFiFloor/Tiles108 | https://ambientcg.com/a/Tiles108 | CC0 | BaseColor, Normal, Roughness, AO, Height | 2K JPG | DirectX | Black gridded tech tiles: data-hall and lab floors (Machine megastructure floors) |
