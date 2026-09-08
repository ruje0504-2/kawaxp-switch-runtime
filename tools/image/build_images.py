#!/usr/bin/env python3
"""Deterministic, fixed-coordinate Chinese UI artwork. Never writes original assets."""
from pathlib import Path
import sys,json,math,csv
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'work/zh-image-deps'))
# Optional local dependency directory supplied by caller.
import os
if os.environ.get('KAWA_IMAGE_DEPS'):sys.path.insert(0,os.environ['KAWA_IMAGE_DEPS'])
import numpy as np
import cv2
from PIL import Image,ImageDraw,ImageFont,ImageFilter
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'originals';DST=ROOT/'zh-CN';DST.mkdir(exist_ok=True)
FONT='/System/Library/Fonts/Supplemental/Songti.ttc'
OPS={};IM={};MASKS={}
def load(n):
 if n not in IM:
  IM[n]=Image.open(SRC/(n+'.png')).convert('RGBA');MASKS[n]=Image.new('L',IM[n].size);OPS[n]=[]
 return IM[n]
def font(size):return ImageFont.truetype(FONT,max(5,int(size)),index=1)
def mark(n,box,label):
 ImageDraw.Draw(MASKS[n]).rectangle((box[0],box[1],box[2]-1,box[3]-1),fill=255)
 OPS[n].append({'rect':list(box),'text':label})
def erase(n,b,transparent=False,solid=None):
 im=load(n);x0,y0,x1,y1=map(int,b);b=(x0,y0,x1,y1)
 if transparent:im.paste((0,0,0,0),b);return
 if solid is not None:im.paste(solid,b);return
 # Reconstruct only the text interior. Outer ornamental borders remain untouched.
 pad=7;left=max(0,x0-pad);top=max(0,y0-pad);right=min(im.width,x1+pad);bottom=min(im.height,y1+pad)
 a=np.array(im.crop((left,top,right,bottom)));m=np.zeros(a.shape[:2],np.uint8)
 roi=a[y0-top:y1-top,x0-left:x1-left,:3].astype(np.int16)
 hi=roi.max(axis=2);lo=roi.min(axis=2)
 neutral=(lo>55)&((hi-lo)<60)
 cyan=(roi[:,:,1]>100)&(roi[:,:,2]>100)&(roi[:,:,0]<roi[:,:,1]*.7)
 red=(roi[:,:,0]>140)&(roi[:,:,1]<roi[:,:,0]*.6)&(roi[:,:,2]<roi[:,:,0]*.65)
 legacy=((hi<140)&(roi[:,:,1]>roi[:,:,0]*1.15)) if n in ['r_menu','sl_parts'] else np.zeros(hi.shape,bool)
 ink=((neutral|cyan|red|legacy).astype(np.uint8)*255)
 # Dilate anti-alias/outline coverage, but keep the mask strictly inside the text region.
 ink=cv2.dilate(ink,np.ones((3,3),np.uint8))
 m[y0-top:y1-top,x0-left:x1-left]=ink
 rgb=cv2.inpaint(a[:,:,:3],m,3,cv2.INPAINT_TELEA)
 alpha=cv2.inpaint(a[:,:,3],m,3,cv2.INPAINT_TELEA)
 im.paste(Image.fromarray(np.dstack((rgb,alpha))), (left,top))
