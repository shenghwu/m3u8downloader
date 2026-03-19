#include "m3u8downloader.h"

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>
#include <QtGlobal>
#include <QDebug>

#include <algorithm>
#include <memory>
// Note: Ensure your project links to OpenSSL
#include <openssl/evp.h>

namespace
{
    constexpr int DefaultMaxWorkers = 10;
    constexpr int DefaultTimeoutMs = 30000;
    constexpr int DefaultRetryTimes = 3;

    QByteArray normalizeIv(const QByteArray &iv)
    {
        if (iv.size() == 16)
        {
            return iv;
        }

        QByteArray padded = iv;
        padded.resize(16);
        return padded;
    }
}

EnhancedM3U8Downloader::EnhancedM3U8Downloader(QObject *parent)
    : QObject(parent),
      m_maxWorkers(DefaultMaxWorkers),
      m_timeoutMs(DefaultTimeoutMs),
      m_retryTimes(DefaultRetryTimes),
      m_allowAlternateUrl(true)
{
    m_defaultHeaders.append({"User-Agent", QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                                         " AppleWebKit/537.36 (KHTML, like Gecko)"
                                                         " Chrome/91.0.4472.124 Safari/537.36")});
    m_defaultHeaders.append({"Accept", QByteArrayLiteral("*/*")});
    m_defaultHeaders.append({"Accept-Encoding", QByteArrayLiteral("identity")});
    m_defaultHeaders.append({"Connection", QByteArrayLiteral("keep-alive")});
}

void EnhancedM3U8Downloader::setMaxWorkers(int workers)
{
    m_maxWorkers = std::max(1, workers);
}

void EnhancedM3U8Downloader::setTimeout(int milliseconds)
{
    m_timeoutMs = std::max(1000, milliseconds);
}

void EnhancedM3U8Downloader::setRetryTimes(int retries)
{
    m_retryTimes = std::max(1, retries);
}

void EnhancedM3U8Downloader::setAllowAlternateUrl(bool allow)
{
    m_allowAlternateUrl = allow;
}

