"""Build every asset the demo needs, headless:

  UnrealEditor-Cmd.exe PixelArt3D.uproject -run=pythonscript -script="<abs path>/Tools/build_assets.py"

Creates under /Game/PixelArt: T_Atlas, MPC_Pixel, M_PixelSurface, M_PixelPost; and /Game/Maps/Workshop.
The HLSL lives in this file so the material logic is reviewable as text.
"""
import os
import unreal

ROOT = "/Game/PixelArt"
HERE = os.path.dirname(os.path.abspath(__file__))
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(*a):
    unreal.log("[build_assets] " + " ".join(str(x) for x in a))


def fresh(name, cls, factory, path=ROOT):
    """Reuse the asset if it exists (keeps references valid), otherwise create it."""
    full = f"{path}/{name}"
    asset = unreal.load_asset(full) if EAL.does_asset_exist(full) else None
    if asset is None:
        asset = AT.create_asset(name, path, cls, factory)
    if isinstance(asset, unreal.Material):
        MEL.delete_all_material_expressions(asset)
    return asset


# ---------------------------------------------------------------------------------------------
# texture
# ---------------------------------------------------------------------------------------------
def import_atlas():
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(HERE, "SourceArt", "T_Atlas.png"))
    task.set_editor_property("destination_path", ROOT)
    task.set_editor_property("destination_name", "T_Atlas")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    AT.import_asset_tasks([task])
    tex = unreal.load_asset(f"{ROOT}/T_Atlas")
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
    tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)
    return tex


# ---------------------------------------------------------------------------------------------
# parameter collection
# ---------------------------------------------------------------------------------------------
MPC_VECTORS = {
    "CamForward": (1, 0, 0, 0), "CamRight": (0, 1, 0, 0), "CamUp": (0, 0, 1, 0), "CamPos": (0, 0, 0, 6.25),
    "LightDir": (0.5, 0.3, -0.8, 0), "LightRight": (0, 1, 0, 0), "LightUp": (0, 0, 1, 0), "LightOrigin": (0, 0, 0, 4800),
    "Misc0": (3, 0, 1, 1),  # x toon steps, y view mode, z true-light, w shadows
    "Misc1": (0, 6.25, 6, 0),  # x dither mode, y texel cm, z shadow bias, w time
    "Misc2": (1, 1, 0, 0),  # x outline mode, y lut cleanup, z camera depth
}


def build_mpc():
    mpc = fresh("MPC_Pixel", unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    vecs = []
    for name, v in MPC_VECTORS.items():
        p = unreal.CollectionVectorParameter()
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        vecs.append(p)
    mpc.set_editor_property("vector_parameters", vecs)
    EAL.save_loaded_asset(mpc)
    return mpc


# ---------------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------------
class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x=-1400, **props):
        e = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 90
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def custom(self, code, inputs, out_type, desc, x=-300):
        c = self.node(unreal.MaterialExpressionCustom, x)
        c.set_editor_property("code", code)
        c.set_editor_property("description", desc)
        c.set_editor_property("output_type", out_type)
        ins = []
        for name, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for name, (src, out) in inputs:
            MEL.connect_material_expressions(src, out, c, name)
        return c


WPO_CODE = r"""
// Sort relocation (report ch.04/09): move the vertex along the view so the sprite's pivot lands on
// its slot depth and the sprite's own depth is compressed by Keep. Only for the main pixel camera:
// the shadow capture looks along another direction and keeps true positions.
float3 o = float3(0, 0, 0);
float3 vf = ResolvedView.ViewForward;
if (Enable > 0.5 && dot(vf, CF.xyz) > 0.999)
{
    float s = dot(P, CF.xyz);
    float s2 = Slot + (s - Pivot) * Keep;
    o = CF.xyz * (s2 - s);
}
return o;
"""

