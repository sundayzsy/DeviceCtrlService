#include "ModbusDevice.h"

/**
 * @file ModbusDevice.cpp
 * @brief ModbusDevice类的实现
 */

#include <QSerialPort>
#include <QTcpSocket>

ModbusDevice::ModbusDevice(const QString& id, const QJsonObject& config, QObject *parent)
    : Device(id, parent)
    , m_config(config)
    , m_serialPort(nullptr)
    , m_tcpSocket(nullptr)
{
}

ModbusDevice::~ModbusDevice()
{
}

bool ModbusDevice::connectDevice()
{
    QString type = m_config["modbus_type"].toString();
    if (type == "rtu") {
        m_serialPort = new QSerialPort(this);
        connect(m_serialPort, &QSerialPort::readyRead, this, &ModbusDevice::onReadyRead);
        QJsonObject rtuParams = m_config["rtu_params"].toObject();
        m_serialPort->setPortName(rtuParams["port_name"].toString());
        m_serialPort->setBaudRate(rtuParams["baud_rate"].toInt());
        m_serialPort->setDataBits(QSerialPort::Data8);
        m_serialPort->setParity(QSerialPort::NoParity);
        m_serialPort->setStopBits(QSerialPort::OneStop);
        if (m_serialPort->open(QIODevice::ReadWrite)) {
            setConnected(true);
            return true;
        }
    } else if (type == "tcp") {
        m_tcpSocket = new QTcpSocket(this);
        connect(m_tcpSocket, &QTcpSocket::readyRead, this, &ModbusDevice::onReadyRead);
        QJsonObject tcpParams = m_config["tcp_params"].toObject();
        m_tcpSocket->connectToHost(tcpParams["ip_address"].toString(), tcpParams["port"].toInt());
        if (m_tcpSocket->waitForConnected()) {
            setConnected(true);
            return true;
        }
    }
    return false;
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