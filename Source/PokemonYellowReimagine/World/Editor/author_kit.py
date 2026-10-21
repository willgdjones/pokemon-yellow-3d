"""Author the m4v anime material kit in /Game/Look/Kit (look worker).

Run under the build lock, after the kit's textures are imported (Tools/look/README.md):
    UnrealEditor-Cmd PokemonYellowReimagine.uproject -run=pythonscript
        -script=<this file> -unattended -nop4 -NullRHI
PYR_KIT_JSON may point at another kit.json (default: Content/Look/Kit/kit.json in the project).

- M_AnimeKit: one lit, fully rough, non-specular material for every painted surface. Base (a T_Kit_ texture: the paint in
  RGB, its ink lines in alpha) is projected in world space every Tiling cm from the three axes, the axes blended with
  BlendSharpness (high for roofs and walls, so tile courses never double), so any box, cylinder or roof tiles without
  UVs. CelSteps (0 off) flattens the paint's own light and dark into that many flat tones; Tint multiplies the paint
  (white: as painted); the ink is drawn in InkColor (#1f1f2e) where alpha says, times InkStrength. The two-tone cel
  lighting itself is M_ToonPost's step (Threshold), the same one the characters get. PaintAverage records the texture's
  average linear colour (PYRKit tints a surface to a palette colour as Colour / PaintAverage). Where an instance's huge
  scale has underflowed its vertex normal (Kanto's far sea), the projection uses the facet normal from the screen
  derivatives instead (a zero normal made NaN, drawn black). UseMeshUV 1 samples Base
  with the mesh's own UVs instead (the generated props, whose pictures are UV-mapped) and InkFromAlpha 0 ignores alpha.
- M_AnimeKitSway: the same with M_Grass's wind sway in world position offset (the tall-grass tufts).
- M_AnimeKitFoliage: the same, masked: near the silhouette the leaf clumps' darker gaps are cut out (EdgeWidth), so a
  round crown's outline is a scalloped run of clumps (tree crowns, bushes, forest masses). RimInk (all masters) inks the
  band where a surface turns edge-on to the camera (a post's cap ellipse, a flower's or a clump's rim).
- MI_Kit_<Surface> for every surface in kit.json (its texture, tiling, ink strength, cel steps and blend sharpness).
- MI_KitProp_<id> for every generated prop in /Game/Props/<id> (M_<id>_0's base-colour texture on the prop's UVs,
  CelSteps 4), so the props are painted through the kit too.
Prints PYR_KIT_READY <count> on success.
"""
import json
import os
from pathlib import Path

import unreal

