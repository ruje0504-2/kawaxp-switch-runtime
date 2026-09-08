from pathlib import Path
import json
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[1];result={}
for p in sorted((root/'zh-CN').glob('*.png')):
 a=np.array(Image.open(root/'originals'/p.name).convert('RGBA'));b=np.array(Image.open(p).convert('RGBA'))
 assert a.shape==b.shape,p.name
 mask=np.array(Image.open(root/'review'/(p.stem+'.edit-mask.png')))>0
 changed=np.any(a!=b,axis=2);assert not (changed&~mask).any(),p.name
 keys=((a[:,:,:3]==[0,255,0]).all(2)|(a[:,:,:3]==[0,0,255]).all(2))
 key_changed=keys&changed
 assert not key_changed.any(),p.name+' changed transparency-key pixels'
 result[p.name]={'dimensions_equal':True,'outside_edit_regions_unchanged':True,'alpha_outside_edits_unchanged':bool((a[:,:,3][~mask]==b[:,:,3][~mask]).all()),'key_pixels_changed':int(key_changed.sum())}
(root/'review/pixel-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
print(len(result),'same-sized PNGs; all pixels outside declared edits unchanged')
for n,r in result.items():
 if r['key_pixels_changed']:print('KEY REVIEW',n,r['key_pixels_changed'])
try:
 from fontTools.ttLib import TTFont
 f=TTFont('/System/Library/Fonts/Supplemental/Songti.ttc',fontNumber=1)
 cmap=f.getBestCmap();ops=json.loads((root/'制作坐标.json').read_text());chars=set(''.join(op['text'] for a in ops.values() for op in a));missing=[c for c in chars if ord(c)>127 and ord(c) not in cmap]
 assert not missing,missing
 print('Chinese font coverage passed')
except ImportError:print('fontTools unavailable')
