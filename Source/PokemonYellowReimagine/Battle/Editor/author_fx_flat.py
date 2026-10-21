"""Author /Game/BattleFx/M_FxFlat, the flat anime effect material (bolts, rings, glows, speed lines).

Unlit, translucent and two-sided: vector parameter Color drives the emissive colour and Color.A the opacity, so
PYRFx::AddPrimitive(..., bFlat = true) pieces read as flat cel-style shapes with see-through glows.
Run under the build lock with:
    UnrealEditor-Cmd PokemonYellowReimagine.uproject -run=pythonscript
        -script=<this file> -unattended -nop4 -NullRHI
Prints PYR_FX_FLAT_READY on success.
"""
import unreal

FOLDER = '/Game/BattleFx'
NAME = 'M_FxFlat'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary

path = f'{FOLDER}/{NAME}'
if assets.does_asset_exist(path):
    mat = assets.load_asset(path)
    lib.delete_all_material_expressions(mat)
else:
    mat = tools.create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property('two_sided', True)

colour = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
colour.set_editor_property('parameter_name', 'Color')
colour.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
# The parameter's default output is RGB; its alpha comes from the separate 'A' output.
if not lib.connect_material_property(colour, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('emissive link failed')
if not lib.connect_material_property(colour, 'A', unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError('opacity link failed')
lib.recompile_material(mat)
assets.save_loaded_asset(mat)
unreal.log('PYR_FX_FLAT_READY')
