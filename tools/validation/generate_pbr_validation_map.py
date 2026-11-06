#!/usr/bin/env python3
"""Build the original openQ4 PBR laboratory below .tmp, never in shipped content.

This is real map geometry and independently authored materials, not a global
material override. All textures, cube faces and sphere geometry are generated
here. Retail PK4s supply only the game, player and engine's standard entities.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

import generate_pbr_fixture as fixture

ROOT = fixture.ROOT
MAP = "maps/tools/openq4_pbr_lab"
PREFIX = "textures/openq4/pbr_lab"
MODEL = "models/openq4/pbr_lab/sphere.ase"
SAMPLING_MODEL = "models/openq4/pbr_lab/sphere_tiled.ase"
BACKFACE_MODEL = "models/openq4/pbr_lab/backface_receiver.ase"
CONSTANT_NORMAL_MODEL = "models/openq4/pbr_lab/sphere_constant_normal.ase"
VERTEX_COLOR_MODEL = "models/openq4/pbr_lab/sphere_vertex_color.ase"
SKINNING_MODEL = "openq4_pbr_lab_skinned"
SKINNING_MESH = "models/openq4/pbr_lab/skinned.md5mesh"
# REN-05 skin substitutions on the skinned specimen (CPU and packed MD5R): its
# authored PBR metal becomes another PBR material, or a classic one.
# REN-05 mixed-material model: the skinned sphere split into two meshes, PBR
# metal above its equator and a classic material below, as a mod might author.
MIXED_MODEL = "openq4_pbr_lab_mixed"
MIXED_MESH = "models/openq4/pbr_lab/mixed.md5mesh"
MIXED_SHADERS = (f"{PREFIX}/metal_3", f"{PREFIX}/classic_specular")
SKINNING_SKINS = {"dielectric": "openq4_pbr_lab/skinned_dielectric", "classic": "openq4_pbr_lab/skinned_classic"}


def rgba_tga(size: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    assert len(pixels) == size * size
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, size, size, 32, 0x28)
    return header + bytes(channel for r, g, b, a in pixels for channel in (b, g, r, a))


def sphere_ase(segments: int = 48, rings: int = 24, radius: float = 72, uv_scale: float = 1, uv_offset: float = 0, constant_normal: bool = False, vertex_color: tuple[int, int, int] | None = None) -> str:
    vertices, uv, triangles = [], [], []
    for j in range(rings + 1):
        theta = math.pi * j / rings
        for i in range(segments + 1):
            phi = 2 * math.pi * i / segments
            vertices.append((radius * math.sin(theta) * math.cos(phi), radius * math.sin(theta) * math.sin(phi), radius * math.cos(theta)))
            uv.append((i / segments * uv_scale + uv_offset, j / rings * uv_scale + uv_offset))
    for j in range(rings):
        for i in range(segments):
            a = j * (segments + 1) + i
            b, c, d = a + 1, a + segments + 1, a + segments + 2
            if j > 0:
                triangles.append((a, c, b))
            if j < rings - 1:
                triangles.append((b, c, d))
    # ASE winding is converted by the engine; include smooth per-corner normals
    # so highlights exercise the tangent basis rather than a faceted proxy.
    out = ['*3DSMAX_ASCIIEXPORT 200', '*MATERIAL_LIST {', '*MATERIAL_COUNT 1', '*MATERIAL 0 {', '*MAP_DIFFUSE {', f'*BITMAP "//openq4/baseoq4/{PREFIX}/neutral"', '*UVW_U_TILING 1', '*UVW_V_TILING 1', '}', '}', '}', '*GEOMOBJECT {', '*NODE_NAME "PBR_LAB_SPHERE"', '*NODE_TM {', '*TM_ROW0 1 0 0', '*TM_ROW1 0 1 0', '*TM_ROW2 0 0 1', '*TM_ROW3 0 0 0', '}', '*MESH {', f'*MESH_NUMVERTEX {len(vertices)}', f'*MESH_NUMFACES {len(triangles)}', '*MESH_VERTEX_LIST {']
    out += [f'*MESH_VERTEX {i} {x:.8f} {y:.8f} {z:.8f}' for i, (x, y, z) in enumerate(vertices)]
    out += ['}', '*MESH_FACE_LIST {']
    out += [f'*MESH_FACE {i}: A: {a} B: {b} C: {c} AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0' for i, (a, b, c) in enumerate(triangles)]
    out += ['}', f'*MESH_NUMTVERTEX {len(uv)}', '*MESH_TVERTLIST {']
    out += [f'*MESH_TVERT {i} {u:.8f} {v:.8f} 0' for i, (u, v) in enumerate(uv)]
    out += ['}', f'*MESH_NUMTVFACES {len(triangles)}', '*MESH_TFACELIST {']
    out += [f'*MESH_TFACE {i} {a} {b} {c}' for i, (a, b, c) in enumerate(triangles)]
    out += ['}']
    if vertex_color is not None:
        # ASE truncates float * 255 to a byte. Stay inside each byte's bin so
        # decimal-to-float rounding cannot change the intended (64,128,192).
        out += ['*MESH_NUMCVERTEX 1', '*MESH_CVERTLIST {',
                '*MESH_VERTCOL 0 ' + ' '.join(f'{(v + .25) / 255:.9f}' for v in vertex_color),
                '}', f'*MESH_NUMCVFACES {len(triangles)}', '*MESH_CFACELIST {']
        out += [f'*MESH_CFACE {i} 0 0 0' for i in range(len(triangles))]
        out += ['}']
    out += ['*MESH_NORMALS {']
    for i, indices in enumerate(triangles):
        center = [sum(vertices[k][axis] for k in indices) for axis in range(3)]
        length = math.sqrt(sum(x*x for x in center))
        out.append(f'*MESH_FACENORMAL {i} ' + ' '.join(f'{x/length:.8f}' for x in center))
        out += [f'*MESH_VERTEXNORMAL {k} ' + ('0 -1 0' if constant_normal else ' '.join(f'{x/radius:.8f}' for x in vertices[k])) for k in indices]
    return '\n'.join(out + ['}', '}', '*MATERIAL_REF 0', '}', ''])


def backface_plane_ase() -> str:
    """Original plane facing -Y; a light at positive local Y rejects both classic faces."""
    return """*3DSMAX_ASCIIEXPORT 200