def text(n,b,s,size=16,state=0,vertical=False,clean=True,transparent=False,solid=None,logo=False):
 im=load(n);b=tuple(map(int,b));x0,y0,x1,y1=b;w=x1-x0;h=y1-y0
 if clean:erase(n,b,transparent,solid)
 mark(n,b,s)
 scale=3; f=font(size*scale)
 chars=list(s) if vertical else [s]
 if vertical:
  size=min(size,h/max(1,len(chars))-1,w-2);f=font(size*scale)
  draw=ImageDraw.Draw(Image.new('L',(1,1)))
  glyph=Image.new('L',(w*scale,h*scale));d=ImageDraw.Draw(glyph)
  for i,ch in enumerate(chars):
   bb=d.textbbox((0,0),ch,font=f);cw=bb[2]-bb[0];chh=bb[3]-bb[1]
   d.text(((w*scale-cw)/2-bb[0],(i+.5)*h*scale/len(chars)-chh/2-bb[1]),ch,font=f,fill=255,stroke_width=1)
 else:
  bb=f.getbbox(s);tw=bb[2]-bb[0];th=bb[3]-bb[1]
  glyph=Image.new('L',(max(1,tw+6),max(1,th+6)));d=ImageDraw.Draw(glyph)
  d.text((3-bb[0],3-bb[1]),s,font=f,fill=255,stroke_width=1)
  ratio=min(1,(w*scale-4)/glyph.width,(h*scale-4)/glyph.height)
  if ratio<1:glyph=glyph.resize((max(1,int(glyph.width*ratio)),max(1,int(glyph.height*ratio))),Image.Resampling.LANCZOS)
  full=Image.new('L',(w*scale,h*scale));full.paste(glyph,((full.width-glyph.width)//2,(full.height-glyph.height)//2));glyph=full
 glyph=glyph.resize((w,h),Image.Resampling.LANCZOS)
 layer=Image.new('RGBA',(w,h))
 if logo:
  # Mincho/Song, white outer contour, black inner contour, red or hollow grey fill.
  outer=glyph.filter(ImageFilter.MaxFilter(5));inner=glyph.filter(ImageFilter.MaxFilter(3))
  layer.paste((242,242,242,255),(0,0),outer);layer.paste((0,0,0,255),(0,0),inner)
  layer.paste((192,0,0,255) if state!=3 else (114,115,130,210),(0,0),glyph)
 else:
  shadow=glyph.filter(ImageFilter.MaxFilter(3))
  if n not in ['r_menu','sl_parts']:layer.paste((0,0,0,220),(0,0),shadow)
  if state in (1,2) and n not in ['r_menu','sl_parts']:
   glow=glyph.filter(ImageFilter.GaussianBlur(1.15));c=(0,240,255,255) if state==1 else (255,30,40,255)
   layer.paste(c,(0,0),glow)
  color=([(20,62,42,255),(90,24,64,255),(115,64,18,255),(95,95,95,255)][state] if n in ['r_menu','sl_parts'] else (242,242,238,255) if state!=3 else (112,112,112,255))
  layer.paste(color,(0,0),glyph)
 im.alpha_composite(layer,(x0,y0))
def cell(n,x,y,w,h,s,state=0,size=16,pad=7,transparent=False):
 text(n,(x+pad,y+2,x+w-pad,y+h-2),s,size,state,transparent=transparent)
def nav(n,cols=(80,80,124,100),labels=('上一页','下一页','返回标题','连续播放')):
 for state in range(4):
  x=0
  for w,s in zip(cols,labels):cell(n,x,24*state,w,24,s,state);x+=w
# Main title atlas; only button center regions are replaced, keeping key/alpha outside.
labels=['开始游戏','读取存档','CG鉴赏','剧情回想','音乐鉴赏','结局鉴赏']
for i,s in enumerate(labels):
 for state in range(3):
  # Original title buttons are glyphs on transparent black; replace the whole glyph footprint.
  text('title_pt',(0,i*108+state*36,408,i*108+state*36+36),s,24,state,transparent=True)
bg=Image.open(SRC/'title_bg.png').convert('RGBA')
for y,state in [(648,3),(768,0)]:
 load('title_pt').paste(bg.crop((28,108,612,228)),(0,y))
 text('title_pt',(0,y,584,y+120),'河原崎家的一族',98,state,clean=False,logo=True)
# Pause menu has both a composed two-row panel and repeated state strips.
first=[['保存存档','读取存档','文字履历','取消'],['隐藏文本框','环境设置','返回标题','退出游戏']]
for y,row in enumerate(first):
 for x,s in enumerate(row):cell('menu',x*192,y*36,192,36,s,0,18,pad=17)
for state in range(4):
 for i,s in enumerate(['保存存档','读取存档','文字履历','取消','显示文本框']):cell('menu',i*168,72+state*28,168,28,s,state)
 for i,s in enumerate(['隐藏文本框','环境设置','返回标题','退出游戏']):cell('menu',i*168,184+state*28,168,28,s,state)
text('menu',(0,296,380,372),'河原崎家的一族',58,solid=(255,255,255,255),logo=True)
# Album page backgrounds and atlas.
nav('cg_pt')
for n in ['cg_bg1','cg_bg2']:
 for x,w,s in [(64,80,'上一页'),(228,80,'下一页'),(328,100,'连续播放'),(452,124,'返回标题')]:cell(n,x,432,w,24,s)
 # Locked cards contain the game title; use identical treatment for all placeholders.
 for row in range(4):
  for col in range(4):
   x=66+132*col;y=24+100*row
   # Last-page layout only has 11 thumbnails; avoid drawing on empty frames.
   if n=='cg_bg2' and row*4+col>=11:continue
   text(n,(x+3,y+31,x+109,y+54),'河原崎家的一族',15,logo=True)
for i in range(1,9):nav(f'sc_pt{i:02}',(80,80,124),('上一页','下一页','返回标题'))
for x,w,s in [(64,80,'上一页'),(228,80,'下一页'),(452,124,'返回标题')]:cell('sc_bg',x,432,w,24,s)
# Save/load base panels are 624x464 and blitted at (8,8).
for n,s in [('sl_save','保存存档'),('sl_load','读取存档')]:
 text(n,(250,12,376,38),s,24)
 for page in range(4):text(n,(565,51+72*page,589,107+72*page),f'{page+1}页',17,vertical=True)
 text(n,(565,349,589,425),'关闭',18,vertical=True)
for p in range(4):
 for state in range(4):text('sl_pt',(112*p+6+28*state,6,112*p+23+28*state,62),f'{p+1}页',16,state,vertical=True)
for state in range(4):text('sl_pt',(448+28*state+6,7,448+28*state+23,81),'关闭',16,state,vertical=True)
text('sl_pt',(160,64,244,80),'空存档',15,solid=(0,0,0,255))
text('sl_pt',(48,170,335,191),'确定保存存档吗？',16)
text('sl_pt',(395,172,650,194),'确定读取存档吗？',16)
text('sl_pt',(48,277,335,300),'确定保存存档吗？',16)
for x,s in [(20,'编辑备注'),(136,'确定'),(252,'取消')]:cell('sl_pt',x,196,112,36,s)
for x,s in [(404,'确定'),(520,'取消')]:cell('sl_pt',x,204,112,36,s)
for x,s in [(20,'清空文字'),(136,'确定'),(252,'取消')]:cell('sl_pt',x,332,112,36,s)
for state in range(3):
 for x,s in [(384,'确定'),(488,'取消')]:text('sl_pt',(x+7,266+state*24,x+97,287+state*24),s,16,state,solid=(0,0,0,255))
 for x,s in [(0,'编辑备注'),(104,'清空文字'),(208,'确定'),(312,'取消')]:text('sl_pt',(x+7,394+state*24,x+97,415+state*24),s,16,state,solid=(0,0,0,255))
# Common modal atlases.
for n,prompt in [('endk','确定退出游戏吗？'),('bktitlen','确定返回标题吗？')]:
 text(n,(20,16,254,38),prompt,16)
 for x,s in [(20,'确定'),(136,'取消')]:cell(n,x,52,112,36,s)
 for state in range(3):
  for x,s in [(0,'确定'),(104,'取消')]:cell(n,x,108+24*state,104,24,s,state)
# Music titles and controls: 7 rows, 2 columns, 4 visual states.
tracks=['命运一族','侯爵千金','走下舞台的少女','通往毁灭的阶梯','觉醒','虚假的贵妇人','无尽的华尔兹','杀戮月夜','午后的露台','树荫','终焉','永恒的黑暗','噩梦','被撕裂的时光']
for i,s in enumerate(tracks):
 cell('soundk_bg',64+268*(i%2),36+52*(i//2),244,40,s,0,22,pad=10)
 for state in range(4):cell('soundk_pt',244*(i%2),160*(i//2)+40*state,244,40,s,state,22,pad=10)
for x,s in [(62,'停止'),(450,'返回标题')]:cell('soundk_bg',x,418,128,28,s)
for state in range(4):
 cell('soundk_pt',128*state,1120,128,28,'停止',state)
 cell('soundk_pt',128*state,1148,128,28,'返回标题',state)
cell('edk_bg',256,416,128,32,'返回标题')
for state in range(3):cell('edk_pt',128*state,0,128,32,'返回标题',state)
# Settings headers and label regions are independent of ornaments and sliders.
for b,s,size in [((23,24,164,54),'声音设置',24),((53,65,130,91),'音乐',19),((53,113,130,139),'音效',19),((53,161,130,187),'语音',19),((178,212,250,237),'男性语音',17),((396,212,468,237),'女性语音',17),((23,255,212,286),'文字显示设置',23),((55,301,246,322),'跳过已读文本',18),((53,349,139,372),'自动播放',18),((23,489,230,521),'画面效果设置',23),((55,533,219,557),'窗口颜色',19),((207,534,240,558),'红',19),((207,583,240,613),'绿',19),((207,632,240,665),'蓝',19),((188,696,241,725),'透明',18),((136,754,239,781),'预览',19),((52,810,169,838),'画面特效',18)]:text('setting',b,s,size)
for x,y in [(148,67),(148,115),(148,163),(260,211),(476,211),(260,301),(148,349),(175,811)]:
 text('setting',(x,y,x+26,y+22),'开',18)
 text('setting',(x+52,y,x+78,y+22),'关',18)
for y in [412,876]:
 for x,w,s in [(26,58,'上一页'),(84,58,'下一页'),(154,132,'恢复默认'),(298,82,'确定'),(392,82,'取消'),(486,80,'应用')]:cell('setting',x,y,w,30,s,0,16,pad=6)
for state in range(4):
 for x,w,s in [(0,52,'上一页'),(52,52,'下一页'),(104,128,'恢复默认'),(232,80,'确定'),(312,88,'取消'),(400,80,'应用'),(480,40,'开'),(520,32,'关')]:cell('setting',x,928+24*state,w,24,s,state,16,pad=5)
# Small speed/strength labels inside the original diamonds (keep the borders).
text('setting',(251,354,261,366),'慢',9,solid=(0,0,0,255))
for y in [544,600,656,712]:text('setting',(555,y-6,565,y+6),'浓',9,solid=(0,0,0,255))
# Legacy alternate menu / save atlas, included so PC variants are covered.
for i,s in enumerate(['保存存档','读取存档','文字履历','隐藏文本框','环境设置','返回标题','退出游戏']):text('r_menu',(53,42+44*i,391,68+44*i),s,22)
text('r_menu',(188,350,256,373),'关闭',19)
for i in range(3):text('r_menu',(85+i*90,489,161+i*90,511),'关闭',17,i)
for p in range(3):
 for state in range(4):text('sl_parts',(1+28*state,62+p*59,25+28*state,114+p*59),f'{p+1}页',15,state,vertical=True)
for state in range(4):text('sl_parts',(1+28*state,196,25+28*state,260),'关闭',16,state,vertical=True)
for state in range(3):
 for y,s in [(72,'编辑备注'),(100,'清空文字'),(128,'确定'),(156,'取消')]:text('sl_parts',(412+72*state,y+3,474+72*state,y+22),s,13,state)
text('sl_parts',(130,148,387,166),'确定保存存档吗？',15)
for x,s in [(139,'清空文字'),(221,'确定'),(302,'取消')]:text('sl_parts',(x,205,x+65,224),s,14)
text('sl_parts',(6,245,363,263),'确定保存存档吗？',15)
text('sl_parts',(374,245,625,263),'确定读取存档吗？',15)
# Credit animation sheets: preserve original frame grid and key-color areas.
# Role/name spelling follows translations/zh_CN.txt, not OCR output.
credits=[('角色介绍',''),('佣人','斋藤 六郎'),('女仆','古手川 美佐子'),('家庭教师','安藤 香织'),('河原崎家次女','河原崎 丽'),('女仆','仓田 亚栗栖'),('河原崎家夫人','河原崎 京子'),('河原崎家长女','河原崎 幸子'),('幸子的同学','朝仓 美奈'),('司机','南原 正明'),('河原崎家长男','河原崎 俊介'),('武士宅邸的住人','神秘老人')]
def credit_glyph(role,name,w=376,h=192):
 mask=Image.new('L',(w,h));d=ImageDraw.Draw(mask)
 for text_,y,size in [(role,70 if name else 83,20 if name else 32),(name,109,28)]:
  if not text_:continue
  f=font(size);bb=f.getbbox(text_);x=(w-(bb[2]-bb[0]))/2-bb[0];d.text((x,y-bb[1]),text_,font=f,fill=255)
 return mask
def credit_color(m):
 out=Image.new('RGBA',m.size,(0,0,0,255));glow=m.filter(ImageFilter.GaussianBlur(2));out.paste((30,190,220,255),(0,0),glow);out.paste((232,255,255,255),(0,0),m);return out
for i,(role,name) in enumerate(credits):
 n=f'end_staff{i:02}';im=load(n);base=credit_glyph(role,name)
 for j in range(6):
  # Text rises from blurred to clear, the original six 376x92 cells.
  m=base.crop((94,54,282,146)).filter(ImageFilter.GaussianBlur([6,4,3,2,1,0][j]))
  m=m.point(lambda a,f=[.25,.4,.55,.7,.85,1][j]:int(a*f))
  im.paste(credit_color(m),(0,j*92));mark(n,(0,j*92,188,(j+1)*92),role+' '+name)
 for j in range(9):
  h=200 if j<3 else 192;cx,cy=188,h/2
  a=np.array(base.resize((376,h)));yy,xx=np.indices((h,376),dtype=np.float32);dx=xx-cx;dy=yy-cy
  radius=np.sqrt(dx*dx+dy*dy);theta=np.arctan2(dy,dx)
  strength=(j+1)*1.2;angle=theta-strength*np.exp(-radius/90)
  mx=(cx+radius*np.cos(angle)).astype(np.float32);my=(cy+radius*np.sin(angle)).astype(np.float32)
  warped=cv2.remap(a,mx,my,cv2.INTER_CUBIC,borderMode=cv2.BORDER_CONSTANT)
  warped=(warped.astype(float)*max(.1,1-j*.1)).astype('uint8')
  tile=credit_color(Image.fromarray(warped))
  x,y=(0,552+j*200) if j<3 else (376,(j-3)*192)
  im.paste(tile,(x,y));mark(n,(x,y,x+376,y+h),role+' '+name+' [swirl frame]')
# Save immutable originals alongside reproducible outputs + per-pixel edit audit.
report={}
for n,im in IM.items():
 im.save(DST/(n+'.png'))
 original=np.array(Image.open(SRC/(n+'.png')).convert('RGBA'));out=np.array(im)
 changed=np.any(original!=out,axis=2);allowed=np.array(MASKS[n])>0
 assert not np.any(changed & ~allowed),n+' edited outside manifest rectangles'
 report[n]={'size':list(im.size),'operations':len(OPS[n]),'changed_pixels':int(changed.sum()),'outside_rect_changes':0,'status':'rendered'}
 MASKS[n].save(ROOT/'review'/(n+'.edit-mask.png'))
(ROOT/'制作坐标.json').write_text(json.dumps(OPS,ensure_ascii=False,indent=2))
(ROOT/'像素验收.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
print('Rendered',len(report),'images;',sum(r['operations'] for r in report.values()),'text/frame regions')