SURFACE_CODE = r"""
float3 n = normalize(N);

// --- albedo: world-aligned 16 texel / metre atlas, nearest ---------------------------------
float3 albedo = Tint.rgb;
if (UseTex > 1.5)
{
    // imported mesh: palette-strip texture through the mesh UVs, nearest
    albedo = MTex.SampleLevel(MTexSampler, UV, 0).rgb * Tint.rgb;
}
else if (UseTex > 0.5)
{
    float3 an = abs(n);
    float2 w; float tile = TileSide;
    if (an.z >= an.x && an.z >= an.y) { w = float2(Pt.x, -Pt.y); tile = n.z > 0 ? Tile : TileSide; }
    else if (an.x >= an.y) { w = float2(Pt.y * sign(n.x), -Pt.z); }
    else { w = float2(-Pt.x * sign(n.y), -Pt.z); }
    float2 t = clamp(floor(frac(w / 100.0 * Density) * 16.0), 0.0, 15.0); // frac() can round to 1.0
    float2 uv = float2((tile * 16.0 + t.x + 0.5) / 256.0, (t.y + 0.5) / 16.0);
    albedo = Tex.SampleLevel(TexSampler, uv, 0).rgb * Tint.rgb;
}

// --- light: true position (or the pushed one, to show what UE's deferred path would do) -----
float3 P = (M0.z > 0.5) ? Pt : Pp;
float ndl = saturate(dot(n, -LDir.xyz) * 1.2);
float lit = 1.0;
float dbgDelta = 0.0;
if (M0.w > 0.5)
{
    // normal-offset + slope-scaled bias: nearest-sampled depth maps alias into stripes at grazing angles
    float3 d = P + n * 5.0 - LOrigin.xyz;
    float ext = LOrigin.w;
    float2 suv = float2(dot(d, LRight.xyz) / ext + 0.5, 0.5 - dot(d, LUp.xyz) / ext);
    if (all(suv > 0.002) && all(suv < 0.998))
    {
        float stored = Shadow.SampleLevel(ShadowSampler, suv, 0).r;
        float depth = dot(d, LDir.xyz);
        float ndlRaw = max(dot(n, -LDir.xyz), 0.05);
        float bias = M1.z + 6.0 * min(sqrt(1.0 - ndlRaw * ndlRaw) / ndlRaw, 6.0);
        lit = (depth - bias > stored) ? 0.0 : 1.0;
        dbgDelta = depth - stored;
    }
}
// --- ordered dither of the toon band boundaries (screen- or object-anchored Bayer 4x4) -------
float th = 0.35;
if (M1.x > 0.5)
{
    float2 cell;
    if (M1.x < 1.5) cell = floor(Parameters.SvPosition.xy);
    else
    {
        // anchored to the sprite pivot in texel units: the pattern travels with the object
        float3 rel = Pt - float3(C5, C6, C7);
        cell = floor(float2(dot(rel, CR.xyz), -dot(rel, CU.xyz)) / max(M1.y, 0.01));
    }
    int2 ci = int2(cell - 4.0 * floor(cell / 4.0));
    const float bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    // only a narrow band around each step edge gets mixed, the flat areas stay solid
    th = 0.35 + ((bayer[ci.y * 4 + ci.x] + 0.5) / 16.0 - 0.5) * 0.6;
}
float q = min(ndl, lit);
if (M0.x > 0.5) q = saturate(floor(q * M0.x + th) / M0.x);
float3 shade = lerp(float3(0.40, 0.46, 0.66), float3(1.05, 1.01, 0.93), q);
float3 c = albedo * shade;

// --- debug views -------------------------------------------------------------------------------
int vm = (int)(M0.y + 0.5);
if (vm == 1) { c = (C4 < 1.5) ? float3(0.25, 0.28, 0.25) : frac(sin(float3(C4 * 12.9898, C4 * 78.233, C4 * 37.719)) * 43758.5453) * 0.75 + 0.2; }
else if (vm == 2) { c = (Enable < 0.5) ? float3(0.2, 0.2, 0.22) : lerp(float3(0.15, 0.35, 0.9), float3(1.0, 0.55, 0.12), saturate(C8 / 8.0)); }
else if (vm == 3) { c = (1.0 - saturate((dot(Pt, CF.xyz) - M2.z - 2900.0) / 5200.0)).xxx; }
else if (vm == 4) { c = (1.0 - saturate((dot(Pp, CF.xyz) - M2.z - 2900.0) / 5200.0)).xxx; }
else if (vm == 5) { c = shade * 0.85; }
else if (vm == 6) { c = float3(saturate(dbgDelta / 40.0 + 0.5), saturate(0.5 - dbgDelta / 40.0), lit * 0.3); }
return c;
"""

