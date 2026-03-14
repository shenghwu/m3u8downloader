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

    m_downloader = new EnhancedM3U8Downloader(this);
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
        if (success) {
            ui->progressBar->setFormat("下载完成");
            QMessageBox::information(this, "完成", "视频下载完成！");
        } else {
            ui->progressBar->setFormat("下载失败");
            QMessageBox::critical(this, "错误", msg);
        }

        downloader->deleteLater(); // Clean up

    });

    // Start!
    downloader->startDownload(QUrl(urlStr), outputPath);
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

