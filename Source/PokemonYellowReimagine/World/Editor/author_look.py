"""Author the anime look's materials in /Game/Look (m2 world worker).

Run under the build lock with:
    UnrealEditor-Cmd PokemonYellowReimagine.uproject -run=pythonscript
        -script=<this file> -unattended -nop4 -NullRHI

- M_ToonPost: post-process, before tonemapping. Lighting is quantised to two flat tones: the lit tone and a
  slightly cool shade tone, both taken from the G-buffer base colour, with the split decided by the ratio of the
  lit scene colour to the base colour (so every mesh from every worker gets it). Bright effects and unlit
  surfaces (the sky, emissive move effects) pass through. Dark ink outlines (#1f1f2e) come from a depth and
  normal edge detect about 2 px wide at 1080p, and a light haze lifts far ground toward the horizon colour.
  Scalar parameters: Threshold, LitTone, ShadeTone, OutlineWidth, DepthEdge, NormalEdge, FxKeep, HazeStart,
  HazeEnd, HazeAmount. Vector parameters: ShadeTint, Ink, Haze.
  m4v: the area's light and colour grade, in the same one pass (World/PYRAreaLook sets it per area from
  Content/Look/Kit/areas.json; every default leaves the m4u picture as it was):
  - vectors GradeTint (the lit picture's colour), ShadowTint (times ShadeTint on the shade tone), SkyTint (the sky);
  - ToneColor, ToneAmount: the light's colour on the lit tone (the lit surfaces' luminance in ToneColor, mixed in by
    ToneAmount: golden afternoon, lamp light), so the shade keeps its own cooler colour;
  - scalars Saturation, Contrast (about mid grey), Warmth (white balance: + warm, - cool), InkWidth (times
    OutlineWidth), InkStrength (the outlines' opacity);
  - DappleAmount, DappleScale, DappleCover: leaf shade breaking the lit tone on the ground (world-space pattern; lower
    DappleCover shades more of it, leaving pools of sun: Viridian Forest);
  - GlowAmount, GlowRadius, GlowCenter (world): a pool of light round the player, darker away from it (Mt. Moon);
  - RayAmount, RayColor, RaySource (screen uv, may be above the top edge), RayEdge (soft 0.3 .. crisp 0.05): painted
    light shafts (forest, skylight);
  - Vignette (darker corners) and Grain (a fixed paper grain);
  - GroundInk, GroundInkThreshold: ink where flat up-facing ground changes colour (a lawn's edge on a path);
  - GrassLit (default 0.6, m3's soft meadows) and GrassInk (default 0): the tall grass's lit floor and its outline; the
    anime areas draw the tufts with two tones and ink (0 and 1).
  The ring dim (journey's M_RingDim) runs after tonemapping, so it always lands on top of this.
- M_Sky: unlit, two-sided dome gradient from #6ec6f0 (reached about 15 degrees up) to near-white at the horizon (Top, Horizon).
  m4v: SkyTex (a painted equirectangular sky, /Game/Look/Kit/Textures/T_Kit_Sky) mixed over the gradient by SkyTexAmount
  (default 0: the m4u gradient), times SkyTexTint, lowered by SkyTexOffset (v) so its clouds sit at the horizon,
  repeated SkyTexScale times round the dome (an area may paint its own texture there: the forest's leaf canopy);
  World/PYRAreaLook sets them per area.
- M_Flat: lit flat colour (vector parameter Color), fully rough with no specular, for every primitive.
- M_Grass: M_Flat plus a sway in world position offset for the tall-grass tufts.
Prints PYR_LOOK_READY on success.
"""
import unreal

