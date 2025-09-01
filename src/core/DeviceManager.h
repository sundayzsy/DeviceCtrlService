#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QJsonObject>

class Device;
class QThread;

/**
 * @brief 设备管理器类，管理系统中的所有设备
 */
class DeviceManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个设备管理器对象
     * @param parent 父对象
     */
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();

    /**
     * @brief 清理所有设备和线程，应在程序关闭前调用
     */
    void cleanup();

    /**
     * @brief 向系统添加一个新设备
     * @param config 设备的配置
     * @return 如果设备添加成功，则返回true，否则返回false
     */
    bool addDevice(const QJsonObject& config);

    /**
     * @brief 从系统中移除一个设备
     * @param id 要移除的设备的ID
     */
    void removeDevice(const QString& id);

    /**
     * @brief 返回具有指定ID的设备
     * @param id 设备的ID
     * @return 指向设备的指针，如果未找到则为nullptr
     */
    Device* getDevice(const QString& id) const;

    /**
     * @brief 返回系统中所有设备的列表
     */
    QList<Device*> getAllDevices() const;

    /**
     * @brief 注册设备与其工作线程的关联关系
     * @param deviceId 设备ID
     * @param thread 线程指针
     */
    void registerDeviceThread(const QString& deviceId, QThread* thread);

    /**
     * @brief 获取指定设备ID关联的工作线程
     * @param deviceId 设备ID
     * @return 线程指针，如果未找到则为nullptr
     */
    QThread* getDeviceThread(const QString& deviceId) const;


private:
    QMap<QString, Device*> m_devices; ///< 设备映射表，以设备ID为键
    QMap<QString, QThread*> m_deviceThreads; ///< 设备ID到线程指针的映射
};

#endif // DEVICEMANAGER_H