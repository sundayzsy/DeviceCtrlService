#include "Device.h"

/**
 * @file Device.cpp
 * @brief Device类的实现
 */

Device::Device(const QString& id, QObject *parent)
    : QObject(parent)
    , m_deviceId(id)
    , m_connected(false)
{
}

Device::~Device()
{
}

QString Device::deviceId() const
{
    return m_deviceId;
}

bool Device::isConnected() const
{
    return m_connected;
}

void Device::setConnected(bool connected)
{
    if (m_connected != connected) {
        m_connected = connected;
        emit connectionStatusChanged(m_connected);
    }
}
