#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_server(nullptr)
    , m_socket(nullptr)
{
    ui->setupUi(this);
    setWindowTitle("Tcp Server Simulator");

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &MainWindow::newConnection);
}

MainWindow::~MainWindow()
{
    if (m_socket) {
        m_socket->close();
    }
    if (m_server) {
        m_server->close();
    }
    delete ui;
}

void MainWindow::on_connectButton_clicked()
{
    if (m_server->isListening()) {
        if(m_socket) {
            m_socket->disconnectFromHost();
        }
        m_server->close();
        ui->logPlainTextEdit->appendPlainText("Server stopped.");
        ui->connectButton->setText("Connect");
        ui->ipLineEdit->setEnabled(true);
        ui->portLineEdit->setEnabled(true);
    } else {
        QHostAddress address(ui->ipLineEdit->text());
        quint16 port = ui->portLineEdit->text().toUShort();

        if (m_server->listen(address, port)) {
            ui->logPlainTextEdit->appendPlainText(QString("Server started, listening on %1:%2").arg(address.toString()).arg(port));
            ui->connectButton->setText("Disconnect");
            ui->ipLineEdit->setEnabled(false);
            ui->portLineEdit->setEnabled(false);
        } else {
            ui->logPlainTextEdit->appendPlainText("Error: " + m_server->errorString());
        }
    }
}

void MainWindow::on_sendButton_clicked()
{
    if (m_socket && m_socket->isOpen() && m_socket->isWritable()) {
        QByteArray data = ui->dataPlainTextEdit->toPlainText().toUtf8();
        m_socket->write(data);

        ui->logPlainTextEdit->appendPlainText(QTime::currentTime().toString(tr("hh:mm:ss")));
        ui->logPlainTextEdit->appendPlainText(QString(tr("Send(text): ")) + QString::fromUtf8(data));
        ui->logPlainTextEdit->appendPlainText(QString(tr("Send(hex ): ")) + toHex(data));
        ui->logPlainTextEdit->appendPlainText(QString(" "));
    } else {
        ui->logPlainTextEdit->appendPlainText("No client connected or socket not writable.");
    }
}

void MainWindow::newConnection()
{
    // For this simple simulator, we only handle one client at a time.
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
    }

    m_socket = m_server->nextPendingConnection();
    if (m_socket) {
        connect(m_socket, &QTcpSocket::disconnected, this, &MainWindow::disconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &MainWindow::readyRead);

        QString peerAddress = m_socket->peerAddress().toString();
        quint16 peerPort = m_socket->peerPort();
        ui->logPlainTextEdit->appendPlainText(QString("New connection from %1:%2").arg(peerAddress).arg(peerPort));
    }
}

void MainWindow::disconnected()
{
    ui->logPlainTextEdit->appendPlainText("Client disconnected.");
    m_socket->deleteLater();
    m_socket = nullptr;
}

void MainWindow::readyRead()
{
    if (m_socket && m_socket->bytesAvailable() > 0) {
        QByteArray data = m_socket->readAll();

        ui->logPlainTextEdit->appendPlainText(QTime::currentTime().toString(tr("hh:mm:ss")));
        ui->logPlainTextEdit->appendPlainText(QString(tr("Recv(text): ")) + QString::fromUtf8(data));
        ui->logPlainTextEdit->appendPlainText(QString(tr("Recv(hex ): ")) + toHex(data));
        ui->logPlainTextEdit->appendPlainText(QString(" "));
    }
}

QByteArray MainWindow::toHex(const QByteArray &bytes)
{
    QByteArray hexBytes;
    const char *pBytes = bytes.data();
    for(int i = 0;i < bytes.size();i++) {
        hexBytes += QByteArray(1,(uchar)pBytes[i]).toHex().toUpper() + " ";
    }
    return hexBytes;
}
