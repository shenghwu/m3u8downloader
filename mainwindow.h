#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "m3u8downloader.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_download_btn_clicked();
    // void onDownloadFinished(bool success);

    void onDownloadProgress(int current, int total, qint64 speed);
    void onStatusChanged(const QString &status);
    void onErrorOccured(const QString &error);
    void onDownloadFinished();

private:
    Ui::MainWindow *ui;
    EnhancedM3U8Downloader *m_downloader;
    const QString STORED_PTH = "E:\tempVideoData";
};
#endif // MAINWINDOW_H
