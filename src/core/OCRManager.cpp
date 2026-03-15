#include "OCRManager.h"
#include <QtConcurrent>
#include <QThreadPool>
#include <QStringList>
#include <QTemporaryFile>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <QLocale>
#include <QCoreApplication>
#include <QBuffer>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Ocr;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;
#endif

OCRManager& OCRManager::instance() {
    static OCRManager inst;
    return inst;
}

OCRManager::OCRManager(QObject* parent) : QObject(parent) {
    detectAvailableEngine();
}

void OCRManager::setLanguage(const QString& lang) {
    m_language = lang;
}

QString OCRManager::getLanguage() const {
    return m_language;
}

void OCRManager::detectAvailableEngine() {
    QString path = findTesseractPath();
    if (!path.isEmpty()) {
        m_engineType = EngineType::Tesseract;
        m_cachedTesseractPath = path;
        qDebug() << "[OCRManager] 检测到 Tesseract 引擎，路径:" << path;
    } else {
        m_engineType = EngineType::WindowsOCR;
        qDebug() << "[OCRManager] 未找到 Tesseract，回退到 Windows 原生 OCR";
    }
}

QString OCRManager::findTesseractPath() {
    QString appPath = QCoreApplication::applicationDirPath();
    QStringList basePaths;
    basePaths << appPath;
    basePaths << QDir(appPath).absolutePath() + "/..";
    basePaths << QDir(appPath).absolutePath() + "/../..";

    for (const QString& base : std::as_const(basePaths)) {
        QStringList exePotentials;
        exePotentials << base + "/resources/Tesseract-OCR/tesseract.exe"
                       << base + "/Tesseract-OCR/tesseract.exe"
                       << base + "/resources/tesseract.exe"
                       << base + "/tesseract.exe"
                       << "C:/Program Files/Tesseract-OCR/tesseract.exe";
        for (const QString& p : std::as_const(exePotentials)) {
            if (QFile::exists(p)) {
                return QDir::toNativeSeparators(p);
            }
        }
    }
    return "";
}

void OCRManager::recognizeAsync(const QImage& image, int contextId) {
    qDebug() << "[OCRManager] recognizeAsync: 接收任务 ID:" << contextId 
             << "图片大小:" << image.width() << "x" << image.height() 
             << "主线程:" << QThread::currentThread();
    (void)QtConcurrent::run(QThreadPool::globalInstance(), [this, image, contextId]() {
        qDebug() << "[OCRManager] 工作线程开始执行 ID:" << contextId 
                 << "线程:" << QThread::currentThread();
        this->recognizeSync(image, contextId);
        qDebug() << "[OCRManager] 工作线程完成 ID:" << contextId;
    });
}

