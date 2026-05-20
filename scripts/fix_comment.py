# Remove test comment and try a minimal valid WXML
p = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxss'
with open(p, 'r', encoding='utf-8') as f:
    c = f.read()
c = c.replace('\n/* test */', '')
with open(p, 'w', encoding='utf-8') as f:
    f.write(c)
print('Removed test comment')
