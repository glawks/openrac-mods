#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lorenzogarrone
"""Build the private graphics companion from the player's extracted R&C1 assets.

Preserves the drill geometry, UVs and material after removing the lower block. The housing binds to
Walloper root0, the rear valve to joint3 and the corkscrew to joint4. The native
rotor supplies both pivots and cancels native jab deformation. Never edits game
data.
"""
import argparse,copy,hashlib,json,struct,sys
from pathlib import Path

COMPONENT_FORMATS={5121:'B',5123:'H',5125:'I',5126:'f'}
ACCESSOR_COMPONENTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
LOWER_BLOCK_JOINT=2

def read_glb(path):
 data=path.read_bytes()
 magic,version,length=struct.unpack_from('<III',data)
 if magic!=0x46546c67 or version!=2 or length!=len(data):raise ValueError(f'Invalid GLB: {path}')
 size,kind=struct.unpack_from('<II',data,12)
 if kind!=0x4e4f534a:raise ValueError('GLB has no JSON')
 document=json.loads(data[20:20+size]);offset=20+size
 size,kind=struct.unpack_from('<II',data,offset)
 if kind!=0x004e4942:raise ValueError('GLB has no binary buffer')
 return document,bytearray(data[offset+8:offset+8+size])

def write_glb(document,buffer):
 document['buffers']=[{'byteLength':len(buffer)}]
 encoded=json.dumps(document,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4)
 buffer+=b'\0'*(-len(buffer)%4)
 return struct.pack('<III',0x46546c67,2,28+len(encoded)+len(buffer))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(buffer),0x004e4942)+buffer

def accessor_layout(document,index):
 accessor=document['accessors'][index];view=document['bufferViews'][accessor['bufferView']]
 base=view.get('byteOffset',0)+accessor.get('byteOffset',0)
 row=struct.Struct('<'+COMPONENT_FORMATS[accessor['componentType']]*ACCESSOR_COMPONENTS[accessor['type']])
 stride=view.get('byteStride',row.size)
 return accessor,view,row,[base+i*stride for i in range(accessor['count'])]

def accessor_values(document,buffer,index):
 _,_,row,offsets=accessor_layout(document,index)
 return [row.unpack_from(buffer,offset) for offset in offsets]

def replace_packed_accessor(document,buffer,index,records):
 accessor,view,row,offsets=accessor_layout(document,index)
 capacity=view['byteLength']
 if (accessor.get('byteOffset',0) or view.get('byteStride',row.size)!=row.size or
     capacity!=len(offsets)*row.size or any(other is not accessor and other['bufferView']==accessor['bufferView']
                                           for other in document['accessors'])):
  raise ValueError('Expected a dedicated packed drill mesh accessor')
 data=b''.join(records)
 if not data or len(data)>capacity or len(data)%row.size:
  raise ValueError('Invalid compacted drill mesh accessor')
 start=offsets[0]
 buffer[start:start+capacity]=data.ljust(capacity,b'\0')
 view['byteLength']=len(data)
 accessor['count']=len(data)//row.size
 if 'min' in accessor or 'max' in accessor:
  columns=list(zip(*row.iter_unpack(data)))
  if 'min' in accessor:accessor['min']=[min(values) for values in columns]
  if 'max' in accessor:accessor['max']=[max(values) for values in columns]

def remove_lower_block(document,buffer,primitive):
 attrs=primitive['attributes']
 joints=accessor_values(document,buffer,attrs['JOINTS_0'])
 weights=accessor_values(document,buffer,attrs['WEIGHTS_0'])
 # The stock block is entirely on joint 2. Match that component before
 # retargeting it, while the original mesh still separates it from the housing.
 block={i for i,(joint_row,weight_row) in enumerate(zip(joints,weights))
        if {joint for joint,weight in zip(joint_row,weight_row) if weight>0}=={LOWER_BLOCK_JOINT}}
 retained=[i for i in range(len(joints)) if i not in block]
 remap={source:target for target,source in enumerate(retained)}
 index=primitive['indices']
 _,_,index_row,_=accessor_layout(document,index)
 indices=accessor_values(document,buffer,index)
 kept_indices=[]
 removed=0
 for a,b,c in zip(indices[::3],indices[1::3],indices[2::3]):
  triangle=(a[0],b[0],c[0])
  excluded=sum(i in block for i in triangle)
  if excluded==3:removed+=1
  elif excluded:raise ValueError('The lower block shares a triangle with the drill body')
  else:kept_indices.extend(index_row.pack(remap[i]) for i in triangle)
 if len(block)!=24 or removed!=12 or len(indices)%3:
  raise ValueError('Unexpected lower block geometry in the original Hoven drill')
 for attribute in attrs.values():
  _,_,row,offsets=accessor_layout(document,attribute)
  replace_packed_accessor(document,buffer,attribute,
      [buffer[offsets[i]:offsets[i]+row.size] for i in retained])
 replace_packed_accessor(document,buffer,index,kept_indices)
 return removed

