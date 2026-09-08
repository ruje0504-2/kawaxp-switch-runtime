from pathlib import Path
from PIL import Image,ImageDraw
import json,hashlib,html,zipfile,csv
r=Path(__file__).resolve().parents[1];p=r/'previews';p.mkdir(exist_ok=True)
files=sorted((r/'zh-CN').glob('*.png'));checks={};items=[]
for f in files:
 n=f.stem;checks[f.name]=hashlib.sha256(f.read_bytes()).hexdigest()
 a=Image.open(r/'originals'/f.name).convert('RGBA');b=Image.open(f).convert('RGBA')
 note='原尺寸显示；宽图可横向滚动'
 if n.startswith('sc_pt'):
  a=a.crop((0,0,284,96));b=b.crop((0,0,284,96));note='仅展示导航区；下方缩略图未修改'
 elif n.startswith('end_staff'):
  a=a.crop((0,460,188,552));b=b.crop((0,460,188,552));note='展示清晰帧；完整动画图集在 zh-CN 文件夹，旋涡近似重制'
 a.save(p/f'{n}.original.png');b.save(p/f'{n}.png')
 items.append({'name':n,'note':note,'size':list(Image.open(f).size)})
(r/'文件校验.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2))
page='''<!doctype html><meta charset="utf-8"><title>KAWAXP 图片汉化对照</title>
<style>body{background:#191919;color:#eee;font:16px system-ui;margin:28px}h1{font-size:24px}p{line-height:1.6}select,button{font:inherit;padding:8px;background:#333;color:white;border:1px solid #777;border-radius:5px}.pair{display:flex;gap:20px;overflow:auto;align-items:flex-start}figure{margin:0;flex-shrink:0;background:#262626}figcaption{padding:10px}img{display:block}small{color:#bbb}</style>
<h1>KAWAXP · 简体中文图片对照</h1><p>38 张输出 · 保留原尺寸与切片坐标。字体、局部背景修补和片尾旋涡为近似重制，尚未进行回包与实机验证。</p>
<button id="prev">上一张</button> <select id="asset"></select> <button id="next">下一张</button><p id="note"></p>
<div class="pair"><figure><figcaption>PC 原图</figcaption><img id="original"></figure><figure><figcaption>简体中文</figcaption><img id="localized"></figure></div>
<p><small>绿色与蓝色是原图透明键色，正常游戏内应由引擎处理。剧情回想只预览导航区域。详见 README.md 与像素验收记录。</small></p>
<script>const items=DATA;const sel=document.querySelector('#asset');for(const v of items){let o=document.createElement('option');o.textContent=v.name;sel.append(o)}function show(){let v=items[sel.selectedIndex];document.querySelector('#original').src='previews/'+v.name+'.original.png';document.querySelector('#localized').src='previews/'+v.name+'.png';document.querySelector('#note').textContent=v.size.join(' × ')+' 像素 · '+v.note}sel.onchange=show;document.querySelector('#prev').onclick=()=>{sel.selectedIndex=(sel.selectedIndex+items.length-1)%items.length;show()};document.querySelector('#next').onclick=()=>{sel.selectedIndex=(sel.selectedIndex+1)%items.length;show()};sel.value='cg_pt';show();</script>'''
(r/'对照预览.html').write_text(page.replace('DATA',json.dumps(items,ensure_ascii=False)))
# Compact overview, showing only non-explicit interface artwork.
canvas=Image.new('RGB',(1100,680),(25,25,25));d=ImageDraw.Draw(canvas)
for x,y,n,box,maxsize in [(20,30,'cg_pt',(0,0,384,96),(520,130)),(20,190,'title_pt',(0,0,408,648),(330,450)),(425,190,'soundk_bg',None,(640,480))]:
 im=Image.open(r/'zh-CN'/f'{n}.png').convert('RGBA')
 if box:im=im.crop(box)
 im.thumbnail(maxsize);canvas.paste(im,(x,y),im);d.text((x,y-18),n,fill='white')
canvas.save(p/'交付概览.png')
include=['README.md','图片汉化清单.csv','统一译名.csv','制作坐标.json','像素验收.json','文件校验.json','对照预览.html','review/pixel-check.json']
with zipfile.ZipFile(r/'KAWAXP-简体图片-v1.zip','w',zipfile.ZIP_DEFLATED) as z:
 for rel in include:z.write(r/rel,'KAWAXP-简体图片-v1/'+rel)
 for folder in ['zh-CN','previews','tools']:
  for f in sorted((r/folder).glob('*')):
   if f.is_file():z.write(f,'KAWAXP-简体图片-v1/'+f.relative_to(r).as_posix())
with zipfile.ZipFile(r/'KAWAXP-简体图片-v1.zip') as z:
 assert z.testzip() is None
 assert sum('/zh-CN/' in n and n.endswith('.png') for n in z.namelist())==38
print('Packaged 38 PNGs with comparisons, coordinates, SHA-256 and validation reports')