*MATERIAL_LIST {
*MATERIAL_COUNT 1
*MATERIAL 0 {
*MAP_DIFFUSE {
*BITMAP "//openq4/baseoq4/textures/openq4/pbr_lab/neutral"
*UVW_U_TILING 1
*UVW_V_TILING 1
}
}
}
*GEOMOBJECT {
*NODE_NAME "PBR_BACKFACE_RECEIVER"
*NODE_TM {
*TM_ROW0 1 0 0
*TM_ROW1 0 1 0
*TM_ROW2 0 0 1
*TM_ROW3 0 0 0
}
*MESH {
*MESH_NUMVERTEX 4
*MESH_NUMFACES 2
*MESH_VERTEX_LIST {
*MESH_VERTEX 0 -72 0 -72
*MESH_VERTEX 1 72 0 -72
*MESH_VERTEX 2 72 0 72
*MESH_VERTEX 3 -72 0 72
}
*MESH_FACE_LIST {
*MESH_FACE 0: A: 0 B: 1 C: 2 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0
*MESH_FACE 1: A: 0 B: 2 C: 3 AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0
}
*MESH_NUMTVERTEX 4
*MESH_TVERTLIST {
*MESH_TVERT 0 0 0 0
*MESH_TVERT 1 1 0 0
*MESH_TVERT 2 1 1 0
*MESH_TVERT 3 0 1 0
}
*MESH_NUMTVFACES 2
*MESH_TFACELIST {
*MESH_TFACE 0 0 1 2
*MESH_TFACE 1 0 2 3
}
*MESH_NORMALS {
*MESH_FACENORMAL 0 0 -1 0
*MESH_VERTEXNORMAL 0 0 -1 0
*MESH_VERTEXNORMAL 1 0 -1 0
*MESH_VERTEXNORMAL 2 0 -1 0
*MESH_FACENORMAL 1 0 -1 0
*MESH_VERTEXNORMAL 0 0 -1 0
*MESH_VERTEXNORMAL 2 0 -1 0
*MESH_VERTEXNORMAL 3 0 -1 0
}
}
*MATERIAL_REF 0
}
"""


def brush(lo: tuple[int, int, int], hi: tuple[int, int, int], material: str) -> str:
    planes = [(1, 0, 0, -hi[0]), (-1, 0, 0, lo[0]), (0, 1, 0, -hi[1]), (0, -1, 0, lo[1]), (0, 0, 1, -hi[2]), (0, 0, -1, lo[2])]
    return '{\nbrushDef3\n{\n' + '\n'.join(f'( {a} {b} {c} {d} ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) "{material}"' for a, b, c, d in planes) + '\n}\n}\n'


def skinned_sphere(shaders: tuple[str, ...] = (f'{PREFIX}/metal_3',)) -> tuple[str,str,str]:
    """Original two-joint MD5 specimen with a smooth, exact two-weight bend.

    With two shaders the rings split at the equator into two meshes that share
    the vertices: the first shader above, the second below.
    """
    segments,rings,radius=32,16,72
    vertices,weights,triangles=[],[],[]
    for j in range(rings+1):
        theta=math.pi*j/rings
        for i in range(segments+1):
            phi=2*math.pi*i/segments
            x,y,z=radius*math.sin(theta)*math.cos(phi),radius*math.sin(theta)*math.sin(phi),radius*math.cos(theta)
            blend=min(1,max(0,(z+24)/72))
            first=len(weights)
            for joint,weight,offset_z in ((0,1-blend,z),(1,blend,z-36)):
                if weight>0: weights.append((joint,weight,x,y,offset_z))
            vertices.append((i/segments,1-j/rings,first,len(weights)-first))
    for j in range(rings):
        for i in range(segments):
            a=j*(segments+1)+i
            b,c,d=a+1,a+segments+1,a+segments+2
            # MD5 faces use the engine's clockwise convention directly.
            if j>0: triangles.append((a,b,c))
            if j<rings-1: triangles.append((b,d,c))
    mesh=['MD5Version 10','commandline "original openQ4 PBR laboratory"','numJoints 2',f'numMeshes {len(shaders)}',
          'joints {','"origin" -1 ( 0 0 0 ) ( 0 0 0 )','"bend" 0 ( 0 0 36 ) ( 0 0 0 )','}']
    split=(rings//2)*segments*2-segments  # triangles of the upper rings (the first ring has one per quad)
    parts=[triangles] if len(shaders)==1 else [triangles[:split], triangles[split:]]
    for shader,part in zip(shaders,parts):
        mesh += ['mesh {',f'shader "{shader}"',f'numverts {len(vertices)}']
        mesh += [f'vert {i} ( {u:.9g} {v:.9g} ) {first} {count}' for i,(u,v,first,count) in enumerate(vertices)]
        mesh += [f'numtris {len(part)}']+[f'tri {i} {a} {b} {c}' for i,(a,b,c) in enumerate(part)]
        mesh += [f'numweights {len(weights)}']+[f'weight {i} {joint} {weight:.9g} ( {x:.9g} {y:.9g} {z:.9g} )' for i,(joint,weight,x,y,z) in enumerate(weights)]
        mesh += ['}','']
    anim='''MD5Version 10
commandline "original openQ4 PBR laboratory"
numFrames 2
numJoints 2
frameRate 24
numAnimatedComponents 0
hierarchy {
"origin" -1 0 0
"bend" 0 0 0
}
bounds {
( -72 -72 -72 ) ( 72 72 72 )
( -72 -72 -72 ) ( 72 72 72 )
}
baseframe {
( 0 0 0 ) ( 0 0 0 )
( 0 0 36 ) ( 0 0 0 )
}
frame 0 { }
frame 1 { }
'''
    model=f'model {SKINNING_MODEL} {{\nmesh {SKINNING_MESH}\nanim idle models/openq4/pbr_lab/idle.md5anim\n}}\n'
    if len(shaders)>1:
        model=f'model {MIXED_MODEL} {{\nmesh {MIXED_MESH}\nanim idle models/openq4/pbr_lab/idle.md5anim\n}}\n'
    return '\n'.join(mesh),anim,model


def entity(**args: str) -> str:
    return '{\n' + '\n'.join(f'"{k}" "{v}"' for k, v in args.items()) + '\n}\n'


def material(name: str, *, albedo: str = 'grey', metallic: float = 0, roughness: float = 0.5, ao: float = 1, layout: str = 'scalar', normal: str = '', normal_scale: float = 1, emissive: float = 0, coverage: str = '', no_self_shadow: bool = False, albedo_options: str = '') -> str:
    color = f'{PREFIX}/{albedo}'
    lines = [f'{PREFIX}/{name}', '{']
    if no_self_shadow:
        lines += ['noSelfShadow']
    if coverage == 'blend':
        lines += ['translucent', 'noShadows']
    lines += ['bumpmap _flat']
    if coverage == 'cutout':
        lines += ['{', 'blend diffusemap', f'map {color}', 'alphaTest 0.5', '}']
    else:
        lines += [f'diffusemap {color}']
    lines += ['specularmap _black']
    if coverage == 'blend':
        lines += ['{', 'blend blend', f'map {color}', '}']
    if emissive:
        lines += ['{', 'blend add', f'map {color}', f'rgb {emissive:g}', '}']
    lines += ['pbr {', 'workflow metallicRoughness', f'albedoMap {albedo_options} {color}']
    if normal:
        lines += [f'normalMap {PREFIX}/normal', f'normalFormat {normal}', f'normalScale {normal_scale:g}']
    if layout == 'packed':
        lines += [f'ormMap {PREFIX}/orm']
    elif layout == 'separate':
        lines += [f'metallicMap {PREFIX}/metallic', f'roughnessMap {PREFIX}/roughness', f'aoMap {PREFIX}/ao']
    lines += [f'metallic {metallic:.9g}', f'roughness {roughness:.9g}', f'ao {ao:.9g}']
    if emissive:
        lines += [f'emissiveMap {color}', f'emissiveColor {emissive:g}, {emissive:g}, {emissive:g}']
    return '\n'.join(lines + ['}', '}', ''])


def payloads() -> tuple[dict[str, bytes], list[dict]]:
    data: dict[str, bytes] = {}
    def add(qpath: str, value: str | bytes) -> None:
        data['baseoq4/' + qpath] = value.encode('utf-8') if isinstance(value, str) else value
    size = 64
    for name, rgb in {'grey': (188,188,188), 'copper': (245,160,105), 'dark': (65,65,65), 'cyan': (30,200,255), 'orm': (192,128,102), 'metallic': (102,102,102), 'roughness': (128,128,128), 'ao': (192,192,192)}.items():
        add(f'{PREFIX}/{name}.tga', fixture.tga_bytes(size, size, [rgb]*(size*size)))
    pixels = []
    for y in range(size):
        for x in range(size):
            nx = 0.35*math.sin(2*math.pi*x/16)
            ny = 0.35*math.sin(2*math.pi*y/16)
            pixels.append((round(127.5*(1+nx)), round(127.5*(1+ny)), round(127.5*(1+math.sqrt(1-nx*nx-ny*ny)))))
    add(f'{PREFIX}/normal.tga', fixture.tga_bytes(size, size, pixels))
    add(f'{PREFIX}/faint.tga', fixture.tga_bytes(size,size,[(1,30,200)]*(size*size)))
    add(f'{PREFIX}/cutout.tga', rgba_tga(size, [(188,188,188,255 if (x//8+y//8)%2 else 0) for y in range(size) for x in range(size)]))
    add(f'{PREFIX}/translucent.tga', rgba_tga(size, [(50,180,255,112)]*(size*size)))
    add(f'{PREFIX}/checker.tga', fixture.tga_bytes(size, size, [((255,)*3 if (x//4+y//4)%2 == 0 else (0,)*3) for y in range(size) for x in range(size)]))

    mats = [material('neutral', albedo='dark', roughness=0.8),
            f'{PREFIX}/classic_floor\n{{\nbumpmap _flat\ndiffusemap {PREFIX}/dark\nspecularmap _black\n}}\n',
            f'{PREFIX}/classic_specular\n{{\nbumpmap _flat\ndiffusemap {PREFIX}/grey\nspecularmap _white\n}}\n']
    mats += [f'{PREFIX}/classic_bump\n{{\nbumpmap {PREFIX}/normal\ndiffusemap {PREFIX}/grey\nspecularmap _white\n}}\n',
             f'{PREFIX}/classic_diffuse\n{{\nbumpmap _flat\ndiffusemap {PREFIX}/grey\nspecularmap _black\n}}\n']
    mats += [f'{PREFIX}/classic_colored\n{{\nbumpmap {PREFIX}/normal\ndiffusemap {PREFIX}/grey\nspecularmap {PREFIX}/copper\n}}\n']
    # REN-02: the classic cutout, a flat-bump diffuse whose stage alpha-tests.
    mats += [f'{PREFIX}/classic_cutout\n{{\nbumpmap _flat\n{{\nblend diffusemap\nmap {PREFIX}/cutout\nalphaTest 0.5\n}}\nspecularmap _black\n}}\n']
    # Corridor-reproducible stage state: vertex colour and a texture matrix on
    # the bump stage (REN-02). An interaction takes its vertex colour from the
    # last diffuse or specular stage, so both stages carry it.
    mats += [f'{PREFIX}/classic_vertex\n{{\nbumpmap {PREFIX}/normal\n{{\nblend diffusemap\nmap {PREFIX}/grey\nvertexColor\n}}\n{{\nblend specularmap\nmap _white\nvertexColor\n}}\n}}\n']
    # A stage expression: the specular stage is conditioned on parm7, which the
    # specimen leaves at 0, so ARB2 skips it.
    mats += [f'{PREFIX}/classic_condition\n{{\nbumpmap {PREFIX}/normal\ndiffusemap {PREFIX}/grey\n{{\nblend specularmap\nmap _white\nif ( parm7 > 0.5 )\n}}\n}}\n']
    # Two interaction stage sets: a bumped specular grey, then a flat dark one
    # (dark keeps the sum inside the comparison's unclipped range).
    mats += [f'{PREFIX}/classic_multistage\n{{\nbumpmap {PREFIX}/normal\ndiffusemap {PREFIX}/grey\nspecularmap _white\n{{\nblend bumpmap\nmap _flat\n}}\n{{\nblend diffusemap\nmap {PREFIX}/dark\n}}\n}}\n']
    mats += [f'{PREFIX}/classic_texmatrix\n{{\n{{\nblend bumpmap\nmap {PREFIX}/normal\nscale 3, 3\n}}\ndiffusemap {PREFIX}/grey\nspecularmap _white\n}}\n']
    # ... and the common stock shape: a bumped, specular cutout.
    mats += [f'{PREFIX}/classic_cutout_bump\n{{\nbumpmap {PREFIX}/normal\n{{\nblend diffusemap\nmap {PREFIX}/cutout\nalphaTest 0.5\n}}\nspecularmap _white\n}}\n']
    stations: list[dict] = []
    for row, metal in enumerate((0,1)):
        for col, roughness in enumerate((0.045, 0.16, 0.32, 0.5, 0.75, 1.0)):
            name = f'{"metal" if metal else "dielectric"}_{col}'
            mats.append(material(name, albedo='copper' if metal else 'grey', metallic=metal, roughness=roughness))
            stations.append({'name': name, 'origin': [-500+col*200, 160, 660-row*180], 'kind': 'roughness ramp', 'metallic': metal, 'roughness': roughness})
    cases = [
        ('data_scalar', dict(metallic=102/255, roughness=128/255, ao=192/255)),
        ('data_packed', dict(metallic=1, roughness=1, ao=1, layout='packed')),
        ('data_separate', dict(metallic=1, roughness=1, ao=1, layout='separate')),
        ('ao_zero', dict(metallic=102/255, roughness=128/255, ao=0)),
        ('emissive', dict(albedo='cyan', emissive=4)),
        ('normal_zero', dict(metallic=1, roughness=0.35, normal='tangentXYZ', normal_scale=0)),
        ('normal_xyz', dict(metallic=1, roughness=0.35, normal='tangentXYZ')),
        ('normal_rg', dict(metallic=1, roughness=0.35, normal='tangentRG')),
        ('normal_agb', dict(metallic=1, roughness=0.35, normal='quake4AGB')),
        ('cutout', dict(albedo='cutout', roughness=0.25, coverage='cutout')),
        ('source_alpha', dict(albedo='translucent', roughness=0.2, coverage='blend')),
        ('packed_xyz', dict(metallic=1, roughness=1, ao=1, layout='packed', normal='tangentXYZ')),
    ]
    for i, (name, kwargs) in enumerate(cases):
        mats.append(material(name, **kwargs))
        stations.append({'name': name, 'origin': [-500+(i%6)*200,160,300-(i//6)*180], 'kind': 'material semantics', 'parameters': kwargs})
    add(MODEL, sphere_ase())
    add(BACKFACE_MODEL, backface_plane_ase())
    # Same geometry with a deliberately constant shading normal: AA must
    # preserve its lighting exactly when no normal map perturbs the surface.
    add(CONSTANT_NORMAL_MODEL, sphere_ase(constant_normal=True))
    add(VERTEX_COLOR_MODEL, sphere_ase(vertex_color=(64, 128, 192)))
    # Both coordinates stay outside [0,1] even on the UV seam. ASE inverts V,
    # so clamp must reach checker(x=63,y=0), whose known value is black.
    add(SAMPLING_MODEL, sphere_ase(uv_scale=16, uv_offset=2.375))
    skin_mesh,skin_anim,skin_def=skinned_sphere()
    add(SKINNING_MESH,skin_mesh)
    add('models/openq4/pbr_lab/idle.md5anim',skin_anim)
    mixed_mesh,_,mixed_def=skinned_sphere(MIXED_SHADERS)
    add(MIXED_MESH,mixed_mesh)
    add('def/openq4_pbr_lab.def',skin_def+mixed_def)
    add('skins/openq4_pbr_lab.skin',
        f'skin {SKINNING_SKINS["dielectric"]}\n{{\n{PREFIX}/metal_3 {PREFIX}/dielectric_1\n}}\n'
        f'skin {SKINNING_SKINS["classic"]}\n{{\n{PREFIX}/metal_3 {PREFIX}/classic_specular\n}}\n')

    # Two distinct native-convention cubes exercise all six orientations,
    # sub-256 source resizing, roughness filtering, overlap and edge fallback.
    for probe, tint in (('warm', (1.0,0.6,0.25)), ('cool',(0.25,0.6,1.0))):
        for face, suffix in enumerate(('px','nx','py','ny','pz','nz')):
            pixels = []
            for y in range(size):
                for x in range(size):
                    stripe = 1.0 if abs(x-size/2)<size/12 or abs(y-size/2)<size/12 else 0.18 + face*0.06
                    pixels.append(tuple(round(255*min(1, stripe*c)) for c in tint))
            add(f'env/openq4/pbr_lab/{probe}_{suffix}.tga', fixture.tga_bytes(size,size,pixels))
        mats.append(f'lights/openq4/pbr_lab/{probe}\n{{\nopenQ4SpecularProbe {{\ncubeMap env/openq4/pbr_lab/{probe}\nintensity 2\nblendFraction 0.5\npriority 8\n}}\n{{ map _white }}\n}}\n')
    mats.append('lights/openq4/pbr_lab/projected\n{\nlightFalloffImage _white\n{\nmap _white\ncolored\n}\n}\n')
    # Constant authored ambient radiance isolates the PBR diffuse contract
    # from normal direction, roughness, distance falloff and specular IBL.
    for name, stages in (('ambient', 1), ('ambient_double', 2)):
        mats.append(f'lights/openq4/pbr_lab/{name}\n{{\nambientLight\nnoShadows\nlightFalloffImage _white\n'
                    + '{\nmap _white\ncolored\n}\n' * stages + '}\n')
    # Keep receivers visible through the volume: an opaque fog sheet would
    # trivially hide incorrect lighting and cannot prove composition.
    mats.append('lights/openq4/pbr_lab/fog\n{\nfogLight\nnoShadows\n{\nmap _white\ncolor 0.18, 0.32, 0.48, 6000\n}\n}\n')
    mats.append('lights/openq4/pbr_lab/blend\n{\nblendLight\nnoShadows\nlightFalloffImage _white\n{\nmap _white\nblend filter\ncolor 0.4, 0.7, 0.2, 1\n}\n}\n')
    mats.append(material('noself_shadow', metallic=1, roughness=0.5, no_self_shadow=True))
    mats.append(material('extreme_emissive', albedo='cyan', emissive=1000000))
    mats.append(material('emission_half', albedo='cyan', emissive=0.5))
    mats.append(material('emission_quarter', albedo='cyan', emissive=0.25))
    mats.append(material('emission_cutout', albedo='cutout', emissive=0.5, coverage='cutout'))
    mats.append(material('emission_extreme_native', albedo='faint', emissive=1000000))
    # A diffuse component keeps the negative controls visibly lit even when
    # their unfiltered subpixel specular peak misses the central patch.
    mats.append(material('aa_geometric', metallic=0.5, roughness=0.045))
    mats.append(material('aa_normal', metallic=0.5, roughness=0.045, normal='tangentXYZ'))
    # Negative ownership controls deliberately disagree with the authored PBR
    # image. They must retain the complete classic material, including its mask.
    mats.append(material('emission_mismatch', albedo='cyan', emissive=0.5).replace(
        f'blend add\nmap {PREFIX}/cyan', f'blend add\nmap {PREFIX}/grey'))
    mats.append(material('cutout_mismatch', albedo='cutout', coverage='cutout').replace(
        f'blend diffusemap\nmap {PREFIX}/cutout', f'blend diffusemap\nmap {PREFIX}/grey'))
    for name, options in (('nearest','nearest noclamp'), ('linear','linear noclamp'), ('clamp','nearest clamp'), ('mips','noclamp')):
        mats.append(material('sampler_'+name, albedo='checker', albedo_options=options))
    for name,encoding in (('xyz','tangentXYZ'),('rg','tangentRG'),('agb','quake4AGB'),('zero','tangentXYZ')):
        mats.append(material('baked_normal_'+name, normal=encoding, normal_scale=0 if name=='zero' else 1))
    # Original fixed-function controls: primary color and texture-environment
    # constants have different clamping rules on floating-point targets.
    for mode, keyword in (('primary', ''), ('vertex', 'vertexColor'), ('inverse', 'inverseVertexColor')):
        for blend, image in (('replace', 'cyan'), ('alpha', 'translucent'), ('mask', 'translucent')):
            state = 'blend' if blend == 'alpha' else 'gl_one, gl_zero'
            coverage = 'alphaTest parm4\n' if blend == 'mask' else ''
            mats.append(f'{PREFIX}/classic_stage_{mode}_{blend}\n{{\nnoShadows\n{{\n'
                        f'blend {state}\nmap {PREFIX}/{image}\n{keyword}\n'
                        f'{coverage}color parm0, parm1, parm2, parm3\n}}\n}}\n')
    add('materials/openq4_pbr_lab.mtr', '\n'.join(mats))
    room = [((-820,-1220,-20),(820,420,0)),((-820,-1220,800),(820,420,820)),((-820,-1220,0),(-800,420,800)),((800,-1220,0),(820,420,800)),((-800,-1220,0),(800,-1200,800)),((-800,400,0),(800,420,800))]
    # Keep the backdrop classic: a missing sphere must never pass ownership
    # checks merely because another green PBR surface is visible behind it.
    level = f'Version 3\n{{\n"classname" "worldspawn"\n"mtr_local_global_test" "{PREFIX}/noself_shadow"\n' + ''.join(brush(lo,hi,f'{PREFIX}/classic_floor') for lo,hi in room) + '}\n'
    precache = f'"model_sampling_test" "{SAMPLING_MODEL}"\n"model_skinning_test" "{SKINNING_MODEL}"\n"def_skinning_test" "func_animate"\n"mtr_classic_rejection_test" "{PREFIX}/classic_specular"\n' + ''.join(f'"mtr_sampling_{name}" "{PREFIX}/sampler_{name}"\n' for name in ('nearest','linear','clamp','mips'))
    precache += ''.join(f'"mtr_fixed_{name}" "{PREFIX}/classic_{name}"\n' for name in ('bump','diffuse','colored','cutout','cutout_bump','vertex','texmatrix','condition','multistage'))
    precache += f'"mtr_extreme_emissive_test" "{PREFIX}/extreme_emissive"\n'
    precache += ''.join(f'"skin_skinning_{name}_test" "{skin}"\n' for name, skin in SKINNING_SKINS.items())
    precache += f'"model_mixed_material_test" "{MIXED_MODEL}"\n'
    precache += f'"model_constant_normal_test" "{CONSTANT_NORMAL_MODEL}"\n"mtr_aa_normal_test" "{PREFIX}/aa_normal"\n"mtr_aa_geometric_test" "{PREFIX}/aa_geometric"\n'
    precache += ''.join(f'"mtr_{name}_test" "{PREFIX}/{name}"\n' for name in
                       ('emission_half','emission_quarter','emission_cutout','emission_mismatch','cutout_mismatch','emission_extreme_native'))
    precache += ''.join(f'"mtr_{name}_test" "lights/openq4/pbr_lab/{name}"\n'
                        for name in ('fog','blend','ambient','ambient_double'))
    precache += ''.join(f'"mtr_baked_normal_{name}" "{PREFIX}/baked_normal_{name}"\n' for name in ('xyz','rg','agb','zero'))
    precache += f'"model_vertex_color_test" "{VERTEX_COLOR_MODEL}"\n'
    precache += ''.join(f'"mtr_stage_{mode}_{blend}" "{PREFIX}/classic_stage_{mode}_{blend}"\n'
                        for mode in ('primary', 'vertex', 'inverse') for blend in ('replace', 'alpha', 'mask'))
    level = level.replace('"classname" "worldspawn"\n', '"classname" "worldspawn"\n'+precache, 1)
    # A classic backplate makes coverage observable in the ownership view:
    # cutout holes reveal it, while source-alpha must blend with it.
    level += '{\n"classname" "func_static"\n"name" "coverage_backplate"\n"model" "coverage_backplate"\n' + brush((10,248,30),(390,264,210),f'{PREFIX}/classic_floor') + '}\n'
    level += entity(classname='info_player_start', name='lab_start', origin='0 -640 0', angle='90')
    for station in stations:
        level += entity(classname='func_static', name=station['name'], model=MODEL, shader=f"{PREFIX}/{station['name']}", origin=' '.join(map(str,station['origin'])), solid='0')
    for name, origin, color in [('key','0 -240 700','3 2.7 2.4'),('blue_fill','-600 -80 400','0.25 0.45 1'),('warm_fill','600 -80 400','1 0.35 0.15')]:
        level += entity(classname='light', name=name, origin=origin, light_radius='1500 1500 1500', _color=color, noshadows='0' if name=='key' else '1')
    for name, x in [('warm',-480),('cool',480)]:
        level += entity(classname='light', name=f'probe_{name}', texture=f'lights/openq4/pbr_lab/{name}', origin=f'{x} 120 400', light_radius='850 850 850', _color='1 1 1', noshadows='1')
    level += entity(classname='light', name='lab_projector', texture='lights/openq4/pbr_lab/projected', origin='-250 -500 740', light_target='0 750 -200', light_right='500 0 0', light_up='0 100 375', light_start='0 15 -4', light_end='0 1000 -267', _color='0.5 0.7 1')
    add(MAP+'.map', level)
    return data, stations


def generate(root: Path, force: bool = False) -> dict:
    root = fixture.validate_runtime_root(root)
    files, stations = payloads()
    manifest = {'schema': 1, 'id': 'openq4-pbr-laboratory', 'map': MAP, 'model': MODEL, 'source': 'Original procedural openQ4 test data; no incorporated external assets.', 'stations': stations, 'cameras': {'overview': [0,-640,390,0,90,0], 'data': [-200,-260,300,0,90,0], 'normals': [-300,-260,120,0,90,0], 'coverage': [300,-260,120,0,90,0], 'sampling': [0,-1100,380,0,90,0]}, 'sha256': {k:hashlib.sha256(v).hexdigest() for k,v in sorted(files.items())}}
    for station in stations:
        x,y,z = station['origin']
        manifest['cameras']['station-'+station['name']] = [x,y-800,z,0,90,0]
    files['pbr-lab.json'] = (json.dumps(manifest, indent=2, sort_keys=True)+'\n').encode()
    paths = {name:root/name for name in files}
    fixture.validate_targets(root, paths, force)
    for name, payload in files.items():
        target = fixture.validate_contained_target(root, paths[name])
        if target.is_file() and target.read_bytes() == payload:
            continue
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(payload)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root',type=Path,required=True)
    parser.add_argument('--force',action='store_true')
    args = parser.parse_args()
    manifest = generate(args.runtime_root,args.force)
    print(f"Generated {manifest['map']}: {len(manifest['stations'])} stations, {len(manifest['sha256'])} original assets")