FOLDER = '/Game/Look'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def srgb(hex_code):
    """Linear colour from an sRGB hex string (converted here: unreal.Color's argument order is not RGB)."""
    def lin(v):
        v /= 255.0
        return ((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92
    return unreal.LinearColor(lin(int(hex_code[0:2], 16)), lin(int(hex_code[2:4], 16)), lin(int(hex_code[4:6], 16)), 1.0)


def fresh(name):
    path = f'{FOLDER}/{name}'
    if assets.does_asset_exist(path):
        asset = assets.load_asset(path)
        lib.delete_all_material_expressions(asset)
        return asset
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


class Graph:
    def __init__(self, mat):
        self.mat = mat

    def node(self, cls, x, y, **props):
        expr = lib.create_material_expression(self.mat, cls, x, y)
        for key, value in props.items():
            expr.set_editor_property(key, value)
        return expr

    def link(self, a, a_out, b, b_in):
        if not lib.connect_material_expressions(a, a_out, b, b_in):
            raise RuntimeError(f'connect {a.get_name()}.{a_out} -> {b.get_name()}.{b_in} failed')

    def out(self, a, prop, a_out=''):
        if not lib.connect_material_property(a, a_out, prop):
            raise RuntimeError(f'connect {a.get_name()} -> {prop} failed')

    def scalar(self, name, value, x, y):
        return self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def vector(self, name, value, x, y):
        return self.node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=value)

    def save(self):
        lib.recompile_material(self.mat)
        assets.save_loaded_asset(self.mat)


# ------------------------------------------------------------------------------------------------ M_ToonPost
TOON_CODE = r'''
struct FPyrNoise
{
    float H(float2 q) { return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
    float V(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H(i), H(i + float2(1, 0)), f.x), lerp(H(i + float2(0, 1)), H(i + float2(1, 1)), f.x), f.y);
    }
};
FPyrNoise N;
float2 uv = GetDefaultSceneTextureUV(Parameters, 14);
float px = max(Width * InkWidth * View.ViewSizeAndInvSize.y / 1080.0, 0.5);
float2 o = View.BufferSizeAndInvSize.zw * px;
float3 lum = float3(0.3, 0.59, 0.11);
float3 outc = Scene;
float bl = dot(Base, lum);
// m3: the grass stencil counts only where the grass is the surface seen (custom depth matches scene depth); custom depth
// ignores occlusion, so grass behind a wall must not soften the wall.
float isGrass = (abs(Stencil - 1.0) < 0.5 && abs(CustomDepth - Depth) < max(8.0, Depth * 0.01)) ? 1.0 : 0.0;
float sl = dot(Scene, lum);
if (bl > 0.004 && Depth < 220000.0)
{
    float light = sl / bl;
    float lit = smoothstep(Threshold - 0.03, Threshold + 0.03, light);
    // m4v: dappled leaf shade on lit ground under a canopy (a world-space pattern breaks the lit tone).
    if (DappleAmount > 0.001)
    {
        float2 dp = WPos.xy / max(DappleScale, 1.0);
        float d = N.V(dp) * 0.65 + N.V(dp * 2.3 + 17.1) * 0.35;
        lit *= 1.0 - DappleAmount * smoothstep(DappleCover, DappleCover + 0.05, d);
    }
    lit = max(lit, isGrass * GrassLit);
    float3 toon = Base * lerp(ShadeTone * ShadeTint * ShadowTint, LitTone.xxx, lit);
    // m4v: the area's light colour on the lit tone (golden afternoon, lamp light), the shade keeping ShadowTint.
    if (ToneAmount > 0.001) toon = lerp(toon, dot(toon, lum) * ToneColor, ToneAmount * lit);
    float fx = saturate((light - FxKeep) / max(FxKeep, 0.01));
    outc = lerp(toon, Scene, fx);
    float haze = saturate((Depth - HazeStart) / max(HazeEnd - HazeStart, 1.0)) * HazeAmount;
    outc = lerp(outc, Haze * LitTone, haze);
    // m4v: a pool of light round the player (Mt. Moon), darker away from it.
    if (GlowAmount > 0.001)
    {
        float r = distance(WPos.xy, GlowCenter.xy);
        float g = smoothstep(GlowRadius, GlowRadius * 2.6, r);
        outc *= lerp(1.0 + 0.45 * GlowAmount, 1.0 - 0.82 * GlowAmount, g);
    }
    outc *= GradeTint;
}
else outc *= SkyTint;
// m4v: the grade (white balance, saturation, contrast about mid grey).
outc *= float3(1.0 + 0.12 * Warmth, 1.0 + 0.03 * Warmth, 1.0 - 0.16 * Warmth);
float gl = dot(outc, lum);
outc = max(lerp(gl.xxx, outc, Saturation), 0.0);
outc = 0.18 * pow(max(outc / 0.18, 0.0), Contrast);
// m4v: painted light shafts from RaySource (screen uv; above the top edge for light through a canopy or a skylight).
if (RayAmount > 0.001)
{
    float aspect = View.ViewSizeAndInvSize.x * View.ViewSizeAndInvSize.w;
    float2 rd = (uv - RaySource.xy) * float2(aspect, 1.0);
    float ang = atan2(rd.x, rd.y);
    float bands = 0.5 + 0.3 * sin(ang * 23.0 + 1.3) + 0.2 * sin(ang * 41.0 + 4.1);
    float shaft = smoothstep(0.92 - RayEdge, 0.92, bands) * saturate(1.0 - length(rd) * 0.9);
    outc += RayColor * RayAmount * shaft;
}
float dL = SceneTextureLookup(uv + float2(-o.x, 0), 1, false).r;
float dR = SceneTextureLookup(uv + float2(o.x, 0), 1, false).r;
float dU = SceneTextureLookup(uv + float2(0, -o.y), 1, false).r;
float dD = SceneTextureLookup(uv + float2(0, o.y), 1, false).r;
float dNear = min(min(dL, dR), min(min(dU, dD), Depth));
float dEdge = (abs(dL + dR - 2.0 * Depth) + abs(dU + dD - 2.0 * Depth)) / max(dNear, 1.0);
float3 nL = SceneTextureLookup(uv + float2(-o.x, 0), 8, false).rgb;
float3 nR = SceneTextureLookup(uv + float2(o.x, 0), 8, false).rgb;
float3 nU = SceneTextureLookup(uv + float2(0, -o.y), 8, false).rgb;
float3 nD = SceneTextureLookup(uv + float2(0, o.y), 8, false).rgb;
float nEdge = max(max(1.0 - dot(Normal, nL), 1.0 - dot(Normal, nR)), max(1.0 - dot(Normal, nU), 1.0 - dot(Normal, nD)));
float edge = max(step(DepthEdge, dEdge) * step(dNear, 220000.0), step(NormalEdge, nEdge) * (1.0 - saturate((dNear - 6000.0) / 6000.0)));
edge *= 1.0 - saturate((dNear - 150000.0) / 100000.0);
// m4v: ink where flat ground changes colour (a lawn's edge on a path): the base colour's chromaticity jumps between
// neighbours that all face up.
// Judge round 2: not on water itself (its painted ripples' colour jumps drew dense dark flecks across the far sea);
// a bank's edge still inks from the land side.
bool bWater = Base.b > Base.r * 1.6 && Base.b > Base.g * 1.05;
if (GroundInk > 0.001 && Normal.z > 0.92 && Depth < 6000.0 && !bWater)
{
    float2 g = o * 1.5;
    float3 bc[4];
    bc[0] = SceneTextureLookup(uv + float2(-g.x, 0), 5, false).rgb;
    bc[1] = SceneTextureLookup(uv + float2(g.x, 0), 5, false).rgb;
    bc[2] = SceneTextureLookup(uv + float2(0, -g.y), 5, false).rgb;
    bc[3] = SceneTextureLookup(uv + float2(0, g.y), 5, false).rgb;
    float3 cc = Base / max(dot(Base, lum), 0.03);
    float jump = 0.0;
    for (int k = 0; k < 4; k++)
        jump = max(jump, length(bc[k] / max(dot(bc[k], lum), 0.03) - cc));
    float up = min(min(nL.z, nR.z), min(nU.z, nD.z));
    edge = max(edge, GroundInk * smoothstep(GroundInkThreshold, GroundInkThreshold + 0.15, jump) * step(0.92, up)
        * (1.0 - saturate((Depth - 3000.0) / 3000.0)));
}
edge *= 1.0 - isGrass * (1.0 - GrassInk);  // Tall-grass blades (stencil 1) without ink by default, so meadows read as soft masses.
outc = lerp(outc, Ink, edge * InkStrength);
// m4v: darker corners and a fixed paper grain.
if (Vignette > 0.001)
{
    float2 vc = (uv - 0.5) * float2(1.0, 0.75);
    outc *= 1.0 - Vignette * smoothstep(0.08, 0.42, dot(vc, vc) * 2.2);
}
if (Grain > 0.001) outc *= 1.0 + Grain * (N.H(floor(uv * View.ViewSizeAndInvSize.xy)) - 0.5);
return outc;
'''

mat = fresh('M_ToonPost')
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
mat.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)
mat.set_editor_property('blendable_priority', -10)
g = Graph(mat)
custom = g.node(unreal.MaterialExpressionCustom, -200, 0, code=TOON_CODE,
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='ToonShade')
names = ['Scene', 'Base', 'Depth', 'Normal', 'Stencil', 'CustomDepth', 'Threshold', 'LitTone', 'ShadeTone', 'ShadeTint', 'Width', 'DepthEdge',
         'NormalEdge', 'FxKeep', 'Ink', 'Haze', 'HazeStart', 'HazeEnd', 'HazeAmount',
         # m4v: the grade
         'WPos', 'GradeTint', 'ShadowTint', 'SkyTint', 'Saturation', 'Contrast', 'Warmth', 'InkWidth', 'InkStrength',
         'DappleAmount', 'DappleScale', 'GlowAmount', 'GlowRadius', 'GlowCenter', 'RayAmount', 'RayColor', 'RaySource',
         'Vignette', 'Grain', 'ToneColor', 'ToneAmount', 'GroundInk', 'GroundInkThreshold', 'GrassLit', 'GrassInk', 'DappleCover', 'RayEdge']
