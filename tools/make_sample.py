"""Generate the MIT-licensed tiny robot fixture. Python is a dev tool, not a runtime dependency."""
import json,struct,math,pathlib,copy
from PIL import Image
root=pathlib.Path(__file__).resolve().parents[1]/'assets'
image=Image.new('RGBA',(16,16))
for y in range(16):
 for x in range(16):
  c=255 if ((x//4+y//4)%2)==0 else 210
  image.putpixel((x,y),(c,c,c,255))
image.save(root/'robot.png')
buf=bytearray();views=[];accessors=[]
def acc(rows,kind,typ=5126):
 while len(buf)%4:buf.append(0)
 start=len(buf);flat=[v for row in rows for v in (row if isinstance(row,(tuple,list)) else [row])]
 code={5126:'f',5123:'H'}[typ];buf.extend(struct.pack('<'+code*len(flat),*flat));view=len(views);views.append({'buffer':0,'byteOffset':start,'byteLength':len(buf)-start})
 result={'bufferView':view,'componentType':typ,'count':len(rows),'type':kind}
 if kind=='VEC3':result.update(min=[min(r[i] for r in rows) for i in range(3)],max=[max(r[i] for r in rows) for i in range(3)])
 if kind=='SCALAR':result.update(min=[min(rows)],max=[max(rows)])
 accessors.append(result);return len(accessors)-1
# All coordinates are metres, Y-up. Node 0 is the mesh; bones start at 1.
nodes=[{'name':'Robot','mesh':0,'skin':0},{'name':'Root','children':[2,3,4,5,6,7]},
 {'name':'Torso','translation':[0,1.45,0]}, {'name':'Head','translation':[0,2.1,0]},
 {'name':'LeftArm','translation':[-.53,1.78,0]}, {'name':'RightArm','translation':[.53,1.78,0]},
 {'name':'LeftLeg','translation':[-.25,.9,0]}, {'name':'RightLeg','translation':[.25,.9,0]}]
materials=[{'name':n,'pbrMetallicRoughness':{'baseColorFactor':c+[1],'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1},'doubleSided':True} for n,c in [('Blue',[.14,.65,.85]),('Cream',[1,.79,.42]),('Dark',[.06,.12,.19]),('Orange',[1,.32,.07])]]
prims=[]
def box(center,size,bone,material):
 cx,cy,cz=center;sx,sy,sz=[v/2 for v in size]
 corners=[(cx+x*sx,cy+y*sy,cz+z*sz) for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
 pos=[];norm=[];uv=[];ix=[]
 for face,n in [([0,3,2,1],(0,0,-1)),([4,5,6,7],(0,0,1)),([0,4,7,3],(-1,0,0)),([1,2,6,5],(1,0,0)),([3,7,6,2],(0,1,0)),([0,1,5,4],(0,-1,0))]:
  base=len(pos);pos.extend(corners[i] for i in face);norm.extend([n]*4);uv.extend([(0,1),(0,0),(1,0),(1,1)]);ix.extend([base,base+1,base+2,base,base+2,base+3])
 attrs={'POSITION':acc(pos,'VEC3'),'NORMAL':acc(norm,'VEC3'),'TEXCOORD_0':acc(uv,'VEC2'),'JOINTS_0':acc([(bone-1,0,0,0)]*len(pos),'VEC4',5123),'WEIGHTS_0':acc([(1,0,0,0)]*len(pos),'VEC4')}
 prims.append({'attributes':attrs,'indices':acc(ix,'SCALAR',5123),'material':material})
box((0,1.45,0),(.9,.8,.48),2,0)
box((0,2.18,0),(.72,.62,.55),3,1)
box((0,2.2,-.29),(.54,.19,.06),3,2)
box((-.62,1.38,0),(.25,.85,.33),4,0)
box((.62,1.38,0),(.25,.85,.33),5,0)
box((-.25,.52,0),(.31,.8,.35),6,2)
box((.25,.52,0),(.31,.8,.35),7,2)
box((-.25,.13,-.12),(.4,.23,.6),6,3)
box((.25,.13,-.12),(.4,.23,.6),7,3)
binds=[]
for node in nodes[1:]:
 x,y,z=node.get('translation',[0,0,0]);binds.append([1,0,0,0,0,1,0,0,0,0,1,0,-x,-y,-z,1])
ib=acc(binds,'MAT4');times=acc([0,.25,.5,.75,1],'SCALAR');anims=[]
def quat(axis,angle):
 q=[0.,0.,0.,math.cos(angle/2)];q[axis]=math.sin(angle/2);return q
def animation(name,tracks):
 samplers=[];channels=[]
 for node,axis,angles in tracks:
  out=acc([quat(axis,a) for a in angles],'VEC4');channels.append({'sampler':len(samplers),'target':{'node':node,'path':'rotation'}});samplers.append({'input':times,'output':out,'interpolation':'LINEAR'})
 anims.append({'name':name,'channels':channels,'samplers':samplers})
animation('Walk',[(6,0,[0,.55,0,-.55,0]),(7,0,[0,-.55,0,.55,0]),(4,0,[0,-.4,0,.4,0]),(5,0,[0,.4,0,-.4,0])])
animation('Wave',[(5,2,[0,1.6,2.5,1.6,0]),(3,2,[0,.12,0,-.12,0])])
a={'asset':{'version':'2.0','generator':'Ember sample generator'},'scene':0,'scenes':[{'nodes':[0,1]}],'nodes':nodes,'meshes':[{'name':'PixelRobot','primitives':prims}],'materials':materials,'skins':[{'joints':list(range(1,len(nodes))),'inverseBindMatrices':ib,'skeleton':1}],'animations':anims,'images':[{'uri':'robot.png'}],'textures':[{'source':0,'sampler':0}],'samplers':[{'magFilter':9728,'minFilter':9728,'wrapS':10497,'wrapT':10497}],'buffers':[{'byteLength':len(buf),'uri':'robot.bin'}],'bufferViews':views,'accessors':accessors}
(root/'robot.bin').write_bytes(buf);(root/'robot.gltf').write_text(json.dumps(a,indent=2))
g=copy.deepcopy(a);g['buffers'][0].pop('uri');png=(root/'robot.png').read_bytes()
while len(buf)%4:buf.append(0)
g['images']=[{'bufferView':len(g['bufferViews']),'mimeType':'image/png'}];g['bufferViews'].append({'buffer':0,'byteOffset':len(buf),'byteLength':len(png)});buf.extend(png);g['buffers'][0]['byteLength']=len(buf)
while len(buf)%4:buf.append(0)
j=json.dumps(g,separators=(',',':')).encode();j+=b' '*((-len(j))%4);glb=struct.pack('<III',0x46546c67,2,12+8+len(j)+8+len(buf))+struct.pack('<II',len(j),0x4e4f534a)+j+struct.pack('<II',len(buf),0x004e4942)+buf
(root/'robot.glb').write_bytes(glb)
# OBJ+MTL uses the same small image.
(root/'textured.mtl').write_text('newmtl Grid\nKd 0.9 0.6 0.2\nmap_Kd robot.png\n')
(root/'textured.obj').write_text('mtllib textured.mtl\nusemtl Grid\nv -1 0 0\nv 1 0 0\nv 1 2 0\nv -1 2 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
print('Generated robot OBJ/glTF/GLB resources')
