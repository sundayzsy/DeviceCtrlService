#include "DeviceManager.h"
/**
 * @file DeviceManager.cpp
 * @brief DeviceManager类的实现
 */

#include "Device.h"
#include "devices/LSJDevice.h"
#include "devices/JGQDevice.h"
#include "devices/JGTDevice.h"

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
{
}

DeviceManager::~DeviceManager()
{
    qDeleteAll(m_devices);
}

bool DeviceManager::addDevice(const QJsonObject& config)
{
    QString id = config["device_id"].toString();
    QString protocol = config["protocol"].toString();

    if (id.isEmpty() || protocol.isEmpty() || m_devices.contains(id)) {
        return false;
    }

    Device* device = nullptr;
    if (protocol == "modbus_rtu") {
        device = new LSJDevice(id, config);
    } else if (protocol == "modbus_tcp") {
        device = new JGQDevice(id, config);
    } else if (protocol == "tcp_socket") {
        device = new JGTDevice(id, config);
    } else {
        // TODO: Add support for other protocols
    }

    if (device) {
        m_devices.insert(id, device);
        return true;
    }

    return false;
}

void DeviceManager::removeDevice(const QString& id)
{
    if (m_devices.contains(id)) {
        delete m_devices.take(id);
    }
}

Device* DeviceManager::getDevice(const QString& id) const
{
    return m_devices.value(id, nullptr);
}

QList<Device*> DeviceManager::getAllDevices() const
{
    return m_devices.values();
}