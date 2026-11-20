#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 noire342
"""Validate rear-valve and head binding, source art and importer compatibility."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--levels',type=Path,required=True)
    parser.add_argument('--pack',type=Path,required=True)
    parser.add_argument('--editor',type=Path,required=True)
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('rotor_builder',Path(__file__).parents[1]/'tools/build_model_pack.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    sys.path.insert(0,str(args.editor.resolve()));import overrides
    source_level=args.levels/'level_12'
    source_info=json.loads((source_level/'manifest.json').read_text())['classes']['moby']['164']
    source_path=source_level/source_info['mesh'];source,raw=builder.read_glb(source_path)
    original_primitive=source['meshes'][0]['primitives'][0];attributes=original_primitive['attributes']
    original={name:builder.accessor_values(source,raw,index) for name,index in attributes.items()}
    indices=[row[0] for row in builder.accessor_values(source,raw,original_primitive['indices'])]
    block={i for i,(joints,weights) in enumerate(zip(original['JOINTS_0'],original['WEIGHTS_0']))
           if {joint for joint,weight in zip(joints,weights) if weight>0}=={2}}
    assert len(block)==24
    retained=[i for i in range(len(original['POSITION'])) if i not in block]
    remap={before:after for after,before in enumerate(retained)}
    triangles=[]
    for start in range(0,len(indices),3):
        triangle=indices[start:start+3]
        if set(triangle)<=block:
            continue
        assert set(triangle).isdisjoint(block)
        triangles.extend((remap[i],) for i in triangle)
    assert len(indices)-len(triangles)==36
    original={name:[rows[i] for i in retained] for name,rows in original.items()}
    texture=(source_path.parent/source['images'][0]['uri']).read_bytes()
    metadata=json.loads((args.pack/'rotor.json').read_text())
    target_class=int(metadata.get('target_class',180))
    assert metadata['source_sha256']==hashlib.sha256(source_path.read_bytes()).hexdigest()
    assert metadata['texture_sha256']==hashlib.sha256(texture).hexdigest()
    total=0
    for level in range(19):
        name=f'level_{level:02}'
        native_info=json.loads((args.levels/name/'manifest.json').read_text())['classes']['moby']['180']
        native_path=args.levels/name/native_info['mesh']
        model_path=args.pack/name/f'mobys/moby_{target_class}.glb'
        document,data=builder.read_glb(model_path)
        primitive=document['meshes'][0]['primitives'][0]
        output={key:builder.accessor_values(document,data,index) for key,index in primitive['attributes'].items()}
        ratio=float(source_info['scale'])/float(native_info['scale'])
        expected_positions=[tuple(struct.unpack('<f',struct.pack('<f',value*ratio))[0] for value in row)
                            for row in original['POSITION']]
        assert output['POSITION']==expected_positions, f'{name}: authentic geometry moved beyond the native scale conversion'
        for attribute in ('NORMAL','TEXCOORD_0','COLOR_0','_LIGHT_SLOT'):
            assert output[attribute]==original[attribute],f'{name}: source {attribute} changed'
        assert builder.accessor_values(document,data,primitive['indices'])==triangles,f'{name}: source topology changed'
        assignments=[]
        for before_joints,before_weights,after_joints,after_weights in zip(
                original['JOINTS_0'],original['WEIGHTS_0'],output['JOINTS_0'],output['WEIGHTS_0']):
            original_bones={joint for joint,weight in zip(before_joints,before_weights) if weight>0}
            expected_joint=0
            if original_bones=={35}:
                expected_joint=4
            elif original_bones and original_bones<={21,22,23}:
                expected_joint=3
            assert after_joints==(expected_joint,0,0,0),f'{name}: original geometry bound to wrong joint'
            assert after_weights[0]>0 and sum(after_weights[1:])==0,f'{name}: a rigid vertex has blended weights'
            assignments.append(expected_joint)
        assert Counter(assignments)=={0:1202,3:112,4:208}
        valve_points={position for position,joint in zip(original['POSITION'],assignments) if joint==3}
        other_points={position for position,joint in zip(original['POSITION'],assignments) if joint!=3}
        assert len(valve_points)==72 and not valve_points&other_points,f'{name}: valve shares welded points with other components'
        triangle_counts=Counter()
        for start in range(0,len(triangles),3):
            first,second,third=(assignments[triangle[0]] for triangle in triangles[start:start+3])
            assert first==second==third,f'{name}: a triangle spans multiple moving components'
            triangle_counts[first]+=1
        assert triangle_counts=={0:1192,3:134,4:202}
        assert sum('mesh' in node for node in document['nodes'])==1,f'{name}: duplicate mesh instance'
        assert overrides.same_skeleton(overrides.skeleton(model_path),overrides.skeleton(native_path)),f'{name}: launcher skeleton compatibility changed'
        assert (args.pack/name/'textures/moby_revolverator_original.png').read_bytes()==texture,f'{name}: authentic texture changed'
        assert metadata['levels'][name]['head_vertices']==208
        assert metadata['levels'][name]['valve_vertices']==112
        assert metadata['levels'][name]['removed_block_triangles']==12
        if target_class!=180:
            assert not (args.pack/name/'mobys/moby_180.glb').exists(),f'{name}: independent pack must not replace native Walloper geometry'
        total+=1
    print(json.dumps({'models':total,'target_class':target_class,'body_vertices_each':1202,'valve_vertices_each':112,'head_vertices_each':208,
        'removed_block_triangles_each':12,'mixed_triangles':0,'remaining_geometry_uv_normals_topology_preserved':True,
        'all_native_inverse_binds_compatible':True,'original_texture_preserved':True},indent=2))


if __name__=='__main__':main()
