#ifndef ENHANCEDM3U8DOWNLOADER_H
#define ENHANCEDM3U8DOWNLOADER_H

#include <QObject>
#include <QUrl>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <QDir>
#include <QPair>
#include <QList>
#include <QHash>
#include <QMutex>

#include <optional>

class QThread;
class QNetworkAccessManager;

class EnhancedM3U8Downloader : public QObject
{
    Q_OBJECT
public:
    explicit EnhancedM3U8Downloader(QObject *parent = nullptr);

    void setMaxWorkers(int workers);
    void setTimeout(int milliseconds);
    void setRetryTimes(int retries);
    void setAllowAlternateUrl(bool allow);

    // Main entry point - asynchronous
    void startDownload(const QUrl &url, const QString &outputFile);

    // Blocking versions (internal use or if running in a thread)
    bool downloadFromUrl(const QUrl &url, const QString &outputFile);
    bool downloadM3U8(const QUrl &m3u8Url, const QString &outputFile);

signals:
    void downloadProgress(int current, int total);
    void logMessage(const QString &msg);
    void downloadFinished(bool success, const QString &message);

private:
    struct PlaylistResult
    {
        QVector<QUrl> segments;
        std::optional<QByteArray> key;
        std::optional<QByteArray> iv;

        bool isValid() const { return !segments.isEmpty(); }
    };

    PlaylistResult resolveM3U8Nesting(const QUrl &m3u8Url, int depth = 0, int maxDepth = 5);
    PlaylistResult parseMediaPlaylist(const QString &content, const QUrl &baseUrl);
    QUrl findBestStream(const QString &content, const QUrl &baseUrl);

    bool isPlayableM3U8(const QUrl &url);
    QStringList extractM3U8FromPage(const QUrl &pageUrl);

    QByteArray downloadText(const QUrl &url);
    QByteArray downloadBinary(const QUrl &url);
    QByteArray downloadKey(const QUrl &url);

    QVector<QString> downloadTsSegments(const QVector<QUrl> &urls,
                                        QDir tempDir,
                                        const std::optional<QByteArray> &key,
                                        const std::optional<QByteArray> &iv);

    bool mergeTsFiles(const QVector<QString> &segments, const QString &outputPath);
    void cleanupTempDir(const QDir &tempDir);

    QByteArray decryptAesCbc(const QByteArray &ciphertext,
                             const QByteArray &key,
                             const QByteArray &iv);
    
    // Helper to emit log signal
    void log(const QString &msg);

    // Returns a QNetworkAccessManager local to the calling thread,
    // creating one on first use. Reusing the manager across requests
    // on the same thread enables HTTP keep-alive and avoids paying the
    // TCP+TLS handshake cost on every segment (equivalent to requests.Session).
    QNetworkAccessManager* getManagerForCurrentThread();

    int m_maxWorkers;
    int m_timeoutMs;
    int m_retryTimes;
    bool m_allowAlternateUrl;

    QList<QPair<QByteArray, QByteArray>> m_defaultHeaders;

    // One QNAM per worker thread — QNAM is not thread-safe so it must
    // not be shared across threads, but reusing within a thread is safe
    // and avoids reconnecting for every segment.
    QHash<QThread*, QNetworkAccessManager*> m_networkManagers;
    QMutex m_managerMutex;

    std::atomic<int> mCompletedCount;
};

#endif // ENHANCEDM3U8DOWNLOADER_H
