"""Author /Game/Look/M_PaintTex for the m2 art pass (polish worker).

Run under the build lock with:
    UnrealEditor-Cmd PokemonYellowReimagine.uproject -run=pythonscript
        -script=<this file> -unattended -nop4 -NullRHI

M_PaintTex is M_Flat with painted detail: a texture (parameter Tex) projected in world space every Tile cm from the
three axes (so any box tiles without UVs), divided by the texture's own average colour (its smallest mip) and clamped,
then multiplied into Color. The palette colour of every primitive stays what it was; the texture only adds the brush
variation. Strength (0..1) fades the detail. Fully rough, no specular, usable on instanced meshes.
Prints PYR_PAINT_TEX_READY on success.
"""
import unreal

FOLDER = '/Game/Look'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary

CODE = r'''
float3 n = abs(normalize(Normal));
float3 w = pow(n, 4.0);
w /= max(w.x + w.y + w.z, 0.0001);
float2 uz = World.xy / Tile;
float2 ux = World.yz / Tile;
float2 uy = World.xz / Tile;
float3 c = Texture2DSample(Tex, TexSampler, uz).rgb * w.z + Texture2DSample(Tex, TexSampler, ux).rgb * w.x + Texture2DSample(Tex, TexSampler, uy).rgb * w.y;
float3 avg = Texture2DSampleLevel(Tex, TexSampler, float2(0.5, 0.5), 14.0).rgb;
float3 detail = clamp(c / max(avg, 0.03), 0.55, 1.5);
return Color * lerp(float3(1, 1, 1), detail, Strength);
'''

path = f'{FOLDER}/M_PaintTex'
if assets.does_asset_exist(path):
    mat = assets.load_asset(path)
    lib.delete_all_material_expressions(mat)
else:
    mat = tools.create_asset('M_PaintTex', FOLDER, unreal.Material, unreal.MaterialFactoryNew())
for flag in ('used_with_instanced_static_meshes', 'used_with_skeletal_mesh', 'used_with_static_lighting'):
    mat.set_editor_property(flag, True)


def node(cls, x, y, **props):
    e = lib.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    if not lib.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f'connect {a.get_name()}.{a_out} -> {b.get_name()}.{b_in} failed')


custom = node(unreal.MaterialExpressionCustom, -300, 0, code=CODE, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              description='PaintTex')
ins = []
for name in ('Tex', 'World', 'Normal', 'Tile', 'Color', 'Strength'):
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', name)
    ins.append(ci)
custom.set_editor_property('inputs', ins)
white = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
tex = node(unreal.MaterialExpressionTextureObjectParameter, -800, -200, parameter_name='Tex')
if white:
    tex.set_editor_property('texture', white)
link(tex, '', custom, 'Tex')
link(node(unreal.MaterialExpressionWorldPosition, -800, 0), '', custom, 'World')
link(node(unreal.MaterialExpressionVertexNormalWS, -800, 100), '', custom, 'Normal')
link(node(unreal.MaterialExpressionScalarParameter, -800, 200, parameter_name='Tile', default_value=400.0), '', custom, 'Tile')
link(node(unreal.MaterialExpressionVectorParameter, -800, 300, parameter_name='Color', default_value=unreal.LinearColor(0.8, 0.8, 0.8, 1)), '', custom, 'Color')
link(node(unreal.MaterialExpressionScalarParameter, -800, 500, parameter_name='Strength', default_value=1.0), '', custom, 'Strength')
if not lib.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR):
    raise RuntimeError('base colour')
lib.connect_material_property(node(unreal.MaterialExpressionConstant, -300, 300, r=1.0), '', unreal.MaterialProperty.MP_ROUGHNESS)
lib.connect_material_property(node(unreal.MaterialExpressionConstant, -300, 400, r=0.0), '', unreal.MaterialProperty.MP_SPECULAR)
lib.recompile_material(mat)
assets.save_loaded_asset(mat)
unreal.log('PYR_PAINT_TEX_READY')