inputs = []
for n in names:
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', n)
    inputs.append(ci)
custom.set_editor_property('inputs', inputs)


def scene_texture(tid, x, y, mask):
    st = g.node(unreal.MaterialExpressionSceneTexture, x, y, scene_texture_id=tid)
    m = g.node(unreal.MaterialExpressionComponentMask, x + 250, y, r=True, g=mask > 1, b=mask > 2, a=False)
    g.link(st, 'Color', m, '')
    return m


g.link(scene_texture(unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, -900, -400, 3), '', custom, 'Scene')
g.link(scene_texture(unreal.SceneTextureId.PPI_BASE_COLOR, -900, -250, 3), '', custom, 'Base')
g.link(scene_texture(unreal.SceneTextureId.PPI_SCENE_DEPTH, -900, -100, 1), '', custom, 'Depth')
g.link(scene_texture(unreal.SceneTextureId.PPI_WORLD_NORMAL, -900, 50, 3), '', custom, 'Normal')
g.link(scene_texture(unreal.SceneTextureId.PPI_CUSTOM_STENCIL, -900, 200, 1), '', custom, 'Stencil')
g.link(scene_texture(unreal.SceneTextureId.PPI_CUSTOM_DEPTH, -900, 350, 1), '', custom, 'CustomDepth')
y = 200
for name, value in (('Threshold', 0.42), ('LitTone', 1.0), ('ShadeTone', 0.66), ('Width', 1.0), ('DepthEdge', 0.06),
                    ('NormalEdge', 0.45), ('FxKeep', 3.0), ('HazeStart', 12000.0), ('HazeEnd', 140000.0),
                    ('HazeAmount', 0.45)):
    g.link(g.scalar('OutlineWidth' if name == 'Width' else name, value, -700, y), '', custom, name)
    y += 90