void EnhancedM3U8Downloader::startDownload(const QUrl &url, const QString &outputFile)
{
    // Run the download process in a background thread to avoid freezing the UI
    QFuture<bool> future = QtConcurrent::run([this, url, outputFile]() {
        return this->downloadFromUrl(url, outputFile);
    });

    // Watch the future to notify when done
    QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, [this, watcher, outputFile]() {
        bool result = watcher->future().result();
        if (result) {
            emit downloadFinished(true, QString("Download completed: %1").arg(outputFile));
        } else {
            emit downloadFinished(false, "Download failed. Check logs.");
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void EnhancedM3U8Downloader::log(const QString &msg)
{
    qInfo().noquote() << msg;
    emit logMessage(msg);
}

bool EnhancedM3U8Downloader::downloadFromUrl(const QUrl &url, const QString &outputFile)
{
    if (!url.isValid())
    {
        log(QString("Invalid URL supplied: %1").arg(url.toString()));
        return false;
    }

    log(QString("Starting analysis of URL: %1").arg(url.toString()));

    if (isPlayableM3U8(url))
    {
        log("Detected playable m3u8 link, downloading directly...");
        return downloadM3U8(url, outputFile);
    }

    log("Likely a video page, extracting m3u8 links...");
    const QStringList m3u8Urls = extractM3U8FromPage(url);
    if (m3u8Urls.isEmpty())
    {
        log("No m3u8 links found, please provide m3u8 URL manually");
        return false;
    }

    log(QString("Found %1 m3u8 links").arg(m3u8Urls.size()));
    for (int i = 0; i < m3u8Urls.size(); ++i)
    {
        log(QString("%1. %2").arg(i + 1).arg(m3u8Urls.at(i)));
    }

    const QUrl primaryUrl(m3u8Urls.first());
    if (downloadM3U8(primaryUrl, outputFile))
    {
        // if (!m_allowAlternateUrl)
        // {
        //     m_allowAlternateUrl = true;
        // }
        return true;
    }

    if (m_allowAlternateUrl && m3u8Urls.size() > 1)
    {
        const QUrl fallbackUrl(m3u8Urls.at(1));
        log(QString("Try to get the video source from %1").arg(fallbackUrl.toString()));
        return downloadM3U8(fallbackUrl, outputFile);
    }

    return false;
}

bool EnhancedM3U8Downloader::downloadM3U8(const QUrl &m3u8Url, const QString &outputFile)
{
    if (!m3u8Url.isValid())
    {
        log(QString("Invalid m3u8 URL: %1").arg(m3u8Url.toString()));
        return false;
    }

    log(QString("Processing m3u8: %1").arg(m3u8Url.toString()));

    QFileInfo outputInfo(QDir::cleanPath(outputFile));
    if (!outputInfo.isAbsolute())
    {
        QDir currentDir(QDir::currentPath());
        outputInfo = QFileInfo(currentDir.absoluteFilePath(outputFile));
    }

    QString basePath = outputInfo.absolutePath();
    if (basePath.isEmpty())
    {
        basePath = QDir::currentPath();
    }

    QString fileName = outputInfo.fileName();
    QString suffix = outputInfo.suffix().toLower();
    if (suffix != QStringLiteral("mp4") &&
        suffix != QStringLiteral("ts") &&
        suffix != QStringLiteral("mkv"))
    {
        QString baseName = outputInfo.completeBaseName();
        if (baseName.isEmpty())
        {
            baseName = outputInfo.fileName();
        }
        if (baseName.isEmpty())
        {
            baseName = QStringLiteral("output");
        }
        fileName = baseName + QStringLiteral(".mp4");
    }

    const QString finalOutputPath = QDir(basePath).absoluteFilePath(fileName);
    outputInfo.setFile(finalOutputPath);

    QDir outputDir(outputInfo.absolutePath());
    if (!outputDir.exists())
    {
        if (!outputDir.mkpath("."))
        {
            log(QString("Cannot create output directory: %1").arg(outputDir.path()));
            return false;
        }
    }

    const QString outputPath = outputInfo.absoluteFilePath();

    const PlaylistResult playlist = resolveM3U8Nesting(m3u8Url);
    if (!playlist.isValid())
    {
        log("Cannot resolve m3u8 nesting structure");
        return false;
    }

    log(QString("Found %1 TS segments").arg(playlist.segments.size()));

    const QString tempDirPath = outputDir.absoluteFilePath(outputInfo.completeBaseName() + "_temp");
    QDir tempDir(tempDirPath);
    if (!tempDir.exists())
    {
        if (!outputDir.mkpath(tempDirPath))
        {
            log(QString("Cannot create temp directory: %1").arg(tempDirPath));
            return false;
        }
    }

    emit downloadProgress(0, playlist.segments.size());

    const QVector<QString> tsFiles = downloadTsSegments(playlist.segments, tempDir,
                                                        playlist.key, playlist.iv);
    if (tsFiles.isEmpty())
    {
        log("Failed to download TS segments");
        return false;
    }

    log("Merging TS files...");
    if (!mergeTsFiles(tsFiles, outputPath))
    {
        log("Failed to merge TS files");
        return false;
    }

    cleanupTempDir(tempDir);
    log(QString("Download complete: %1").arg(outputPath));
    return true;
}

EnhancedM3U8Downloader::PlaylistResult EnhancedM3U8Downloader::resolveM3U8Nesting(const QUrl &m3u8Url,
                                                                                   int depth,
                                                                                   int maxDepth)
{
    if (depth > maxDepth)
    {
        log("m3u8 nesting too deep");
        return {};
    }

    log(QString(depth * 2, ' ') + QStringLiteral("Resolving level %1 m3u8: %2")
                             .arg(depth + 1)
                             .arg(m3u8Url.toString()));

    const QByteArray contentBytes = downloadText(m3u8Url);
    if (contentBytes.isEmpty())
    {
        return {};
    }

    const QString content = QString::fromUtf8(contentBytes);
    if (content.contains(QStringLiteral("#EXT-X-STREAM-INF")))
    {
        log(QString(depth * 2, ' ') + QStringLiteral("Detected master playlist, finding best stream..."));
        const QUrl bestStream = findBestStream(content, m3u8Url);
        if (!bestStream.isEmpty())
        {
            return resolveM3U8Nesting(bestStream, depth + 1, maxDepth);
        }
        return {};
    }

    log(QString(depth * 2, ' ') + QStringLiteral("Detected media playlist, parsing TS segments..."));
    return parseMediaPlaylist(content, m3u8Url);
}

EnhancedM3U8Downloader::PlaylistResult EnhancedM3U8Downloader::parseMediaPlaylist(const QString &content,
                                                                                   const QUrl &baseUrl)
{
    PlaylistResult result;

    const QStringList lines = content.split('\n');
    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
        {
            continue;
        }

        if (line.startsWith('#'))
        {
            if (line.startsWith(QStringLiteral("#EXT-X-KEY")))
            {
                QRegularExpression keyExpr(QStringLiteral("URI=\"([^\"]+)\""));
                QRegularExpression ivExpr(QStringLiteral("IV=([^,]+)"));

                const QRegularExpressionMatch keyMatch = keyExpr.match(line);
                if (keyMatch.hasMatch() && !result.key.has_value())
                {
                    QUrl keyUrl(keyMatch.captured(1));
                    if (keyUrl.isRelative())
                    {
                        keyUrl = baseUrl.resolved(keyUrl);
                    }

                    if (keyUrl.isValid())
                    {
                        result.key = downloadKey(keyUrl);
                    }
                    else
                    {
                        log(QString("Invalid key URL: %1").arg(keyMatch.captured(1)));
                    }
                }

                const QRegularExpressionMatch ivMatch = ivExpr.match(line);
                if (ivMatch.hasMatch())
                {
                    QString ivString = ivMatch.captured(1);
                    if (ivString.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
                    {
                        ivString = ivString.mid(2);
                    }
                    result.iv = QByteArray::fromHex(ivString.toLatin1());
                }
            }
            continue;
        }

        // Standard HLS check: line without # or explicitly ending in .ts
        if (!line.startsWith('#'))
        {
            QUrl segmentUrl(line);
            if (segmentUrl.isRelative())
            {
                segmentUrl = baseUrl.resolved(segmentUrl);
            }
            if (segmentUrl.isValid())
            {
                 result.segments.append(segmentUrl);
            }
        }
    }

    return result;
}

QUrl EnhancedM3U8Downloader::findBestStream(const QString &content, const QUrl &baseUrl)
{
    qint64 bestBandwidth = 0;
    QUrl bestUrl;

    const QStringList lines = content.split('\n');
    qint64 currentBandwidth = 0;

    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();

        if (line.startsWith(QStringLiteral("#EXT-X-STREAM-INF")))
        {
            QRegularExpression bandwidthExpr(QStringLiteral("BANDWIDTH=(\\d+)"));
            const QRegularExpressionMatch bandwidthMatch = bandwidthExpr.match(line);
            if (bandwidthMatch.hasMatch())
            {
                currentBandwidth = bandwidthMatch.captured(1).toLongLong();
            }
        }
        else if (!line.isEmpty() && !line.startsWith('#'))
        {
            QUrl streamUrl(line);
            if (streamUrl.isRelative())
            {
                streamUrl = baseUrl.resolved(streamUrl);
            }

            if (currentBandwidth > bestBandwidth)
            {
                bestBandwidth = currentBandwidth;
                bestUrl = streamUrl;
            }
        }
    }

    if (!bestUrl.isEmpty())
    {
        log(QString("Selected best stream: BW=%1, URL=%2").arg(bestBandwidth).arg(bestUrl.toString()));
    }

    return bestUrl;
}

bool EnhancedM3U8Downloader::isPlayableM3U8(const QUrl &url)
{
    const QByteArray content = downloadText(url);
    return !content.isEmpty() && content.contains("#EXTINF");
}

QStringList EnhancedM3U8Downloader::extractM3U8FromPage(const QUrl &pageUrl)
{
    const QByteArray content = downloadText(pageUrl);
    if (content.isEmpty())
    {
        return {};
    }

    const QString pageContent = QString::fromUtf8(content);
    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("https?://[^\\s\"']+\\.m3u8[^\\s\"']*")),
        QRegularExpression(QStringLiteral("/[^\\s\"']+\\.m3u8[^\\s\"']*"))
    };

    QSet<QString> unique;
    for (const QRegularExpression &pattern : patterns)
    {
        auto it = pattern.globalMatch(pageContent);
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            QString link = match.captured(0);
            QUrl candidate(link);
            if (candidate.isRelative())
            {
                candidate = pageUrl.resolved(candidate);
            }
            unique.insert(candidate.toString());
        }
    }

    return QStringList(unique.begin(), unique.end());
}