POST_CODE = r"""
// Runs at the internal (pixel) resolution, replacing the tonemapper. 1-texel outlines from the
// object id (custom stencil) and depth, with ownership rules, then an optional 3x3 cleanup pass.
float2 uv = GetDefaultSceneTextureUV(Parameters, 14);
float2 ts = View.BufferSizeAndInvSize.zw;
float3 col = SceneTextureLookup(uv, 14, false).rgb / max(View.PreExposure, 1e-4);
int mode = (int)(M2.x + 0.5);

float ids[25];
float dep[25];
for (int y = -2; y <= 2; y++)
{
    for (int x = -2; x <= 2; x++)
    {
        int k = (y + 2) * 5 + (x + 2);
        float2 u = uv + float2(x, y) * ts;
        ids[k] = floor(SceneTextureLookup(u, 25, false).r + 0.5);
        dep[k] = SceneTextureLookup(u, 1, false).r;
    }
}

bool E[9];
for (int cy = -1; cy <= 1; cy++)
{
    for (int cx = -1; cx <= 1; cx++)
    {
        int c = (cy + 2) * 5 + (cx + 2);
        bool e = false;
        for (int i = 0; i < 4; i++)
        {
            int q = c + (i == 0 ? 1 : (i == 1 ? -1 : (i == 2 ? 5 : -5)));
            float ic = ids[c], iq = ids[q], dc = dep[c], dq = dep[q];
            if (mode == 1)
            {
                if (iq != ic && ic > 1.5 && dq > dc + 1.0) e = true;           // object edge: line owned by the nearer object
                if (iq == ic && ic > 0.5 && ic < 1.5 && dq - dc > 45.0) e = true; // crease inside terrain
            }
            else if (mode == 2)
            {
                if (iq != ic && dq + 1.0 < dc) e = true;                         // line drawn on the farther side
                if (iq == ic && ic > 0.5 && ic < 1.5 && dc - dq > 45.0) e = true;
            }
            else if (mode == 3)
            {
                if (iq != ic) e = true;                                           // both sides: 2-texel seams
                if (iq == ic && ic > 0.5 && ic < 1.5 && abs(dq - dc) > 45.0) e = true;
            }
        }
        E[(cy + 1) * 3 + (cx + 1)] = e;
    }
}

bool edge = E[4];
if (edge && M2.y > 0.5)
{
    int n8 = 0;
    for (int i = 0; i < 9; i++) { if (i != 4 && E[i]) n8++; }
    bool l = E[3], r = E[5], u = E[1], d = E[7];
    if (n8 == 0) edge = false;                                               // orphan texel
    if ((r && u && !E[2] && !l && !d) || (r && d && !E[8] && !l && !u) ||
        (l && u && !E[0] && !r && !d) || (l && d && !E[6] && !r && !u)) edge = false; // L-corner "double"
}
if (edge) col *= float3(0.30, 0.32, 0.40);
return pow(saturate(col), 1.0 / 2.2);
"""


