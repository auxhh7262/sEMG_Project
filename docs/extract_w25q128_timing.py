import pdfplumber

pdf_path = r'E:\Personal\资料\资料\W25Q128\C113767_NOR+FLASH_W25Q128JVSIQ_规格书_WJ71451.PDF'
output_path = r'E:\Personal\sEMG_Project\docs\W25Q128JV_AC_timing.txt'

with pdfplumber.open(pdf_path) as pdf:
    # 提取第63-65页 (Section 9.5 AC Electrical Characteristics)
    with open(output_path, 'w', encoding='utf-8') as f:
        for i in range(62, min(65, len(pdf.pages))):
            page = pdf.pages[i]
            text = page.extract_text()
            if text:
                f.write(f'=== PDF Page {i+1} (index {i}) ===\n')
                f.write(text)
                f.write('\n\n')
        
        print(f'Extracted to: {output_path}')

# 读取输出
with open(output_path, 'r', encoding='utf-8') as f:
    content = f.read()
    print(content[:5000])  # 打印前5000字符
