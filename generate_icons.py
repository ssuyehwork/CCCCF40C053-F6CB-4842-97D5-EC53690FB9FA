import sys

# 分类定义
categories = {
    "#2196F3": ["jpg", "jpeg", "png", "gif", "bmp", "tif", "tiff", "webp", "heic", "heif", "ico", "raw", "psd", "ai", "eps", "indd", "xcf", "svg", "pdf", "wmf", "emf"],
    "#FF9800": ["mp3", "wav", "flac", "aac", "m4a", "ogg", "wma", "aiff", "mid", "midi"],
    "#F44336": ["mp4", "avi", "mkv", "mov", "wmv", "flv", "f4v", "webm", "3gp", "ts", "m4v"],
    "#009688": ["txt", "doc", "docx", "rtf", "odt", "wps"],
    "#4CAF50": ["xls", "xlsx", "ods", "csv", "ppt", "pptx", "odp"],
    "#795548": ["epub", "mobi", "azw", "djvu", "ps", "chm"],
    "#3F51B5": ["tex", "bib", "md", "rst", "log"],
    "#9C27B0": ["exe", "msi", "apk", "ipa", "bin", "app"],
    "#00BCD4": ["bat", "cmd", "sh", "ps1", "vbs", "js", "ts"],
    "#FF5722": ["c", "cpp", "h", "cs", "java", "py", "rb", "php", "rs", "go", "swift", "kt"],
    "#607D8B": ["ini", "cfg", "json", "xml", "yaml", "yml", "env"]
}

template = '        {"ext_%s", R"svg(<svg viewBox="0 0 24 24" fill="%s"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" /><path d="M14 2v6h6z" fill="rgba(255,255,255,0.3)" /><text x="12" y="16.5" fill="white" font-size="6" font-weight="bold" text-anchor="middle" font-family="Arial, sans-serif">%s</text></svg>)svg"},'

all_exts = {}
for color, exts in categories.items():
    for ext in exts:
        all_exts[ext] = color

sorted_exts = sorted(all_exts.keys())

for ext in sorted_exts:
    print(template % (ext, all_exts[ext], ext.upper()))
