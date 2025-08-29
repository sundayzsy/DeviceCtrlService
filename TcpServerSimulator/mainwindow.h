#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QTcpServer;
class QTcpSocket;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_connectButton_clicked();
    void on_sendButton_clicked();

    void newConnection();
    void disconnected();
    void readyRead();

private:
    QByteArray toHex(const QByteArray &bytes);

private:
    Ui::MainWindow *ui;
    QTcpServer *m_server;
    QTcpSocket *m_socket;
};
#endif // MAINWINDOW_H
