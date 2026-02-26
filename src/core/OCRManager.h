#ifndef OCRMANAGER_H
#define OCRMANAGER_H
#include <QObject>
#include <QImage>
#include <QString>

class OCRManager : public QObject {
    Q_OBJECT
public:
    static OCRManager& instance();
    void recognizeAsync(const QImage& image, int contextId = -1);
    
    // 设置 OCR 识别语言（默认: "chi_sim+eng"）
    // 可用语言见 traineddata 文件，多语言用 + 连接
    // 例如: "chi_sim+eng", "jpn+eng", "fra+deu"
    void setLanguage(const QString& lang);
    QString getLanguage() const;

private:
    void recognizeSync(const QImage& image, int contextId);
    QImage preprocessImage(const QImage& original);

signals:
    void recognitionFinished(const QString& text, int contextId);

private:
    OCRManager(QObject* parent = nullptr);
    QString m_language = "chi_sim+eng"; // 默认中文简体+英文
};

#endif // OCRMANAGER_H