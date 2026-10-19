# Ribbon looks with Material Canvas nodes

The **Particle Ribbon** output node lets a Material Canvas graph generate a ribbon material: a material type, its shader and a material, using the ribbon renderer's own vertex data.

## Making one

1. In Material Canvas, make a new graph and add the **Particle Ribbon** node (category *Material Outputs*).
2. Build the look from nodes and connect it to the output's **Base Color**, **Alpha** and **Emissive**. Everything in the standard Math, Constants and Scene nodes works (Scene > Time drives scrolling and flicker).
3. Use the nodes of the **Particle Ribbon** category for what is specific to the ribbon:

| Node | What it gives |
|---|---|
| Ribbon Vertex | `UV` (u across the width, v along the length, in tiles when the emitter's Tile Distance is set), `Length` (0 to 1 along the ribbon whatever the tiling), `Age Fade` (the particle colour alpha) and the particle `Color` |
| Ribbon Float / Float2 / Float3 / Float4 / Color / Bool / Int / Uint Input | A value that becomes a property of the material, like the standard Material Inputs, but readable from the ribbon shader |
| Ribbon Sample Texture | A texture and sampler that become material properties, sampled at the uv you connect |

The standard Material Input and Sample Texture nodes read the material through the material pipeline's parameter struct, which the particle shaders do not have, so use the Ribbon versions of those.

4. Save the graph; Material Canvas writes `<graph>.materialtype`, `<graph>.azsl`, `<graph>.shader` and `<graph>.material` next to it. Assign the `.material` to the emitter of a particle (or make a material from the generated material type).

## What the output does

- The graph result is multiplied by the particle colour, so the age fade from the particle's colour curves still fades the ribbon.
- **Mode** (Opaque, Cutout, Blended, Additive, Subtractive, Modulate) is the same blend mode switch the hand-written ribbon material has. Additive and Subtractive multiply the colour by the alpha so the fade works; Cutout clips at an alpha of 0.5.
- The rim, the scene distortion, the soft depth fade and the mask scroll of `ParticleRibbon.materialtype` are not part of the output node. Rebuild them with nodes (a mask is a texture node scrolled by time) or use the hand-written material when you need the distortion.

## Limits

- The generated shader has no distortion pass shader and no soft depth fade.
- The sample texture node only has the pixel-stage path.
