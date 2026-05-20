import json

with open(r'E:\Personal\sEMG_Project\mini_program\project.config.json', 'r', encoding='utf-8') as f:
    cfg = json.load(f)

# Print all keys
for k, v in cfg.items():
    if k != 'setting':
        print(f'{k}: {v}')
    else:
        print(f'setting: {json.dumps(v, indent=2, ensure_ascii=False)}')
