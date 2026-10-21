"""Author /Game/Encounters/MPC_Ring and M_RingDim (the ring's world dimming) in the editor.

Also authors M_Paint (vector parameter Color into base colour).
Run under the build lock with:
    UnrealEditor-Cmd PokemonYellowReimagine.uproject -run=pythonscript
        -script=<this file> -unattended -nop4 -NullRHI
M_RingDim is a post-process material (after tonemapping). Outside the circle MPC_Ring.Center/Radius the picture
is washed toward 40% colour with a warm paper tint, blended by MPC_Ring.Amount (0 none, 1 full); inside the
ring the colour is untouched, with a 1.5 m soft edge. Prints PYR_RING_DIM_READY on success.
"""
import unreal

FOLDER = '/Game/Encounters'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def fresh(name, cls, factory):
    """The asset, emptied if it already exists (so re-running rebuilds it in place)."""
    path = f'{FOLDER}/{name}'
    if assets.does_asset_exist(path):
        asset = assets.load_asset(path)
        if isinstance(asset, unreal.Material):
            lib.delete_all_material_expressions(asset)
        return asset
    return tools.create_asset(name, FOLDER, cls, factory)


mpc = fresh('MPC_Ring', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
scalars = []
for name, value in (('Radius', 1000.0), ('Amount', 0.0)):
    p = unreal.CollectionScalarParameter()
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('default_value', value)
    scalars.append(p)
center = unreal.CollectionVectorParameter()
center.set_editor_property('parameter_name', 'Center')
center.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0))
mpc.set_editor_property('scalar_parameters', scalars)
mpc.set_editor_property('vector_parameters', [center])
assets.save_loaded_asset(mpc)

mat = fresh('M_RingDim', unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
mat.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)


def node(cls, x, y, **props):
    expr = lib.create_material_expression(mat, cls, x, y)
    for key, value in props.items():
        expr.set_editor_property(key, value)
    return expr


def link(a, a_out, b, b_in):
    if not lib.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f'connect {a.get_name()}.{a_out} -> {b.get_name()}.{b_in} failed')


scene = node(unreal.MaterialExpressionSceneTexture, -1400, 0, scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
rgb = node(unreal.MaterialExpressionComponentMask, -1150, 0, r=True, g=True, b=True, a=False)
link(scene, 'Color', rgb, '')
weights = node(unreal.MaterialExpressionConstant3Vector, -1150, 150, constant=unreal.LinearColor(0.3, 0.59, 0.11, 0))
lum = node(unreal.MaterialExpressionDotProduct, -950, 100)
link(rgb, '', lum, 'A')
link(weights, '', lum, 'B')
keep = node(unreal.MaterialExpressionConstant, -950, 220, r=0.4)
washed = node(unreal.MaterialExpressionLinearInterpolate, -750, 100)
link(lum, '', washed, 'A')
link(rgb, '', washed, 'B')
link(keep, '', washed, 'Alpha')
paper = node(unreal.MaterialExpressionConstant3Vector, -750, 260, constant=unreal.LinearColor(0.91, 0.86, 0.75, 0))
paper_amount = node(unreal.MaterialExpressionConstant, -750, 380, r=0.18)
painted = node(unreal.MaterialExpressionLinearInterpolate, -550, 150)
link(washed, '', painted, 'A')
link(paper, '', painted, 'B')
link(paper_amount, '', painted, 'Alpha')

world = node(unreal.MaterialExpressionWorldPosition, -1400, 500)
world_xy = node(unreal.MaterialExpressionComponentMask, -1150, 500, r=True, g=True, b=False, a=False)
link(world, '', world_xy, '')
center_param = node(unreal.MaterialExpressionCollectionParameter, -1400, 650, collection=mpc, parameter_name='Center')
center_xy = node(unreal.MaterialExpressionComponentMask, -1150, 650, r=True, g=True, b=False, a=False)
link(center_param, '', center_xy, '')
dist = node(unreal.MaterialExpressionDistance, -950, 550)
link(world_xy, '', dist, 'A')
link(center_xy, '', dist, 'B')
radius = node(unreal.MaterialExpressionCollectionParameter, -950, 700, collection=mpc, parameter_name='Radius')
beyond = node(unreal.MaterialExpressionSubtract, -750, 550)
link(dist, '', beyond, 'A')
link(radius, '', beyond, 'B')
soft = node(unreal.MaterialExpressionConstant, -750, 700, r=150.0)
ramp = node(unreal.MaterialExpressionDivide, -550, 550)
link(beyond, '', ramp, 'A')
link(soft, '', ramp, 'B')
mask = node(unreal.MaterialExpressionSaturate, -400, 550)
link(ramp, '', mask, '')
amount = node(unreal.MaterialExpressionCollectionParameter, -400, 700, collection=mpc, parameter_name='Amount')
strength = node(unreal.MaterialExpressionMultiply, -250, 550)
link(mask, '', strength, 'A')
link(amount, '', strength, 'B')

final = node(unreal.MaterialExpressionLinearInterpolate, -150, 150)
link(rgb, '', final, 'A')
link(painted, '', final, 'B')
link(strength, '', final, 'Alpha')
if not lib.connect_material_property(final, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('connect to emissive failed')
lib.recompile_material(mat)
assets.save_loaded_asset(mat)
unreal.log('PYR_RING_DIM_READY')

# M_Paint: a flat painted colour (vector parameter Color) for the ring rim, test props and stand-in bodies.
paint = fresh('M_Paint', unreal.Material, unreal.MaterialFactoryNew())
# The rim and grass are instanced; skeletal stand-ins may use it too. Without these flags a cooked game draws
# the default material instead.
paint.set_editor_property('used_with_instanced_static_meshes', True)
paint.set_editor_property('used_with_skeletal_mesh', True)
colour = lib.create_material_expression(paint, unreal.MaterialExpressionVectorParameter, -400, 0)
colour.set_editor_property('parameter_name', 'Color')
colour.set_editor_property('default_value', unreal.LinearColor(0.8, 0.8, 0.8, 1))
rough = lib.create_material_expression(paint, unreal.MaterialExpressionConstant, -400, 200)
rough.set_editor_property('r', 0.9)
lib.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
lib.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
lib.recompile_material(paint)
assets.save_loaded_asset(paint)
unreal.log('PYR_PAINT_READY')
