import sys
sys.stdout.reconfigure(encoding='utf-8')

path = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxml'
with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Find resting/max_contraction block and check tag balance
in_block = False
view_depth = 0
for i, line in enumerate(lines):
    if "resting" in line and "max_contraction" in line and "wx:if" in line:
        in_block = True
        print(f"Block starts at line {i+1}")
    
    if in_block:
        o = line.count('<view')
        c = line.count('</view')
        view_depth += o - c
        
        if '<block' in line or '</block>' in line:
            print(f"  L{i+1}: depth={view_depth} | {line.rstrip()[:80]}")
        elif o > 0 or c > 0:
            print(f"  L{i+1}: depth={view_depth}} (o={o},c={c}) | {line.rstrip()[:70]}")
        
        if '</block>' in line:
            print(f"Block ends at line {i+1}, final view_depth={view_depth}")
            break
