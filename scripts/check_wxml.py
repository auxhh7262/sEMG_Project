import re

filepath = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxml'
with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Check wx:if bindings
print("=== wx:if bindings ===")
for m in re.finditer(r'wx:if="([^"]+)"', content):
    print(f"  {m.group(1)}")

# Check for unclosed tags globally
opens = len(re.findall(r'<view[\s>]', content))
closes = len(re.findall(r'</view>', content))
print(f"\nGlobal view tags: {opens} open, {closes} close, diff={opens-closes}")

# Check picker closing
pickers = len(re.findall(r'<picker', content))
picker_closes = len(re.findall(r'</picker>', content))
print(f"Picker tags: {pickers} open, {picker_closes} close")

# Check input tags (self-closing)
inputs = len(re.findall(r'<input\s', content))
input_closes = len(re.findall(r'</input>', content))
print(f"Input tags: {inputs} (self-closing), {input_closes} with close tag")
