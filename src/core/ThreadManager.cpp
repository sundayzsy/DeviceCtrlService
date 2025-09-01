#include "ThreadManager.h"
/**
 * @file ThreadManager.cpp
 * @brief ThreadManager类的实现
 */

#include "Device.h"
#include "core/DeviceManager.h"
#include <QDebug>

ThreadManager::ThreadManager(DeviceManager* deviceManager, QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
{
    int idealThreadCount = QThread::idealThreadCount();
    qDebug()<<"idealThreadCount:"<<idealThreadCount;
}

ThreadManager::~ThreadManager()
{
    // 实际的清理工作在 cleanup() 中完成，这里可以为空，
    // 或者保留 qDeleteAll 以防 cleanup() 未被调用。
    // 但最佳实践是在 cleanup() 中处理所有事情。
}

void ThreadManager::cleanup()
{
    // Deprecated: Cleanup is now handled by DeviceManager::cleanup()
    // This function is kept to avoid breaking old connections, but does nothing.
}

bool ThreadManager::startDeviceThread(Device* device)
{
    if (!device || m_threads.contains(device->deviceId())) {
        return false;
    }

    QThread* thread = new QThread;
    device->moveToThread(thread);

    // 线程结束时，自动删除线程对象
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    // 线程启动后，先在线程内初始化，然后连接设备
    connect(thread, &QThread::started, device, [device](){
        device->initInThread();
        device->connectDevice();
    });
    // 线程管理器析构时，通知设备停止工作
    connect(this, &ThreadManager::aboutToQuit, device, &Device::stop, Qt::QueuedConnection);

    m_threads.insert(device->deviceId(), thread);
    m_deviceManager->registerDeviceThread(device->deviceId(), thread);
    thread->start();

    return true;
}

void ThreadManager::stopDeviceThread(const QString& deviceId)
{
    if (m_threads.contains(deviceId)) {
        QThread* thread = m_threads.value(deviceId);
        thread->quit();
        thread->wait();
        m_threads.remove(deviceId);
    }
}