for name, value in (('ShadeTint', unreal.LinearColor(0.80, 0.86, 1.0, 1)), ('Ink', srgb('1f1f2e')),
                    ('Haze', srgb('e9f6fc'))):
    g.link(g.vector(name, value, -700, y), '', custom, name)
    y += 160
# m4v: the grade's parameters, all neutral by default (the m4u picture).
for name, value in (('Saturation', 1.0), ('Contrast', 1.0), ('Warmth', 0.0), ('InkWidth', 1.0), ('InkStrength', 1.0),
                    ('DappleAmount', 0.0), ('DappleScale', 220.0), ('GlowAmount', 0.0), ('GlowRadius', 450.0),
                    ('RayAmount', 0.0), ('Vignette', 0.0), ('Grain', 0.0), ('ToneAmount', 0.0), ('GroundInk', 0.0),
                    ('GroundInkThreshold', 0.55), ('GrassLit', 0.6), ('GrassInk', 0.0), ('DappleCover', 0.5), ('RayEdge', 0.3)):
    g.link(g.scalar(name, value, -1100, y), '', custom, name)
    y += 90
for name, value in (('GradeTint', unreal.LinearColor(1, 1, 1, 1)), ('ShadowTint', unreal.LinearColor(1, 1, 1, 1)),
                    ('SkyTint', unreal.LinearColor(1, 1, 1, 1)), ('GlowCenter', unreal.LinearColor(0, 0, 0, 1)),
                    ('RayColor', unreal.LinearColor(1.0, 0.95, 0.8, 1)), ('RaySource', unreal.LinearColor(0.5, -0.3, 0, 1)),
                    ('ToneColor', unreal.LinearColor(1.0, 1.0, 1.0, 1))):
    g.link(g.vector(name, value, -1100, y), '', custom, name)
    y += 160