def build_surface(mpc, atlas):
    mat = fresh("M_PixelSurface", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    for flag in ("used_with_skeletal_mesh", "used_with_instanced_static_meshes", "used_with_static_lighting"):
        try:
            mat.set_editor_property(flag, True)
        except Exception as e:  # noqa
            log("flag", flag, e)
    g = Graph(mat)

    def coll(name):
        return g.node(unreal.MaterialExpressionCollectionParameter, collection=mpc, parameter_name=name)

    def cpd(i):
        return g.node(unreal.MaterialExpressionScalarParameter, parameter_name=f"CPD{i}", default_value=0.0,
                      use_custom_primitive_data=True, primitive_data_index=i)

    def scalar(name, v):
        return g.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    p_true = g.node(unreal.MaterialExpressionWorldPosition,
                    world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    p_pushed = g.node(unreal.MaterialExpressionWorldPosition)
    normal = g.node(unreal.MaterialExpressionVertexNormalWS)
    cf, cr, cu = coll("CamForward"), coll("CamRight"), coll("CamUp")
    ldir, lright, lup, lorig = coll("LightDir"), coll("LightRight"), coll("LightUp"), coll("LightOrigin")
    m0, m1, m2 = coll("Misc0"), coll("Misc1"), coll("Misc2")
    c = [cpd(i) for i in range(9)]
    tile, tile_side, use_tex, density = scalar("Tile", 0), scalar("TileSide", 0), scalar("UseTexture", 1), scalar("Density", 1)
    tint = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    tex = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Atlas", texture=atlas)
    shadow = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="ShadowDepth", texture=atlas)
    mtex = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="MeshTex", texture=atlas)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)

    F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    wpo = g.custom(WPO_CODE, [
        ("P", (p_true, "")), ("CF", (cf, "")), ("Slot", (c[0], "")), ("Pivot", (c[1], "")), ("Keep", (c[2], "")), ("Enable", (c[3], "")),
    ], F3, "SortRelocation")
    surf = g.custom(SURFACE_CODE, [
        ("Pt", (p_true, "")), ("Pp", (p_pushed, "")), ("N", (normal, "")),
        ("Tex", (tex, "")), ("Tile", (tile, "")), ("TileSide", (tile_side, "")), ("UseTex", (use_tex, "")), ("Density", (density, "")), ("Tint", (tint, "")),
        ("MTex", (mtex, "")), ("UV", (uv, "")),
        ("LDir", (ldir, "")), ("LRight", (lright, "")), ("LUp", (lup, "")), ("LOrigin", (lorig, "")), ("Shadow", (shadow, "")),
        ("M0", (m0, "")), ("M1", (m1, "")), ("M2", (m2, "")), ("CF", (cf, "")), ("CR", (cr, "")), ("CU", (cu, "")),
        ("Enable", (c[3], "")), ("C4", (c[4], "")), ("C5", (c[5], "")), ("C6", (c[6], "")), ("C7", (c[7], "")), ("C8", (c[8], "")),
    ], F3, "PixelSurface")
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.connect_material_property(surf, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_post(mpc):
    mat = fresh("M_PixelPost", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    loc = None
    for name in ("BL_REPLACING_TONEMAPPER", "BL_ReplacingTonemapper", "REPLACING_TONEMAPPER"):
        loc = getattr(unreal.BlendableLocation, name, None)
        if loc is not None:
            break
    log("blendable location", loc)
    mat.set_editor_property("blendable_location", loc)
    g = Graph(mat)
    st = []
    for sid in ("PPI_POST_PROCESS_INPUT0", "PPI_SCENE_DEPTH", "PPI_CUSTOM_STENCIL"):
        st.append(g.node(unreal.MaterialExpressionSceneTexture, scene_texture_id=getattr(unreal.SceneTextureId, sid)))
    m2 = g.node(unreal.MaterialExpressionCollectionParameter, collection=mpc, parameter_name="Misc2")
    post = g.custom(POST_CODE, [
        ("M2", (m2, "")), ("Dummy0", (st[0], "Color")), ("Dummy1", (st[1], "Color")), ("Dummy2", (st[2], "Color")),
    ], unreal.CustomMaterialOutputType.CMOT_FLOAT3, "PixelOutline")
    MEL.connect_material_property(post, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def build_map():
    path = "/Game/Maps/Workshop"
    if EAL.does_asset_exist(path):
        log("map exists")
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    log("map saved", ok)


def main():
    atlas = import_atlas()
    mpc = build_mpc()
    build_surface(mpc, atlas)
    build_post(mpc)
    build_map()
    log("done")


main()
