import json

with open(r'E:\Personal\sEMG_Project\mini_program\app.json', 'r', encoding='utf-8') as f:
    app = json.load(f)

pages = app.get('pages', [])
print('Registered pages:')
for p in pages:
    print(f'  {p}')

has_calib = any('calibrate' in p for p in pages)
print(f'\ncalibrate registered: {has_calib}')

# Check for usingComponents
calib_json_path = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.json'
import os
if os.path.exists(calib_json_path):
    with open(calib_json_path, 'r', encoding='utf-8') as f:
        cj = json.load(f)
    print(f'\ncalibrate index.json: {cj}')
else:
    print('\nNo calibrate/index.json found')
