import re

wpath = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxml'
xpath = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxss'
jspath = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.js'

with open(wpath, 'r', encoding='utf-8') as f:
    w = f.read()
with open(xpath, 'r', encoding='utf-8') as f:
    x = f.read()
with open(jspath, 'r', encoding='utf-8') as j:
    js = j.read()

print('=== WXML tag balance ===')
for tag in ['view', 'text', 'picker', 'image', 'button', 'input', 'scroll-view']:
    o = len(re.findall(rf'<{tag}[\s>]', w))
    c = len(re.findall(rf'</{tag}>', w))
    status = 'OK' if o == c else f'MISMATCH!'
    print(f'  {tag}: {o} open / {c} close  {status}')

print('\n=== WXSS braces ===')
print(f'  open={x.count("{")} close={x.count("}")}')

print('\n=== JS balance ===')
print(f'  braces: {js.count("{")}/{js.count("}")}  parens: {js.count("(")}/{js.count(")")}')
