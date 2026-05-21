import sys

f = open(r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp', 'r', encoding='utf-8')
lines = f.readlines()
f.close()

# Find and replace the "no data" branch (around line 306-308)
for i in range(len(lines)):
    if 'points\":[]}' in lines[i] and 'cz_data' in lines[i] and 'has_curve' not in lines[i]:
        # Replace this line to add has_curve
        old_line = lines[i]
        indent = old_line[:len(old_line) - len(old_line.lstrip())]
        # Insert PersonalCalibData lines before the sendJsonTo line
        new_lines = [
            indent + 'PersonalCalibData_t calib = {0};\n',
            indent + 'bool hc = gStorage.GetPersonalCalib(&calib) && calib.has_curve;\n',
            indent + 'char buf[128];\n',
        ]
        # Replace the sendJsonTo line
        lines[i] = indent + 'snprintf(buf, sizeof(buf), "{\"cmd\":\"cz_data\",\"points\":[],\"has_curve\":%d}", hc ? 1 : 0);\n'
        lines.insert(i+1, indent + 'gNetManager.sendJsonTo(clientNum, buf);\n')
        # Insert the new lines before the snprintf line
        for j, nl in enumerate(new_lines):
            lines.insert(i + j, nl)
        break

# Now add has_curve to the batch response with data
for i in range(len(lines)):
    if 'doc["cmd"] = "cz_data";' in lines[i] and 'has_curve' not in lines[i]:
        indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
        insert_lines = [
            indent + '// [Bug fix] has_curve from A-zone\n',
            indent + 'PersonalCalibData_t calibInfo = {0};\n',
            indent + 'bool hasC = gStorage.GetPersonalCalib(&calibInfo) && calibInfo.has_curve;\n',
            indent + 'doc["has_curve"] = hasC ? 1 : 0;\n',
        ]
        for j, nl in enumerate(insert_lines):
            lines.insert(i + 1 + j, nl)
        break

f = open(r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp', 'w', encoding='utf-8')
f.writelines(lines)
f.close()
print('Done - patched handleQueryCZ')
