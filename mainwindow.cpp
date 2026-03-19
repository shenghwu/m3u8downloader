#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QString>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("M3U8Downloader");

    ui->centralwidget->setStyleSheet(
        "QWidget#centralwidget {"
        "    background-color: grey;"
        "    border-top-left-radius: 10px;"
        "    border-top-right-radius: 10px;"
        "    border-bottom-left-radius: 10px;"
        "    border-bottom-right-radius: 10px;"
        "}"
    );
    setMinimumSize(500, 400);

    // m_downloader = new EnhancedM3U8Downloader(this);
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->progressBar->setFormat("准备中...");
    ui->progressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 1px solid grey;"
        "    border-radius: 5px;"
        "    background-color: #f0f0f0;"
        "    text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "   background-color: rgba(0, 255, 0, 100);" // half-green
        "   border-radius: 5px;"
        "}"
        );
    ui->progressBar->setFixedHeight(20);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_download_btn_clicked()
{
    QString urlStr = ui->m3u8_lineEdit->text();
    QString outputPath = ui->output_file_name->text();

    ui->download_btn->setEnabled(false);
    ui->statusbar->showMessage("Parsing m3u8 file...", 2000);
    m_downloadTimer.start();


    // Create the downloader instance
    // Note: Better to have this as a member variable if you want to reuse it or cancel it
    EnhancedM3U8Downloader *downloader = new EnhancedM3U8Downloader(this);

    // Connect logging to a text area or debug console
    connect(downloader, &EnhancedM3U8Downloader::logMessage, this, [](const QString &msg){
        qDebug() << "[Downloader]" << msg;
        // ui->logTextEdit->append(msg);

    });

    // Connect progress to a progress bar
    connect(downloader, &EnhancedM3U8Downloader::downloadProgress, this, [this](int current, int total){
        ui->progressBar->setRange(0, total);
        ui->progressBar->setValue(current);
        ui->progressBar->setFormat(QString("%1/%2").arg(current).arg(total));
        ui->statusbar->showMessage(QString("Downloading clip - %1").arg(current));
    });

    // Connect finished signal
    connect(downloader, &EnhancedM3U8Downloader::downloadFinished, this, [this, downloader](bool success, const QString &msg){
        qDebug() << "Finished:" << msg;
        ui->download_btn->setEnabled(true);
        QString timeStr = downloadTime();
        if (success) {
            ui->progressBar->setFormat("下载完成");
            statusBar()->showMessage(QString("下载完成，耗时：%1").arg(timeStr), 3000);
            QMessageBox::information(this, "完成", "视频下载完成！");
        } else {
            ui->progressBar->setFormat("下载失败");
            statusBar()->showMessage(QString("下载失败，耗时：%1").arg(timeStr), 3000);
            QMessageBox::critical(this, "错误", msg);
        }

        downloader->deleteLater(); // Clean up

    });

    // Start!
    downloader->startDownload(QUrl(urlStr), outputPath);
}

QString MainWindow::downloadTime()
{
    QString timeStr;
    qint64 elapsedMs = m_downloadTimer.elapsed();
    int seconds = (elapsedMs / 1000) % 60;
    int minutes = (elapsedMs / (1000 * 60)) % 60;
    int hours = (elapsedMs / (1000 * 60 * 60));
    if (hours > 0) {
        timeStr = QString("%1h%2m%3s").arg(hours).arg(minutes).arg(seconds);
    } else if (minutes > 0) {
        timeStr = QString("%1m%2s").arg(minutes).arg(seconds);
    } else {
        timeStr = QString("%1.%2s").arg(seconds).arg((elapsedMs % 1000) / 100);
    }
    return timeStr;
}

void MainWindow::onDownloadFinished()
{
    ui->download_btn->setEnabled(true);

    ui->percent_label->setText("下载完成");
    QMessageBox::information(this, "完成", "视频下载完成！");

}

void MainWindow::onDownloadProgress(int current, int total, qint64 speed)
{
    Q_UNUSED(current);
    Q_UNUSED(total);
    Q_UNUSED(speed);
}
void MainWindow::onStatusChanged(const QString &status)
{
    Q_UNUSED(status);
}
void MainWindow::onErrorOccured(const QString &error)
{
    Q_UNUSED(error);
}