g.link(g.node(unreal.MaterialExpressionWorldPosition, -1100, y), '', custom, 'WPos')
g.out(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
g.save()
unreal.log('PYR_LOOK M_ToonPost')

# ------------------------------------------------------------------------------------------------ M_Sky
mat = fresh('M_Sky')
mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('two_sided', True)
g = Graph(mat)
wp = g.node(unreal.MaterialExpressionWorldPosition, -1200, 0)
cam = g.node(unreal.MaterialExpressionCameraPositionWS, -1200, 150)
d = g.node(unreal.MaterialExpressionSubtract, -1000, 50)
g.link(wp, '', d, 'A')
g.link(cam, '', d, 'B')
n = g.node(unreal.MaterialExpressionNormalize, -850, 50)
g.link(d, '', n, '')
z = g.node(unreal.MaterialExpressionComponentMask, -700, 50, r=False, g=False, b=True, a=False)
g.link(n, '', z, '')
k = g.node(unreal.MaterialExpressionConstant, -700, 180, r=4.0)
zk = g.node(unreal.MaterialExpressionMultiply, -550, 50)
g.link(z, '', zk, 'A')
g.link(k, '', zk, 'B')
sat = g.node(unreal.MaterialExpressionSaturate, -420, 50)
g.link(zk, '', sat, '')
curve = g.node(unreal.MaterialExpressionPower, -300, 50)
g.link(sat, '', curve, 'Base')
e = g.node(unreal.MaterialExpressionConstant, -420, 180, r=0.5)
g.link(e, '', curve, 'Exp')
top = g.vector('Top', srgb('6ec6f0'), -420, 300)
horizon = g.vector('Horizon', srgb('e2f3fc'), -420, 460)
lerp = g.node(unreal.MaterialExpressionLinearInterpolate, -150, 200)
g.link(horizon, '', lerp, 'A')
g.link(top, '', lerp, 'B')
g.link(curve, '', lerp, 'Alpha')
SKY_CODE = r'''
float3 d = normalize(WPos - Cam);
float2 uv = float2(atan2(d.y, d.x) * 0.15915494 + 0.5, saturate(acos(clamp(d.z, -1.0, 1.0)) * 0.31830989 - Offset)) * Scale;
float3 painted = Texture2DSampleLevel(Tex, TexSampler, uv, 0).rgb * Tint;
return lerp(Grad, painted, Amount);
'''
sky = g.node(unreal.MaterialExpressionCustom, 50, 200, code=SKY_CODE, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
             description='PaintedSky')
ins = []
for nm in ('Grad', 'WPos', 'Cam', 'Tex', 'Amount', 'Tint', 'Offset', 'Scale'):
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', nm)
    ins.append(ci)
sky.set_editor_property('inputs', ins)
g.link(lerp, '', sky, 'Grad')
g.link(g.node(unreal.MaterialExpressionWorldPosition, -150, 500), '', sky, 'WPos')
g.link(g.node(unreal.MaterialExpressionCameraPositionWS, -150, 600), '', sky, 'Cam')
sky_tex = g.node(unreal.MaterialExpressionTextureObjectParameter, -150, 700, parameter_name='SkyTex')
painted_sky = unreal.load_asset('/Game/Look/Kit/Textures/T_Kit_Sky') or unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
if painted_sky:
    sky_tex.set_editor_property('texture', painted_sky)
g.link(sky_tex, '', sky, 'Tex')
g.link(g.scalar('SkyTexAmount', 0.0, -150, 850), '', sky, 'Amount')
g.link(g.vector('SkyTexTint', unreal.LinearColor(1, 1, 1, 1), -150, 950), '', sky, 'Tint')
g.link(g.scalar('SkyTexOffset', 0.0, -150, 1100), '', sky, 'Offset')
g.link(g.scalar('SkyTexScale', 1.0, -150, 1200), '', sky, 'Scale')
g.out(sky, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
g.save()
unreal.log('PYR_LOOK M_Sky')


# ------------------------------------------------------------------------------------------------ M_Flat, M_Grass
def flat(name, sway):
    mat = fresh(name)
    for flag in ('used_with_instanced_static_meshes', 'used_with_skeletal_mesh', 'used_with_static_lighting'):
        mat.set_editor_property(flag, True)
    g = Graph(mat)
    colour = g.vector('Color', unreal.LinearColor(0.8, 0.8, 0.8, 1), -500, 0)
    g.out(colour, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, -500, 200, r=1.0), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, -500, 300, r=0.0), unreal.MaterialProperty.MP_SPECULAR)
    if sway:
        code = ('float h = saturate(Local.z / 100.0 + 0.5);'
                'float t = Time * 1.7 + World.x * 0.004 + World.y * 0.003;'
                'return float3(sin(t) * 7.0, cos(t * 0.8) * 5.0, 0) * h * h;')
        custom = g.node(unreal.MaterialExpressionCustom, -300, 500, code=code,
                        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description='Sway')
        ins = []
        for nm in ('Local', 'World', 'Time'):
            ci = unreal.CustomInput()
            ci.set_editor_property('input_name', nm)
            ins.append(ci)
        custom.set_editor_property('inputs', ins)
        g.link(g.node(unreal.MaterialExpressionLocalPosition, -700, 450), '', custom, 'Local')
        g.link(g.node(unreal.MaterialExpressionWorldPosition, -700, 550), '', custom, 'World')
        g.link(g.node(unreal.MaterialExpressionTime, -700, 650), '', custom, 'Time')
        g.out(custom, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    g.save()
    unreal.log(f'PYR_LOOK {name}')


flat('M_Flat', False)
flat('M_Grass', True)
unreal.log('PYR_LOOK_READY')