// 图像预处理函数：提高 OCR 识别准确度
QImage OCRManager::preprocessImage(const QImage& original) {
    if (original.isNull()) {
        return original;
    }
    
    // 1. 转换为灰度图 (保留 8 位深度的灰度细节，这对 Tesseract 4+ 至关重要)
    QImage processed = original.convertToFormat(QImage::Format_Grayscale8);
    
    // 2. 动态缩放策略
    // 目标：使文字像素高度达到 Tesseract 偏好的 30-35 像素。
    // 如果原图已经很大（宽度 > 2000），则不需要放大 3 倍，否则会导致内存占用过高且识别变慢
    int scale = 3;
    if (processed.width() > 2000 || processed.height() > 2000) {
        scale = 1;
    } else if (processed.width() > 1000 || processed.height() > 1000) {
        scale = 2;
    }
    
    if (scale > 1) {
        int targetW = processed.width() * scale;
        int targetH = processed.height() * scale;
        
        // [OPTIMIZATION] OCR 预处理像素红线保护：禁止缩放后超过 4000 像素，防止卷积运算引发 OOM
        if (targetW > 4000 || targetH > 4000) {
            QSize sz(targetW, targetH);
            sz.scale(4000, 4000, Qt::KeepAspectRatio);
            targetW = sz.width();
            targetH = sz.height();
        }

        processed = processed.scaled(
            targetW, 
            targetH, 
            Qt::KeepAspectRatio, 
            Qt::SmoothTransformation
        );
    }
    
    // 3. 自动反色处理：Tesseract 在白底黑字下表现最好
    // 简单判断：如果四个角的像素平均值较暗，则认为可能是深色背景
    int cornerSum = 0;
    cornerSum += qGray(processed.pixel(0, 0));
    cornerSum += qGray(processed.pixel(processed.width()-1, 0));
    cornerSum += qGray(processed.pixel(0, processed.height()-1));
    cornerSum += qGray(processed.pixel(processed.width()-1, processed.height()-1));
    if (cornerSum / 4 < 128) {
        processed.invertPixels();
    }

    // 4. 增强对比度（线性拉伸）
    // 泰语等细笔画文字对二值化和过度的对比度拉伸很敏感，因此我们收窄忽略范围（从 1% 降至 0.5%）
    int histogram[256] = {0};
    for (int y = 0; y < processed.height(); ++y) {
        const uchar* line = processed.constScanLine(y);
        for (int x = 0; x < processed.width(); ++x) {
            histogram[line[x]]++;
        }
    }
    
    int totalPixels = processed.width() * processed.height();
    int minGray = 0, maxGray = 255;
    int count = 0;
    
    for (int i = 0; i < 256; ++i) {
        count += histogram[i];
        if (count > totalPixels * 0.005) {
            minGray = i;
            break;
        }
    }
    
    count = 0;
    for (int i = 255; i >= 0; --i) {
        count += histogram[i];
        if (count > totalPixels * 0.005) {
            maxGray = i;
            break;
        }
    }
    
    if (maxGray > minGray) {
        for (int y = 0; y < processed.height(); ++y) {
            uchar* line = processed.scanLine(y);
            for (int x = 0; x < processed.width(); ++x) {
                int val = line[x];
                val = (val - minGray) * 255 / (maxGray - minGray);
                val = qBound(0, val, 255);
                line[x] = static_cast<uchar>(val);
            }
        }
    }

    // 5. 简单锐化处理 (卷积)
    // 增加文字边缘对比度，有助于 Tesseract 识别彩色变灰度后的细微笔画
    QImage sharpened = processed;
    int kernel[3][3] = {
        {0, -1, 0},
        {-1, 5, -1},
        {0, -1, 0}
    };
    for (int y = 1; y < processed.height() - 1; ++y) {
        const uchar* prevLine = processed.constScanLine(y - 1);
        const uchar* currLine = processed.constScanLine(y);
        const uchar* nextLine = processed.constScanLine(y + 1);
        uchar* destLine = sharpened.scanLine(y);
        for (int x = 1; x < processed.width() - 1; ++x) {
            int sum = currLine[x] * kernel[1][1]
                    + prevLine[x] * kernel[0][1] + nextLine[x] * kernel[2][1]
                    + currLine[x-1] * kernel[1][0] + currLine[x+1] * kernel[1][2];
            destLine[x] = static_cast<uchar>(qBound(0, sum, 255));
        }
    }
    processed = sharpened;
    
    // 注意：不再手动调用 Otsu 二值化。
    // Tesseract 4.0+ 内部的二值化器（基于 Leptonica）在处理具有抗锯齿边缘的灰度图像时表现更好。
    
    return processed;
}

void OCRManager::recognizeSync(const QImage& image, int contextId) {
    qDebug() << "[OCRManager] recognizeSync: 开始识别 ID:" << contextId 
             << "线程:" << QThread::currentThread();

    QString result;

#ifdef Q_OS_WIN
    // 优先采用 Tesseract (如果探测到可用)
    if (m_engineType == EngineType::Tesseract) {
        result = recognizeWithTesseract(image);
    } else {
        result = recognizeWithWindowsOCR(image);
    }
#else
    result = "当前平台不支持 OCR 功能";
#endif

    if (result.isEmpty()) {
        result = "未能从图片中识别出任何文字";
    }
    
    qDebug() << "[OCRManager] recognizeSync: 识别完成 ID:" << contextId
             << "结果长度:" << result.length() << "线程:" << QThread::currentThread();
    emit recognitionFinished(result, contextId);
}

