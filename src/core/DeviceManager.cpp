#include "DeviceManager.h"
/**
 * @file DeviceManager.cpp
 * @brief DeviceManager类的实现
 */

#include "Device.h"
#include <QThread>
#include "devices/LSJDevice.h"
#include "devices/JGQDevice.h"
#include "devices/JGTDevice.h"
#include "devices/ZMotionDevice.h"

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
    QString name = config["device_name"].toString();
    QString protocol = config["protocol"].toString();

    if (id.isEmpty() || m_devices.contains(id)) {
        return false;
    }

    Device* device = nullptr;
    if (protocol == "modbus_rtu") {
        device = new LSJDevice(id, name, config);
    } else if (protocol == "modbus_tcp") {
        device = new JGQDevice(id, name, config);
    } else if (protocol == "tcp_socket") {
        device = new JGTDevice(id, name, config);
    } else if (protocol == "zmotion_api") {
        device = new ZMotionDevice(id, name, config);
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
        m_deviceThreads.remove(id);
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

void DeviceManager::registerDeviceThread(const QString& deviceId, QThread* thread)
{
    if (!deviceId.isEmpty() && thread) {
        m_deviceThreads.insert(deviceId, thread);
    }
}

QThread* DeviceManager::getDeviceThread(const QString& deviceId) const
{
    return m_deviceThreads.value(deviceId, nullptr);
}

void DeviceManager::cleanup()
{
    // 1. 同步、阻塞式地调用每个设备的 stop() 方法
    for (Device* device : m_devices) {
        if (!device) continue;

        QThread* thread = getDeviceThread(device->deviceId());
        if (thread && thread->isRunning()) {
            // 使用阻塞连接，确保 stop() 在其所属线程执行完毕后才返回
            QMetaObject::invokeMethod(device, "stop", Qt::BlockingQueuedConnection);
        }
    }

    // 2. 现在所有设备的定时器等资源都已停止，可以安全地退出线程事件循环
    for (QThread* thread : m_deviceThreads) {
        if (thread && thread->isRunning()) {
            thread->quit();
        }
    }

    // 3. 等待所有线程真正结束
    for (QThread* thread : m_deviceThreads) {
        if (thread && !thread->wait(3000)) { // 等待3秒
            thread->terminate(); // 强制终止
            thread->wait();
        }
    }
    
    // 4. 注意：线程对象本身由 ThreadManager 创建并设置了 deleteLater，
    //    这里不需要手动删除。DeviceManager 只负责协调。
    m_deviceThreads.clear();
}
