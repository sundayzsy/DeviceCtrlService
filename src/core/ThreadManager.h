#ifndef THREADMANAGER_H
#define THREADMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QThread>

class Device;
class DeviceManager;

/**
 * @brief 线程管理器类，管理所有设备线程
 */
class ThreadManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个线程管理器对象
     * @param deviceManager 设备管理器的指针
     * @param parent 父对象
     */
    explicit ThreadManager(DeviceManager* deviceManager, QObject *parent = nullptr);
    ~ThreadManager();

    /**
     * @brief 清理并停止所有线程。
     * @deprecated 该函数已废弃，请使用 DeviceManager::cleanup()
     */
    void cleanup();

    /**
     * @brief 为指定设备启动一个新线程
     * @param device 要启动线程的设备
     * @return 如果线程启动成功，则返回true，否则返回false
     */
    bool startDeviceThread(Device* device);

    /**
     * @brief 停止指定设备的线程
     * @param deviceId 要停止线程的设备的ID
     */
    void stopDeviceThread(const QString& deviceId);

signals:
    /**
     * @brief 在线程管理器即将析构、所有线程即将退出时发出。
     *        设备可以连接此信号以执行清理操作（例如，停止定时器）。
     */
    void aboutToQuit();

private:
    DeviceManager* m_deviceManager; ///< 设备管理器的指针，非所有
    QMap<QString, QThread*> m_threads; ///< 线程映射表，以设备ID为键
};

#endif // THREADMANAGER_H