#include "ModbusDevice.h"

/**
 * @file ModbusDevice.cpp
 * @brief ModbusDevice类的实现
 */

#include <QSerialPort>
#include <QTcpSocket>

ModbusDevice::ModbusDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent)
    : Device(id, name, parent)
    , m_config(config)
    , m_serialPort(nullptr)
    , m_tcpSocket(nullptr)
{
}

ModbusDevice::~ModbusDevice()
{
}

const QJsonObject &ModbusDevice::getConfig() const
{
    return m_config;
}

void ModbusDevice::disconnectDevice()
{
    if (m_serialPort && m_serialPort->isOpen()) {
        m_serialPort->close();
    }
    if (m_tcpSocket && m_tcpSocket->isOpen()) {
        m_tcpSocket->disconnectFromHost();
    }
    setConnected(false);
}

void ModbusDevice::sendData(const QByteArray& data)
{
    if (m_serialPort && m_serialPort->isOpen()) {
        m_serialPort->write(data);
    } else if (m_tcpSocket && m_tcpSocket->isOpen()) {
        m_tcpSocket->write(data);
    }
}

void ModbusDevice::onReadyRead()
{
    QByteArray data;
    if (m_serialPort && m_serialPort->bytesAvailable() > 0) {
        data = m_serialPort->readAll();
    } else if (m_tcpSocket && m_tcpSocket->bytesAvailable() > 0) {
        data = m_tcpSocket->readAll();
    }
    emit dataReceived(data);
}