QNetworkAccessManager* EnhancedM3U8Downloader::getManagerForCurrentThread()
{
    QThread *thread = QThread::currentThread();
    QMutexLocker lock(&m_managerMutex);
    auto it = m_networkManagers.find(thread);
    if (it == m_networkManagers.end())
    {
        auto *mgr = new QNetworkAccessManager();
        m_networkManagers.insert(thread, mgr);
        // Remove the entry when the thread exits so the hash doesn't grow
        // unboundedly. The QNAM itself is deleted by Qt (no parent, but we
        // call deleteLater via the finished connection).
        QObject::connect(thread, &QThread::finished, mgr, &QObject::deleteLater);
        QObject::connect(thread, &QThread::finished, this, [this, thread]() {
            QMutexLocker l(&m_managerMutex);
            m_networkManagers.remove(thread);
        });
        return mgr;
    }
    return it.value();
}

QByteArray EnhancedM3U8Downloader::downloadText(const QUrl &url)
{
    return downloadBinary(url);
}

QByteArray EnhancedM3U8Downloader::downloadBinary(const QUrl &url)
{
    if (!url.isValid())
    {
        return {};
    }

    // Reuse the per-thread QNAM so HTTP keep-alive connections are preserved
    // across calls on the same thread, avoiding a full TCP+TLS handshake for
    // every segment (mirrors what requests.Session does in the Python version).
    QNetworkAccessManager *manager = getManagerForCurrentThread();

    for (int attempt = 0; attempt < m_retryTimes; ++attempt)
    {
        // Must create QNAM in the current thread (which is likely a worker thread)
        // auto manager = std::make_unique<QNetworkAccessManager>();
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        for (const auto &header : m_defaultHeaders)
        {
            request.setRawHeader(header.first, header.second);
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        QNetworkReply *reply = manager->get(request);
        reply->ignoreSslErrors();
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        // QObject::connect(reply, &QNetworkReply::sslErrors, reply, &QNetworkReply::ignoreSslErrors);
        QObject::connect(&timer, &QTimer::timeout, [&]() {
            log(QString("Download timeout, cancelling: %1").arg(url.toString()));
            reply->abort();
        });

        timer.start(m_timeoutMs);
        loop.exec();

        if (timer.isActive())
        {
            timer.stop();
        }

        QByteArray data;
        bool success = (reply->error() == QNetworkReply::NoError);
        if (success)
        {
            data = reply->readAll();
        }
        else
        {
            // Only log simple errors if it's not the last attempt
            if (attempt == m_retryTimes - 1) {
             qWarning() << "Download failed" << reply->errorString();
            }
        }

        delete reply;
        // manager.reset();

        if (success && !data.isEmpty())
        {
            return data;
        }
        
        // Simple backoff
        if (attempt + 1 < m_retryTimes)
        {
             QThread::msleep(2000); 
        }
    }

    return {};
}

QByteArray EnhancedM3U8Downloader::downloadKey(const QUrl &url)
{
    log(QString("Downloading decryption key: %1").arg(url.toString()));
    return downloadBinary(url);
}

QVector<QString> EnhancedM3U8Downloader::downloadTsSegments(const QVector<QUrl> &urls,
                                                            QDir tempDir,
                                                            const std::optional<QByteArray> &key,
                                                            const std::optional<QByteArray> &iv)
{
    if (!tempDir.exists())
    {
        if (!QDir().mkpath(tempDir.path()))
        {
            log(QString("Cannot create temp dir: %1").arg(tempDir.path()));
            return {};
        }
    }

    if (urls.isEmpty())
    {
        return {};
    }

    struct SegmentResult
    {
        int index = -1;
        QString filePath;
        bool success = false;
    };

    // Make local copies for lambda capture
    const QByteArray effectiveKey = key.value_or(QByteArray());
    const QByteArray effectiveIv = iv.has_value() ? normalizeIv(iv.value()) : QByteArray();
    
    // We need to know who to notify
    // auto notifier = [this, total = urls.size()](int current) {
    //      emit downloadProgress(current, total);
    // };

    // Atomic counter so each worker emits progress exactly once when it finishes,
    // giving a smooth +1 increment in the UI regardless of segment completion order.
    // (The old approach called notifier() from the ordered collection loop, which
    // caused bursts: if segments 1-25 finished while waiting on segment 0, they
    // all fired in rapid succession the moment segment 0 unblocked.)
    // std::atomic<int> completedCount{0};
    mCompletedCount = 0;
    const int totalCount = urls.size();
    // const int totalCount = static_cast<int>(urls.size());

    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, m_maxWorkers));

    QVector<QFuture<SegmentResult>> futures;
    futures.reserve(urls.size());

    // Submit all tasks
    for (int i = 0; i < urls.size(); ++i)
    {
        const QUrl segmentUrl = urls.at(i);
        const QString filePath = tempDir.filePath(QStringLiteral("segment_%1.ts").arg(i, 5, 10, QChar('0')));

        futures.append(QtConcurrent::run(&pool, [this, i, segmentUrl, filePath, effectiveKey, effectiveIv,  totalCount]() -> SegmentResult {
            SegmentResult result;
            result.index = i;
            result.filePath = filePath;

            QFile existing(filePath);
            if (existing.exists() && existing.size() > 0)
            {
                result.success = true;
                qDebug() << "Emitting progress for segment" << i << "count =" << (mCompletedCount.load() + 1);
                emit downloadProgress(mCompletedCount.fetch_add(1) + 1, totalCount);
                return result;
            }

            const QByteArray payload = downloadBinary(segmentUrl);
            if (payload.isEmpty())
            {
                return result; // success = false
            }

            QByteArray data = payload;
            if (!effectiveKey.isEmpty() && !effectiveIv.isEmpty())
            {
                data = decryptAesCbc(payload, effectiveKey, effectiveIv);
                if (data.isEmpty())
                {
                    // Decryption failed
                    return result;
                }
            }

            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly))
            {
                return result;
            }

            file.write(data);
            file.close();

            result.success = true;
            qDebug() << "Emitting progress (new) for segment" << i << "count =" << (mCompletedCount.load() + 1);
            emit downloadProgress(mCompletedCount.fetch_add(1) + 1, totalCount);
            return result;
        }));
    }

    QVector<QString> files(urls.size());
    // int completed = 0;
    int failed=0;

    // Wait and collect results
    for (QFuture<SegmentResult> &future : futures)
    {
        future.waitForFinished();
        const SegmentResult result = future.result();
        if (result.success && result.index >= 0 && result.index < files.size())
        {
            files[result.index] = result.filePath;
            // ++completed;
            // notifier(completed);
        }
        else
        {
            log(QString("Failed segment index: %1").arg(result.index));
            ++failed;
        }
    }

    // if (completed != urls.size())
    if (failed > 0)
    {
        // log(QString("Download failed. incomplete segments: %1").arg(urls.size() - completed));
        log(QString("Download failed. incomplete segments: %1").arg(failed));
        return {};
    }

    return files;
}

