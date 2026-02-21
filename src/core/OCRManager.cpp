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
#include <utility>

OCRManager& OCRManager::instance() {
    static OCRManager inst;
    return inst;
}

OCRManager::OCRManager(QObject* parent) : QObject(parent) {}

void OCRManager::setLanguage(const QString& lang) {
    m_language = lang;
}

QString OCRManager::getLanguage() const {
    return m_language;
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
        processed = processed.scaled(
            processed.width() * scale, 
            processed.height() * scale, 
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
    // 预处理图像以提高识别准确度
    QImage processedImage = preprocessImage(image);
    
    if (processedImage.isNull()) {
        result = "图像无效";
        emit recognitionFinished(result, contextId);
        return;
    }
    
    // 使用 BMP 格式存储临时文件，因为其写入速度最快且不涉及复杂的压缩计算，能缩短毫秒级开销
    QTemporaryFile tempFile(QDir::tempPath() + "/ocr_XXXXXX.bmp");
    tempFile.setAutoRemove(true);
    
    if (!tempFile.open()) {
        result = "无法创建临时图像文件";
        emit recognitionFinished(result, contextId);
        return;
    }
    
    QString filePath = QDir::toNativeSeparators(tempFile.fileName());
    
    // 保存预处理后的灰度图
    if (!processedImage.save(filePath, "BMP")) {
        result = "无法保存临时图像文件";
        emit recognitionFinished(result, contextId);
        return;
    }
    
    tempFile.close();

    // 路径探测逻辑：增强鲁棒性，支持从 bin 或 build 目录运行
    QString appPath = QCoreApplication::applicationDirPath();
    QString tessDataPath;
    QString tesseractPath;

    // 尝试在多个层级寻找 resources 目录
    QStringList basePaths;
    basePaths << appPath;
    basePaths << QDir(appPath).absolutePath() + "/..";
    basePaths << QDir(appPath).absolutePath() + "/../..";

    for (const QString& base : std::as_const(basePaths)) {
        // 搜索数据目录 (支持资源目录、根目录及标准安装路径)
        if (tessDataPath.isEmpty()) {
            QStringList dataPotentials;
            dataPotentials << base + "/resources/Tesseract-OCR/tessdata"
                           << base + "/Tesseract-OCR/tessdata"
                           << base + "/tessdata"
                           << "C:/Program Files/Tesseract-OCR/tessdata";
            for (const QString& p : std::as_const(dataPotentials)) {
                if (QDir(p).exists()) {
                    tessDataPath = QDir(p).absolutePath();
                    break;
                }
            }
        }
        
        // 搜索执行文件
        if (tesseractPath.isEmpty()) {
            QStringList exePotentials;
            exePotentials << base + "/resources/Tesseract-OCR/tesseract.exe"
                           << base + "/Tesseract-OCR/tesseract.exe"
                           << base + "/resources/tesseract.exe"
                           << base + "/tesseract.exe"
                           << "C:/Program Files/Tesseract-OCR/tesseract.exe";
            for (const QString& p : std::as_const(exePotentials)) {
                if (QFile::exists(p)) {
                    tesseractPath = QDir::toNativeSeparators(p);
                    break;
                }
            }
        }
        
        if (!tessDataPath.isEmpty() && !tesseractPath.isEmpty()) break;
    }

    // 系统 PATH 兜底
    if (tesseractPath.isEmpty()) tesseractPath = "tesseract";

    if (!tesseractPath.isEmpty()) {
        QProcess tesseract;
        
        // 设置 TESSDATA_PREFIX 环境变量（Tesseract 主程序所在的父目录或 tessdata 所在目录）
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!tessDataPath.isEmpty() && QDir(tessDataPath).exists()) {
            // TESSDATA_PREFIX 通常应该是包含 tessdata 文件夹的目录
            QDir prefixDir(tessDataPath);
            prefixDir.cdUp();
            env.insert("TESSDATA_PREFIX", QDir::toNativeSeparators(prefixDir.absolutePath()));
        }
        tesseract.setProcessEnvironment(env);
        
        // 智能语言探测：自动扫描 tessdata 目录下所有可用的训练数据
        QStringList foundLangs;
        if (!tessDataPath.isEmpty()) {
            QDir dir(tessDataPath);
            if (dir.exists()) {
            QStringList filters; filters << "*.traineddata";
            QStringList files = dir.entryList(filters, QDir::Files);
            
            // 定义优先级：优先加载中文简体和泰语，防止误识别为英文字符 (如 星号 -> BE)
            QStringList priority;
            QString systemName = QLocale::system().name();
            // 严谨判断简繁：只有明确匹配到港澳台地区或传统脚本时才视为繁体
            bool isTraditional = (systemName.contains("zh_TW") || systemName.contains("zh_HK") || 
                                 systemName.contains("zh_MO") || 
                                 QLocale::system().script() == QLocale::TraditionalChineseScript);
            
            if (isTraditional) {
                priority = {"chi_tra", "tha", "eng", "chi_sim", "jpn", "kor"};
            } else {
                priority = {"chi_sim", "tha", "eng", "chi_tra", "jpn", "kor"};
            }

            for (const QString& pLang : std::as_const(priority)) {
                if (files.contains(pLang + ".traineddata")) {
                    foundLangs << pLang;
                    files.removeAll(pLang + ".traineddata");
                }
            }
            // 其余语言按字母顺序追加，但限制总数。
            // 速度优化的关键：加载的语言模型（LSTM）越多，Tesseract 初始化越慢。
            // 将上限从 10 降至 3，可使初始化速度提升数倍。
            for (const QString& file : std::as_const(files)) {
                if (foundLangs.size() >= 3) break;
                QString name = file.left(file.lastIndexOf('.'));
                if (name != "osd" && !foundLangs.contains(name)) foundLangs << name;
            }
            }
        }

        QString currentLang = foundLangs.isEmpty() ? m_language : foundLangs.join('+');
        qDebug() << "OCR: Used tessdata path:" << tessDataPath;
        qDebug() << "OCR: Detected languages:" << foundLangs.size() << ":" << currentLang;

        QStringList args;
        // 明确指定数据目录
        if (QFile::exists(tessDataPath)) {
            args << "--tessdata-dir" << QDir::toNativeSeparators(tessDataPath);
        }
        
        args << filePath << "stdout" << "-l" << currentLang << "--oem" << "1" << "--psm" << "3";
        tesseract.start(tesseractPath, args);
        
        if (!tesseract.waitForStarted()) {
            result = "无法启动 Tesseract 引擎。路径: " + tesseractPath;
        } else if (tesseract.waitForFinished(20000)) {
            QByteArray output = tesseract.readAllStandardOutput();
            QByteArray errorOutput = tesseract.readAllStandardError();
            result = QString::fromUtf8(output).trimmed();
            
            if (!result.isEmpty()) {
                qDebug() << "Tesseract OCR succeeded using:" << tesseractPath << "with lang:" << currentLang;
                emit recognitionFinished(result, contextId);
                return;
            }
            
            if (!errorOutput.isEmpty()) {
                result = "Tesseract 错误: " + QString::fromUtf8(errorOutput).left(100);
            } else {
                result = "未识别到任何内容。请检查数据包及语言。";
            }
        } else {
            tesseract.kill();
            result = "OCR 识别超时 (20s)。语言包过量或图片过大。";
        }
    } else {
        result = "未找到 Tesseract 引擎组件。搜索路径包括 resources/Tesseract-OCR。";
    }
#else
    result = "当前平台不支持 OCR 功能";
#endif

    if (result.isEmpty()) {
        result = "未能从图片中识别出任何文字";
    }
    
    qDebug() << "[OCRManager] recognizeSync: 识别完成 ID:" << contextId 
             << "结果长度:" << result.length() << "线程:" << QThread::currentThread();
    qDebug() << "[OCRManager] 准备发送 recognitionFinished 信号 ID:" << contextId;
    
    try {
        emit recognitionFinished(result, contextId);
        qDebug() << "[OCRManager] recognitionFinished 信号已发送 ID:" << contextId;
    } catch (const std::exception& e) {
        qDebug() << "[OCRManager] 异常: 发送信号时出错 ID:" << contextId << "错误:" << e.what();
    } catch (...) {
        qDebug() << "[OCRManager] 异常: 发送信号时出现未知错误 ID:" << contextId;
    }
}