def original_head_pivot(source):
 document,raw=source
 matrices=accessor_values(document,raw,document['skins'][0]['inverseBindMatrices'])
 head=matrices[35]
 for r in range(3):
  for c in range(3):
   if abs(head[r*4+c]-(1 if r==c else 0))>1e-5:raise ValueError('The original drill head bind rotation changed')
 return [-head[12],-head[13],-head[14]]

def retarget(source,target,ratio,report=None):
 original,raw=source;document=copy.deepcopy(original);buffer=bytearray(raw)
 target_doc,target_raw=target
 source_valve_joints={21,22,23}
 vertex_counts={0:0,3:0,4:0}
 triangle_counts={0:0,3:0,4:0}
 removed_block_triangles=0
 for mesh in document['meshes']:
  for primitive in mesh['primitives']:
   removed_block_triangles+=remove_lower_block(document,buffer,primitive)
   attrs=primitive['attributes'];accessor,_,_,offsets=accessor_layout(document,attrs['POSITION'])
   if accessor['componentType']!=5126:raise ValueError('Positions must be float32')
   joints=accessor_values(document,buffer,attrs['JOINTS_0']);weights=accessor_values(document,buffer,attrs['WEIGHTS_0'])
   positions=accessor_values(document,buffer,attrs['POSITION'])
   new_joints=[]
   for position,joint,weight in zip(positions,joints,weights):
    influences={j for j,w in zip(joint,weight) if w>0}
    destination=0
    if 35 in influences:
     if influences!={35} or position[1]>-4.70:
      raise ValueError('The original corkscrew must be a rigid component outside the housing')
     destination=4
    elif influences & source_valve_joints:
     if not influences <= source_valve_joints:
      raise ValueError('The original rear valve has influences outside its own joints')
     destination=3
    new_joints.append(destination)
    vertex_counts[destination]+=1
   indices=[row[0] for row in accessor_values(document,buffer,primitive['indices'])]
   for start in range(0,len(indices),3):
    first,second,third=(new_joints[i] for i in indices[start:start+3])
    if not first==second==third:
     raise ValueError('A triangle would span independently moving drill components')
    triangle_counts[first]+=1
   for offset,values in zip(offsets,positions):
    struct.pack_into('<3f',buffer,offset,*(v*ratio for v in values))
   accessor['min']=[v*ratio for v in accessor['min']];accessor['max']=[v*ratio for v in accessor['max']]
   for attribute,first in [('JOINTS_0',0),('WEIGHTS_0',1)]:
    accessor,_,_,offsets=accessor_layout(document,attrs[attribute]);kind=accessor['componentType']
    fmt={5121:'4B',5123:'4H',5126:'4f'}[kind]
    value=first*(255 if kind==5121 and accessor.get('normalized') else 65535 if kind==5123 and accessor.get('normalized') else 1)
    for index,offset in enumerate(offsets):
     component=new_joints[index] if attribute=='JOINTS_0' else value
     struct.pack_into('<'+fmt,buffer,offset,component,0,0,0)
    accessor.pop('min',None);accessor.pop('max',None)
 document.pop('animations',None)
 document['nodes']=copy.deepcopy(target_doc['nodes']);document['scenes']=copy.deepcopy(target_doc['scenes']);document['scene']=target_doc.get('scene',0)
 mesh_bound=False
 for node in document['nodes']:
  if 'mesh' in node:
   if not mesh_bound:node['mesh']=0;mesh_bound=True
   else:node.pop('mesh');node.pop('skin',None)
 skin=copy.deepcopy(target_doc['skins'][0]);old_accessor=target_doc['accessors'][skin['inverseBindMatrices']]
 old_view=target_doc['bufferViews'][old_accessor['bufferView']]
 padding=-len(buffer)%4;buffer.extend(b'\0'*padding);start=len(buffer)
 view_start=old_view.get('byteOffset',0);buffer.extend(target_raw[view_start:view_start+old_view['byteLength']])
 new_view=copy.deepcopy(old_view);new_view['byteOffset']=start;new_view['buffer']=0
 document['bufferViews'].append(new_view);new_accessor=copy.deepcopy(old_accessor);new_accessor['bufferView']=len(document['bufferViews'])-1
 document['accessors'].append(new_accessor);skin['inverseBindMatrices']=len(document['accessors'])-1;document['skins']=[skin]
 document['images'][0]['uri']='../textures/moby_revolverator_original.png'
 for mesh in document['meshes']:mesh['name']='revolverator_original'
 if vertex_counts!={0:1202,3:112,4:208} or removed_block_triangles!=12:
  raise ValueError('Unexpected original Hoven drill vertex components')
 if report is not None:
  report.update({'body_joint':0,'valve_joint':3,'head_joint':4,'original_head_joint':35,
   'original_valve_joints':sorted(source_valve_joints),
   'head_vertices':vertex_counts[4],'body_vertices':vertex_counts[0],'valve_vertices':vertex_counts[3],
   'head_triangles':triangle_counts[4],'body_triangles':triangle_counts[0],'valve_triangles':triangle_counts[3],
   'mixed_triangles':0,'removed_block_triangles':removed_block_triangles,'head_pivot_source':original_head_pivot(source),
   'head_pivot_model':[v*ratio for v in original_head_pivot(source)],
   'importer_inverse_bind_matrices_preserved':True,'mesh_instances':1})
 return write_glb(document,buffer)

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--levels',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--editor',type=Path,required=True)
 parser.add_argument('--target-class',type=int,default=2017,help='Independent Revolverator render class; 180 explicitly builds the legacy replacement pack')
 parser.add_argument('--version',choices=('ntsc','pal'),default='ntsc',help='Disc region of the extracted levels (default: ntsc for existing build commands)')
 args=parser.parse_args();levels=args.levels.resolve();out=args.out.resolve()
 if out.exists():raise ValueError('Output exists; choose a new folder')
 if out==levels or out.is_relative_to(levels):raise ValueError('Output must be separate from the installed game levels')
 if args.target_class<0 or args.target_class>=0x800:raise ValueError('Target class must fit the native class-ID map')
 source_level=levels/'level_12';source_manifest=json.loads((source_level/'manifest.json').read_text());source_info=source_manifest['classes']['moby']['164']
 source_format=source_manifest.get('format')
 if source_format not in (3,4) or source_manifest.get('game')!='rac1' or source_manifest.get('version')!=args.version:raise ValueError(f'The original drill must come from extracted R&C1 {args.version} format 3/4')
 source=read_glb(source_level/source_info['mesh'])
 source_image=(source_level/'mobys'/source[0]['images'][0]['uri']).resolve();texture=source_image.read_bytes()
 files={};replaces={};rotors={}
 for index in range(19):
  folder=levels/f'level_{index:02}';manifest=json.loads((folder/'manifest.json').read_text());info=manifest['classes']['moby'].get('180')
  if not info:raise ValueError(f'Native Walloper class 180 is missing in level {index}')
  if args.target_class!=180 and str(args.target_class) in manifest['classes']['moby']:raise ValueError(f'Target render class {args.target_class} already exists in level {index}')
  if manifest.get('format')!=source_format or manifest.get('game')!='rac1' or manifest.get('version')!=args.version:raise ValueError(f'This pack requires consistent R&C1 {args.version} format {source_format} levels')
  target=read_glb(folder/info['mesh']);report={};model=retarget(source,target,float(source_info['scale'])/float(info['scale']),report)
  rotors[f'level_{index:02}']={**report,'native_class_scale':float(info['scale'])}
  files[f'level_{index:02}/mobys/moby_{args.target_class}.glb']=model;files[f'level_{index:02}/textures/moby_revolverator_original.png']=texture
  replaces[f'level_{index:02}']={'mobys':[args.target_class],'textures':['moby_revolverator_original']}
 sys.path.insert(0,str(args.editor.resolve()));import overrides
 originals=overrides.originals(files,levels)
 description='Original Hoven drill with a rigid housing, moving rear valve and rotating corkscrew. '
 description+=('Replaces Walloper appearance.' if args.target_class==180 else f'Uses distinct render class{args.target_class}; original Walloper art remains unchanged. Requires the matching extended-level manifest.')
 manifest={'format':1,'id':'revolverator-original-model' if args.target_class==180 else 'revolverator-additive-model','game':'rac1','version':args.version,'name':'Revolverator original model','author':'Insomniac Games; community restoration adapter','description':description,'levels_format':source_format,'replaces':replaces,'originals':originals}
 out.mkdir(parents=True)
 for name,data in files.items():path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 (out/'openrac-mod.json').write_text(json.dumps(manifest,indent=2)+'\n')
 (out/'rotor.json').write_text(json.dumps({'format':1,'version':args.version,'levels_format':source_format,'target_class':args.target_class,'native_source_class':180,'source_class_scale':float(source_info['scale']),
  'source_sha256':hashlib.sha256((source_level/source_info['mesh']).read_bytes()).hexdigest(),
  'texture_sha256':hashlib.sha256(texture).hexdigest(),'levels':rotors},indent=2)+'\n')
 (out/'README.txt').write_text('Private local asset pack generated from the installed R&C1 disc. Original art belongs to Insomniac Games / Sony. '+description+'\n')
 print(json.dumps({'levels':len(replaces),'files':len(files),'source':str(source_level/source_info['mesh']),'source_sha256':hashlib.sha256((source_level/source_info['mesh']).read_bytes()).hexdigest(),'output':str(out)},indent=2))
if __name__=='__main__':main()
