#include "ThreadManager.h"
/**
 * @file ThreadManager.cpp
 * @brief ThreadManager类的实现
 */

#include "Device.h"
#include <QDebug>

ThreadManager::ThreadManager(QObject *parent)
    : QObject(parent)
{
}

ThreadManager::~ThreadManager()
{
    // 实际的清理工作在 cleanup() 中完成，这里可以为空，
    // 或者保留 qDeleteAll 以防 cleanup() 未被调用。
    // 但最佳实践是在 cleanup() 中处理所有事情。
}

void ThreadManager::cleanup()
{
    // 发送信号，让设备自行清理（如停止定时器）
    emit aboutToQuit();

    // 停止所有线程
    for (QThread* thread : m_threads) {
        thread->quit();
    }

    // 等待所有线程结束
    for (QThread* thread : m_threads) {
        if (!thread->wait(3000)) { // 等待最多3秒
            thread->terminate();
            thread->wait(); // 等待终止完成
        }
    }

    qDeleteAll(m_threads); // 删除线程对象
    m_threads.clear(); // 清空map
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
    // 线程启动时，连接设备
    connect(thread, &QThread::started, device, &Device::connectDevice);
    // 线程管理器析构时，通知设备停止工作
    connect(this, &ThreadManager::aboutToQuit, device, &Device::stop, Qt::QueuedConnection);

    m_threads.insert(device->deviceId(), thread);
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