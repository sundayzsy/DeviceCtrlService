#ifndef JGTDEVICE_H
#define JGTDEVICE_H

#include "core/Device.h"
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

class QTimer;

class JGTDevice : public Device
{
    Q_OBJECT

public:
    explicit JGTDevice(const QString& id, const QJsonObject& config, QObject *parent = nullptr);
    ~JGTDevice();

    bool connectDevice() override;
    void disconnectDevice() override;
    const QJsonObject& getConfig() const override;

public slots:
    void writeData2Device(const QString &key, const QString &value) override;
    void writeText2Device(const QString &text) override;
    void stop() override;


private slots:
    void onSocketStateChanged(QAbstractSocket::SocketState socketState);
    void onReadyRead();

private:
    QByteArray encodeRequest(const QString& command, const QString& value);
    void parseResponse(const QByteArray& data);

    QJsonObject m_config;
    QTcpSocket* m_tcpSocket;

};

#endif // JGTDEVICE_H