FOLDER = '/Game/Look/Kit'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def srgb(hex_code):
    def lin(v):
        v /= 255.0
        return ((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92
    return unreal.LinearColor(lin(int(hex_code[0:2], 16)), lin(int(hex_code[2:4], 16)), lin(int(hex_code[4:6], 16)), 1.0)


def fresh_material(name):
    path = f'{FOLDER}/{name}'
    if assets.does_asset_exist(path):
        asset = assets.load_asset(path)
        lib.delete_all_material_expressions(asset)
        return asset
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


KIT_SAMPLE = r'''
float4 c;
// The vertex normal, or where an instance's huge scale underflowed it to nothing (Kanto's far sea), the surface's own
// facet normal from the screen derivatives.
float nl = dot(Normal, Normal);
float3 ng = cross(ddx(World), ddy(World));
float3 nn = nl > 1e-12 ? Normal * rsqrt(nl) : ng * rsqrt(max(dot(ng, ng), 1e-20));
float ndv = abs(dot(nn, normalize(CamVec)));
if (UseMeshUV > 0.5)
{
    c = Texture2DSample(Tex, TexSampler, UV);
}
else
{
    float3 w = pow(abs(nn), BlendSharpness);
    w /= max(w.x + w.y + w.z, 0.0001);
    float2 uz = World.xy / Tiling;
    float2 ux = float2(World.y, -World.z) / Tiling;
    float2 uy = float2(World.x, -World.z) / Tiling;
    float2 zx = ddx(uz), zy = ddy(uz), xx = ddx(ux), xy = ddy(ux), yx = ddx(uy), yy = ddy(uy);
    c = 0;
    float sum = 0;
    if (w.z > 0.03) { c += Texture2DSampleGrad(Tex, TexSampler, uz, zx, zy) * w.z; sum += w.z; }
    if (w.x > 0.03) { c += Texture2DSampleGrad(Tex, TexSampler, ux, xx, xy) * w.x; sum += w.x; }
    if (w.y > 0.03) { c += Texture2DSampleGrad(Tex, TexSampler, uy, yx, yy) * w.y; sum += w.y; }
    c /= max(sum, 0.0001);
}
if (any(isnan(c))) c = float4(PaintAverage.rgb, 0);
'''

KIT_CODE = KIT_SAMPLE + r'''
// Detail: the paint's own light and dark kept at this fraction (1: as painted; ground at a half reads calm at a distance).
float3 paint = lerp(PaintAverage.rgb, c.rgb, Detail);
if (CelSteps >= 2.0)
{
    float L = max(dot(paint, float3(0.3, 0.59, 0.11)), 0.0001);
    float q = L * CelSteps;
    float s = (floor(q) + smoothstep(0.3, 0.7, frac(q))) / CelSteps;
    paint *= max(s, 0.5 / CelSteps) / L;
}
paint *= Tint.rgb;
// A painted shade side (posts, trunks): the faces turned toward ShadeDir take ShadeColor, a hard two-tone split whatever
// the sun does, as an anime background paints a rounded post.
if (ShadeAmount > 0.001) paint *= lerp(float3(1, 1, 1), ShadeColor.rgb, ShadeAmount * smoothstep(0.1, 0.2, dot(nn, ShadeDir.xyz)));
float ink = saturate(c.a * InkFromAlpha * InkStrength);
// Rim ink: a dark band where the surface turns edge-on to the camera (a post's cap ellipse, a flower's or a leaf clump's
// rim), as an anime background inks every rounded form.
ink = max(ink, RimInk * smoothstep(0.2, 0.13, ndv));
return lerp(paint, InkColor.rgb, ink) + PaintAverage.rgb * 0.0;
'''

# The foliage master's opacity: the leaf clumps' gaps are cut out near the silhouette, so a crown's outline is a scalloped
# run of clumps rather than a smooth sphere (the mesh is the same).
FOLIAGE_CODE = KIT_SAMPLE + r'''
float lum = dot(c.rgb, float3(0.3, 0.59, 0.11));
float rim = saturate(1.0 - ndv / max(EdgeWidth, 0.01));
return (lum * 1.6 + 0.35 - rim * 1.1).xxx;
'''
KIT_INPUTS = ['Tex', 'World', 'Normal', 'UV', 'CamVec', 'Tiling', 'BlendSharpness', 'CelSteps', 'Tint', 'InkColor', 'InkStrength',
              'InkFromAlpha', 'UseMeshUV', 'PaintAverage', 'RimInk', 'Detail', 'ShadeAmount', 'ShadeDir', 'ShadeColor']

SWAY_CODE = ('float h = saturate(Local.z / 100.0 + 0.5);'
             'float t = Time * 1.7 + World.x * 0.004 + World.y * 0.003;'
             'return float3(sin(t) * 7.0, cos(t * 0.8) * 5.0, 0) * h * h;')


def custom(mat, code, names, x, y, description):
    node = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property('description', description)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        inputs.append(ci)
    node.set_editor_property('inputs', inputs)
    return node


def link(a, a_out, b, b_in):
    if not lib.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f'connect {a.get_name()}.{a_out} -> {b.get_name()}.{b_in} failed')


def node(mat, cls, x, y, **props):
    e = lib.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def kit_inputs(mat, target, names):
    """The kit's inputs (one parameter node each) into a Custom node."""
    shared = {}
    for name in names:
        if name == 'Tex':
            tex = node(mat, unreal.MaterialExpressionTextureObjectParameter, -900, -300, parameter_name='Base')
            white = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
            if white:
                tex.set_editor_property('texture', white)
            shared[name] = tex
        elif name == 'World':
            shared[name] = node(mat, unreal.MaterialExpressionWorldPosition, -900, -150)
        elif name == 'Normal':
            shared[name] = node(mat, unreal.MaterialExpressionVertexNormalWS, -900, -60)
        elif name == 'UV':
            shared[name] = node(mat, unreal.MaterialExpressionTextureCoordinate, -900, 30)
        elif name == 'CamVec':
            shared[name] = node(mat, unreal.MaterialExpressionCameraVectorWS, -900, 90)
        elif name in VECTORS:
            shared[name] = node(mat, unreal.MaterialExpressionVectorParameter, -900, 0, parameter_name=name, default_value=VECTORS[name])
        else:
            shared[name] = node(mat, unreal.MaterialExpressionScalarParameter, -900, 0, parameter_name=name, default_value=SCALARS[name])
        link(shared[name], '', target, name)
    return shared


def author_master(name, sway, foliage=False):
    mat = fresh_material(name)
    for flag in ('used_with_instanced_static_meshes', 'used_with_static_lighting'):
        mat.set_editor_property(flag, True)
    if foliage:
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('opacity_mask_clip_value', 0.5)
    kit = custom(mat, KIT_CODE, KIT_INPUTS, -300, 0, 'AnimeKit')
    kit_inputs(mat, kit, KIT_INPUTS)
    if foliage:
        mask = custom(mat, FOLIAGE_CODE, KIT_INPUTS[:-5] + ['EdgeWidth'], -300, 250, 'FoliageEdge')
        kit_inputs(mat, mask, KIT_INPUTS[:-5] + ['EdgeWidth'])
        if not lib.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY_MASK):
            raise RuntimeError('opacity mask')
    if not lib.connect_material_property(kit, '', unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError('base colour')
    lib.connect_material_property(node(mat, unreal.MaterialExpressionConstant, -300, 300, r=1.0), '', unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(node(mat, unreal.MaterialExpressionConstant, -300, 400, r=0.0), '', unreal.MaterialProperty.MP_SPECULAR)
    if sway:
        s = custom(mat, SWAY_CODE, ['Local', 'World', 'Time'], -300, 600, 'Sway')
        link(node(mat, unreal.MaterialExpressionLocalPosition, -700, 550), '', s, 'Local')
        link(node(mat, unreal.MaterialExpressionWorldPosition, -700, 650), '', s, 'World')
        link(node(mat, unreal.MaterialExpressionTime, -700, 750), '', s, 'Time')
        lib.connect_material_property(s, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    lib.recompile_material(mat)
    assets.save_loaded_asset(mat)
    unreal.log(f'PYR_KIT {name}')
    return mat


SCALARS = {'Tiling': 300.0, 'BlendSharpness': 6.0, 'CelSteps': 0.0, 'InkStrength': 1.0, 'InkFromAlpha': 1.0, 'UseMeshUV': 0.0,
           'RimInk': 0.0, 'EdgeWidth': 0.45, 'Detail': 1.0, 'ShadeAmount': 0.0}
VECTORS = {'Tint': unreal.LinearColor(1, 1, 1, 1), 'InkColor': srgb('1f1f2e'), 'PaintAverage': unreal.LinearColor(0.5, 0.5, 0.5, 1),
           'ShadeDir': unreal.LinearColor(1, 0, 0, 0), 'ShadeColor': unreal.LinearColor(0.6, 0.6, 0.72, 1)}


def instance(name, folder, parent, textures, scalars, vectors):
    path = f'{folder}/{name}'
    if assets.does_asset_exist(path):
        mi = assets.load_asset(path)
    else:
        mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    for key, value in textures.items():
        # UE 5.8's setter returns False even when it sets the texture: read it back instead.
        lib.set_material_instance_texture_parameter_value(mi, key, value)
        if lib.get_material_instance_texture_parameter_value(mi, key) != value:
            raise RuntimeError(f'{name}: texture parameter {key}')
    for key, value in scalars.items():
        lib.set_material_instance_scalar_parameter_value(mi, key, float(value))
        if abs(lib.get_material_instance_scalar_parameter_value(mi, key) - float(value)) > 1e-4:
            raise RuntimeError(f'{name}: scalar parameter {key}')
    for key, value in vectors.items():
        lib.set_material_instance_vector_parameter_value(mi, key, value)
        got = lib.get_material_instance_vector_parameter_value(mi, key)
        if abs(got.r - value.r) + abs(got.g - value.g) + abs(got.b - value.b) > 1e-4:
            raise RuntimeError(f'{name}: vector parameter {key}')
    lib.update_material_instance(mi)
    assets.save_loaded_asset(mi)
    return mi


def main():
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    kit_path = Path(os.environ.get('PYR_KIT_JSON') or project / 'Content/Look/Kit/kit.json')
    kit = json.loads(kit_path.read_text(encoding='utf-8'))
    master = author_master('M_AnimeKit', False)
    sway = author_master('M_AnimeKitSway', True)
    foliage = author_master('M_AnimeKitFoliage', False, foliage=True)
    count = 0
    for surface, spec in kit['surfaces'].items():
        tex = assets.load_asset(spec['texture'])
        if not tex:
            raise RuntimeError(f'{surface}: texture {spec["texture"]} is not imported')
        avg = spec['average_linear']
        parent = sway if spec.get('sway') else foliage if spec.get('foliage') else master
        instance(f'MI_Kit_{surface}', FOLDER, parent, {'Base': tex},
                 {'Tiling': spec['tiling_cm'], 'InkStrength': spec['ink_strength'], 'CelSteps': spec['cel_steps'],
                  'BlendSharpness': spec['blend_sharpness'], 'RimInk': spec.get('rim_ink', 0.0), 'Detail': spec.get('detail', 1.0),
                  'ShadeAmount': spec.get('shade_amount', 0.0)},
                 {'PaintAverage': unreal.LinearColor(avg[0], avg[1], avg[2], 1)})
        count += 1
        unreal.log(f'PYR_KIT MI_Kit_{surface}')
    # The generated props: their own pictures on their own UVs, through the kit.
    for prop in kit.get('props', []):
        material = assets.load_asset(f'/Game/Props/{prop}/M_{prop}_0')
        # The prop's material is an instance: its base-colour texture is the parameter named for colour (else T_<id>_0).
        base = None
        if isinstance(material, unreal.MaterialInstance):
            for p in material.get_editor_property('texture_parameter_values'):
                name = str(p.get_editor_property('parameter_info').get_editor_property('name')).lower()
                if any(w in name for w in ('base', 'color', 'colour', 'diffuse', 'albedo')) and p.get_editor_property('parameter_value'):
                    base = p.get_editor_property('parameter_value')
                    break
        base = base or assets.load_asset(f'/Game/Props/{prop}/T_{prop}_0')
        unreal.log(f'PYR_KIT prop {prop}: base {base.get_path_name() if base else None}')
        if not base:
            raise RuntimeError(f'prop {prop}: no base-colour texture on M_{prop}_0')
        instance(f'MI_KitProp_{prop}', f'{FOLDER}/Props', master, {'Base': base},
                 {'UseMeshUV': 1.0, 'InkFromAlpha': 0.0, 'CelSteps': kit.get('prop_cel_steps', 4)}, {})
        count += 1
        unreal.log(f'PYR_KIT MI_KitProp_{prop}')
    unreal.log(f'PYR_KIT_READY {count}')


main()
