import os
import re

def find_emojis(directory):
    emoji_pattern = re.compile(r'[\U00010000-\U0010ffff\u2600-\u27bf\u2b50\u2b06\u2192\u2705\u274c\u2714\u2716\u2605\u2606]')
    results = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(('.cpp', '.h')):
                path = os.path.join(root, file)
                with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    matches = emoji_pattern.findall(content)
                    if matches:
                        results.append((path, list(set(matches))))
    return results

if __name__ == "__main__":
    for path, matches in find_emojis("src"):
        print(f"{path}: {' '.join(matches)}")
