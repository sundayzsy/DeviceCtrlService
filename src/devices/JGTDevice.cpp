#include "JGTDevice.h"
#include <QTimer>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

JGTDevice::JGTDevice(const QString& id, const QJsonObject& config, QObject *parent)
    : Device(id, parent)
    , m_config(config)
    , m_tcpSocket(new QTcpSocket(this))
{
    connect(m_tcpSocket, &QTcpSocket::stateChanged, this, &JGTDevice::onSocketStateChanged);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &JGTDevice::onReadyRead);


}

JGTDevice::~JGTDevice()
{
}

void JGTDevice::writeData2Device(const QString &key, const QString &value)
{
    // Find the command from config
    QJsonArray registers = m_config["registers"].toArray();
    for (const QJsonValue& val : registers) {
        QJsonObject obj = val.toObject();
        if (obj["key"].toString() == key) {
            QString command = obj["command"].toString();
            if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
                m_tcpSocket->write(encodeRequest(command, value));
            }
            return;
        }
    }
    qWarning() << "JGTDevice: Could not find key" << key << "in config";
}

bool JGTDevice::connectDevice()
{
    if (m_tcpSocket->state() == QAbstractSocket::UnconnectedState) {
        QJsonObject tcpParams = m_config["tcp_params"].toObject();
        QString ip = tcpParams["ip_address"].toString();
        int port = tcpParams["port"].toInt();
        m_tcpSocket->connectToHost(ip, port);
    }
    return true; // Asynchronous connection
}

void JGTDevice::disconnectDevice()
{
    m_tcpSocket->disconnectFromHost();
}

const QJsonObject& JGTDevice::getConfig() const
{
    return m_config;
}

void JGTDevice::writeText2Device(const QString &text)
{
    if(text.isEmpty())
        return;

    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState)
    {
        QByteArray bytes = text.toLatin1();
        m_tcpSocket->write(bytes);
        emit sig_printLog(bytes,true);
    }
}

void JGTDevice::stop()
{
    disconnectDevice();
}

void JGTDevice::onSocketStateChanged(QAbstractSocket::SocketState socketState)
{
    bool connected = (socketState == QAbstractSocket::ConnectedState);
    emit connectedChanged(deviceId(), connected);
}

void JGTDevice::onReadyRead()
{
    QByteArray data = m_tcpSocket->readAll();
    if (!data.isEmpty()) {
        emit sig_printLog(data, false);
        // 如果需要，可以在这里直接调用解析逻辑
        // parseResponse(data);
    }
}

QByteArray JGTDevice::encodeRequest(const QString& command, const QString& value)
{
    // Protocol: <command,value>
    return QString("<%1,%2>").arg(command).arg(value).toUtf8();
}

void JGTDevice::parseResponse(const QByteArray& data)
{
    QString responseStr(data);
    // The response might contain multiple messages concatenated, e.g., "<MSG1><MSG2>"
    // We need to split them. A simple way is to replace "><" with a unique separator and then split.
    responseStr.replace("><", ">#<");
    QStringList messages = responseStr.split('#');

    for (const QString& msg : messages) {
        if (msg.startsWith('<') && msg.endsWith('>')) {
            // Remove '<' and '>'
            QString content = msg.mid(1, msg.length() - 2);
            // content is now "COMMAND,VALUE" or "COMMAND"
            QStringList parts = content.split(',');
            if (parts.isEmpty()) continue;

            QString command = parts[0];
            QString value = (parts.size() > 1) ? parts[1] : "";

            // Find the key associated with this command
            QJsonArray registers = m_config["registers"].toArray();
            for (const QJsonValue& regVal : registers) {
                QJsonObject obj = regVal.toObject();
                if (obj["command"].toString() == command) {
                    QString key = obj["key"].toString();
                    // Update DataManager with the new value
                    emit dataUpdated(deviceId(), key, QJsonValue(value));
                    qDebug() << "JGTDevice parsed response for key:" << key << "value:" << value;
                    break; // Found command, move to next message in the frame
                }
            }
        }
    }
}

