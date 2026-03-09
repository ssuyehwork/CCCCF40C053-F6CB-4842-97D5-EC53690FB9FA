import sys
import os
import re

def extract_file(md_path, file_marker):
    with open(md_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # We look for the section starting with ## 文件: `file_marker`
    # and then the FIRST code block following it.
    section_pattern = rf"## 文件: `{re.escape(file_marker)}`.*?\n\n```(?:cpp)?\n(.*?)\n```"
    match = re.search(section_pattern, content, re.DOTALL)

    if match:
        return match.group(1).strip()
    return None

if __name__ == "__main__":
    md_file = "旧版本-1.md"
    files_to_extract = [
        "src/main.cpp",
        "src/models/NoteModel.cpp",
        "src/ui/StringUtils.h",
        "src/core/HotkeyManager.cpp"
    ]

    for f_path in files_to_extract:
        code = extract_file(md_file, f_path)
        if code:
            # Ensure the directory exists
            os.makedirs(os.path.dirname(f_path), exist_ok=True)
            with open(f_path, 'w', encoding='utf-8') as f:
                f.write(code + '\n')
            print(f"Cleanly restored {f_path}")
        else:
            print(f"Failed to find {f_path} in {md_file}")

    # Also make sure HotkeyManager.h is correct as per the md file (just in case)
    code_h = extract_file(md_file, "src/core/HotkeyManager.h")
    if code_h:
        with open("src/core/HotkeyManager.h", 'w', encoding='utf-8') as f:
            f.write(code_h + '\n')
        print("Cleanly restored src/core/HotkeyManager.h")