QString OCRManager::recognizeWithTesseract(const QImage& image) {
    QString result;
    // 预处理图像以提高识别准确度
    QImage processedImage = preprocessImage(image);
    if (processedImage.isNull()) return "图像无效";

    QTemporaryFile tempFile(QDir::tempPath() + "/ocr_XXXXXX.bmp");
    tempFile.setAutoRemove(true);
    if (!tempFile.open()) return "无法创建临时图像文件";
    
    QString filePath = QDir::toNativeSeparators(tempFile.fileName());
    if (!processedImage.save(filePath, "BMP")) return "无法保存临时图像文件";
    tempFile.close();

    QString tesseractPath = m_cachedTesseractPath;
    if (tesseractPath.isEmpty()) tesseractPath = findTesseractPath();
    if (tesseractPath.isEmpty()) return "未找到 Tesseract 引擎";

    QString tessDataPath;
    QString appPath = QCoreApplication::applicationDirPath();
    QStringList basePaths = { appPath, QDir(appPath).absolutePath() + "/..", QDir(appPath).absolutePath() + "/../.." };
    for (const QString& base : basePaths) {
        QStringList dataPotentials = { base + "/resources/Tesseract-OCR/tessdata", base + "/Tesseract-OCR/tessdata", base + "/tessdata" };
        for (const QString& p : dataPotentials) { if (QDir(p).exists()) { tessDataPath = QDir(p).absolutePath(); break; } }
        if (!tessDataPath.isEmpty()) break;
    }

    QProcess tesseract;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!tessDataPath.isEmpty()) {
        QDir prefixDir(tessDataPath); prefixDir.cdUp();
        env.insert("TESSDATA_PREFIX", QDir::toNativeSeparators(prefixDir.absolutePath()));
    }
    tesseract.setProcessEnvironment(env);

    QStringList foundLangs;
    if (!tessDataPath.isEmpty()) {
        QDir dir(tessDataPath);
        QStringList files = dir.entryList({"*.traineddata"}, QDir::Files);
        QStringList priority = QLocale::system().script() == QLocale::TraditionalChineseScript ?
            QStringList{"chi_tra", "tha", "eng", "chi_sim"} : QStringList{"chi_sim", "tha", "eng", "chi_tra"};
        for (const QString& pLang : priority) { if (files.contains(pLang + ".traineddata")) foundLangs << pLang; }
    }
    QString currentLang = foundLangs.isEmpty() ? m_language : foundLangs.join('+');

    QStringList args;
    if (!tessDataPath.isEmpty()) args << "--tessdata-dir" << QDir::toNativeSeparators(tessDataPath);
    args << filePath << "stdout" << "-l" << currentLang << "--oem" << "1" << "--psm" << "3";

    tesseract.start(tesseractPath, args);
    if (tesseract.waitForFinished(20000)) {
        result = QString::fromUtf8(tesseract.readAllStandardOutput()).trimmed();
    }
    return result;
}

// Windows 原生 OCR 实现 (C++/WinRT)
QString OCRManager::recognizeWithWindowsOCR(const QImage& image) {
    qDebug() << "[OCRManager] 正在启动 Windows 原生 OCR 引擎...";

    QString resultText;
    try {
        // 1. 初始化 WinRT 线程环境 (由于在后台线程运行，必须确保初始化)
        winrt::init_apartment();

        // 2. 将 QImage 转换为内存流 (PNG 格式作为中转)
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        
        // 3. 将数据写入 WinRT 随机访问流
        InMemoryRandomAccessStream stream;
        DataWriter writer(stream);
        writer.WriteBytes(winrt::array_view<const uint8_t>(reinterpret_cast<const uint8_t*>(ba.constData()), ba.size()));
        writer.StoreAsync().get();
        writer.FlushAsync().get();
        stream.Seek(0);

        // 4. 解码位图
        BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
        SoftwareBitmap bitmap = decoder.GetSoftwareBitmapAsync().get();

        // 5. 语言选择：尝试匹配系统语言或 Tesseract 设定的首选语言
        OcrEngine engine = nullptr;
        if (m_language.contains("chi")) {
            engine = OcrEngine::TryCreateFromLanguage(Windows::Globalization::Language(L"zh-Hans-CN"));
        } else if (m_language.contains("tha")) {
            engine = OcrEngine::TryCreateFromLanguage(Windows::Globalization::Language(L"th-TH"));
        }
        
        if (!engine) {
            engine = OcrEngine::TryCreateFromUserProfileLanguages();
        }

        if (!engine) {
            return "无法创建 Windows OCR 引擎，请检查系统语言包是否安装。";
        }

        // 6. 执行识别
        OcrResult ocrResult = engine.RecognizeAsync(bitmap).get();
        resultText = QString::fromWCharArray(ocrResult.Text().c_str());

        qDebug() << "[OCRManager] Windows OCR 识别成功，文本长度:" << resultText.length();

    } catch (winrt::hresult_error const& ex) {
        QString errMsg = QString::fromWCharArray(ex.message().c_str());
        qDebug() << "[OCRManager] Windows OCR 运行异常:" << errMsg;
        resultText = "Windows OCR 错误: " + errMsg;
    } catch (...) {
        qDebug() << "[OCRManager] Windows OCR 出现未知错误";
        resultText = "Windows OCR 失败 (未知原因)";
    }

    return resultText;
}