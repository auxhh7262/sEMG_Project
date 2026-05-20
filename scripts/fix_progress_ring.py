import re

path = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxml'
with open(path, 'r', encoding='utf-8') as f:
    c = f.read()

# Remove progress ring blocks (comment + wrapper view + all content inside)
pat = r'[\s]*<!-- 进度圆环 -->[\s\S]*?<view class="progress-ring-wrapper">[\s\S]*?</view>'
m = re.findall(pat, c)
print(f'Found {len(m)} progress ring blocks')
for b in m:
    print(f'  len={len(b)}')

c2 = re.sub(pat, '', c)

# Also clean up any orphaned canvas/ring lines that might remain
lines = c2.split('\n')
cleaned = []
for line in lines:
    stripped = line.strip()
    if any(x in stripped for x in ['progressRing', 'progress-ring', 'ring-center', 'ring-percent']):
        continue
    cleaned.append(line)
c3 = '\n'.join(cleaned)

with open(path, 'w', encoding='utf-8') as f:
    f.write(c3)

print(f'Done! {len(c3)} bytes, removed {len(lines)-len(cleaned)} total lines')
