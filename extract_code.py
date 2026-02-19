import os
import re

with open('C++.md', 'r', encoding='utf-8') as f:
    content = f.read()

pattern = r'## 文件: `([^`]+)`\s+```\w*\s+(.*?)\s+```'
matches = re.findall(pattern, content, re.DOTALL)

for filepath, code in matches:
    # Cleanup filepath and normalize slashes
    filepath = filepath.strip().replace('\\', '/')
    # Handle directories
    dir_path = os.path.dirname(filepath)
    if dir_path and not os.path.exists(dir_path):
        os.makedirs(dir_path)
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(code)
    print(f'Extracted: {filepath}')
