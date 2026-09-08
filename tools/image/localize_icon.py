from pathlib import Path
from PIL import Image, ImageFont, ImageDraw, ImageFilter
import numpy as np
r=Path(__file__).resolve().parents[1];assets=r.parent/'KAWAXP-port/assets'
backup=r/'originals/kawaxp-icon.png'
if not backup.exists():backup.write_bytes((assets/'kawaxp-icon.png').read_bytes())
base=Image.open(assets/'title-preview.png').convert('RGB')
bg=Image.open(r/'originals/title_bg.png').convert('RGB')
# Rebuild the complete title with one font, keeping the original logo footprint.
box=(28,108,612,228);base.paste(bg.crop(box),box[:2])
scale=4;f=ImageFont.truetype('/System/Library/Fonts/Supplemental/Songti.ttc',120*scale,index=1)
title='河原崎家的一族'
bb=f.getbbox(title);m=Image.new('L',(bb[2]-bb[0]+16,bb[3]-bb[1]+16));d=ImageDraw.Draw(m)
d.text((8-bb[0],8-bb[1]),title,font=f,fill=255,stroke_width=2)
m=m.crop(m.getbbox()).resize((564*scale,100*scale),Image.Resampling.LANCZOS)
mask=Image.new('L',(584*scale,120*scale));mask.paste(m,(10*scale,10*scale))
# Red ink and the original white outer / black inner contour treatment.
layer=Image.new('RGBA',mask.size)
layer.paste((239,239,239,255),(0,0),mask.filter(ImageFilter.MaxFilter(29)))
layer.paste((0,0,0,255),(0,0),mask.filter(ImageFilter.MaxFilter(21)))
layer.paste((198,0,7,255),(0,0),mask)
layer=layer.resize((584,120),Image.Resampling.LANCZOS)
base=base.convert('RGBA');base.alpha_composite(layer,box[:2]);base=base.convert('RGB')
preview=r/'zh-CN/title-preview.zh-CN.png';base.save(preview)
icon=Image.new('RGB',(256,256));icon.paste(base.resize((256,192),Image.Resampling.LANCZOS),(0,32))
icon.save(assets/'kawaxp-icon.png')
a=np.array(Image.open(backup).convert('RGB'));b=np.array(icon);changed=np.any(a!=b,2)
# Account for Lanczos filter support around the edited source rectangle.
allowed=np.zeros(changed.shape,bool);allowed[72:126,8:248]=True
assert not (changed&~allowed).any()
assert icon.size==(256,256)
print('Updated',assets/'kawaxp-icon.png','changed pixels:',int(changed.sum()))
