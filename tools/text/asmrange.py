import sys,struct,re
from pathlib import Path
b=Path('/Users/rujian/Documents/DeepSeek/KAWAXP/AI.exe').read_bytes()
lines=Path('work/ai.asm').read_text().splitlines()
for arg in sys.argv[1:]:
 a=int(arg,16)
 if b[a-0x400000]==0xe9:a+=5+struct.unpack_from('<i',b,a-0x400000+1)[0]
 print('FUNCTION',hex(a))
 found=False
 count=0
 for l in lines:
  m=re.match(r'  ([0-9a-f]+):',l)
  if not m:continue
  addr=int(m[1],16)
  if addr<a:continue
  if '\tint3' in l:break
  print(l);count+=1
  if count>=230:break
