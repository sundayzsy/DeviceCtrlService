#include "JGQDevice.h"
#include <QModbusTcpClient>
#include <QVariant>

/**
 * @file JGQDevice.cpp
 * @brief JGQDevice类的实现
 */

JGQDevice::JGQDevice(const QString& id, const QJsonObject& config, QObject *parent)
    : Device(id, parent)
    , m_config(config)
    , m_modbusDevice(nullptr)
{
}

JGQDevice::~JGQDevice()
{
}

void JGQDevice::writeData2Device(const QString &key, const QString &value)
{

}

bool JGQDevice::connectDevice()
{
    if (m_modbusDevice)
        return true;

    m_modbusDevice = new QModbusTcpClient(this);
    connect(m_modbusDevice, &QModbusClient::stateChanged, this, &JGQDevice::onStateChanged);

    QJsonObject tcpParams = m_config["tcp_params"].toObject();
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkAddressParameter, tcpParams["ip_address"].toString());
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkPortParameter, tcpParams["port"].toInt());

    QJsonObject protocolParams = m_config["protocol_params"].toObject();
    m_modbusDevice->setTimeout(protocolParams["response_timeout"].toInt());
    m_modbusDevice->setNumberOfRetries(protocolParams["retry_count"].toInt());

    return m_modbusDevice->connectDevice();
}

void JGQDevice::disconnectDevice()
{
    if (m_modbusDevice)
        m_modbusDevice->disconnectDevice();
}

void JGQDevice::onStateChanged(int state)
{
    setConnected(state == QModbusDevice::ConnectedState);
}

void JGQDevice::onReadReady()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        // TODO: Process data
    }

    reply->deleteLater();
}

const QJsonObject& JGQDevice::getConfig() const
{
    return m_config;
}