bool EnhancedM3U8Downloader::mergeTsFiles(const QVector<QString> &segments, const QString &outputPath)
{
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log(QString("Cannot create output file: %1").arg(outputPath));
        return false;
    }

    for (const QString &segmentPath : segments)
    {
        QFile segment(segmentPath);
        if (!segment.open(QIODevice::ReadOnly))
        {
            log(QString("Cannot read segment: %1").arg(segmentPath));
            return false;
        }

        output.write(segment.readAll());
        segment.close();
    }

    output.close();
    return true;
}

void EnhancedM3U8Downloader::cleanupTempDir(const QDir &tempDir)
{
    if (!tempDir.exists())
    {
        return;
    }

    const QFileInfoList entries = tempDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries)
    {
        QFile::remove(info.absoluteFilePath());
    }

    QDir dir(tempDir);
    dir.rmpath(tempDir.path());
    log("Temp files cleaned.");
}

QByteArray EnhancedM3U8Downloader::decryptAesCbc(const QByteArray &ciphertext,
                                                 const QByteArray &key,
                                                 const QByteArray &iv)
{
    if (key.isEmpty() || iv.isEmpty())
    {
        return ciphertext;
    }

    const EVP_CIPHER *cipher = nullptr;
    switch (key.size())
    {
    case 16:
        cipher = EVP_aes_128_cbc();
        break;
    case 24:
        cipher = EVP_aes_192_cbc();
        break;
    case 32:
        cipher = EVP_aes_256_cbc();
        break;
    default:
        qCritical() << "Unsupported key size:" << key.size();
        return {};
    }

    const int expectedIvLength = EVP_CIPHER_iv_length(cipher);
    if (iv.size() != expectedIvLength)
    {
        qCritical() << "IV length mismatch:" << iv.size() << "expected:" << expectedIvLength;
        return {};
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        return {};
    }

    QByteArray buffer(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int outLen1 = 0;
    int outLen2 = 0;

    if (EVP_DecryptInit_ex(ctx, cipher, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(iv.constData())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_DecryptUpdate(ctx,
                           reinterpret_cast<unsigned char *>(buffer.data()),
                           &outLen1,
                           reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                           static_cast<int>(ciphertext.size())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    if (EVP_DecryptFinal_ex(ctx,
                             reinterpret_cast<unsigned char *>(buffer.data()) + outLen1,
                             &outLen2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    EVP_CIPHER_CTX_free(ctx);

    buffer.resize(outLen1 + outLen2);
    return buffer;
}
