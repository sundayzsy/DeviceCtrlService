#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QReadWriteLock>

/**
 * @brief 数据管理器类，管理来自设备的所有数据
 */
class DataManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个数据管理器对象
     * @param parent 父对象
     */
    explicit DataManager(QObject *parent = nullptr);
    ~DataManager();

    /**
     * @brief 更新特定设备的数据
     * @param deviceId 设备的ID
     * @param key 数据的键
     * @param value 数据的值
     */
    void updateDeviceData(const QString& deviceId, const QString& key, const QVariant& value);

    /**
     * @brief 返回特定设备和键的数据
     * @param deviceId 设备的ID
     * @param key 数据的键
     * @return 数据的值
     */
    QVariant getDeviceData(const QString& deviceId, const QString& key) const;

    /**
     * @brief 返回特定设备的所有数据
     * @param deviceId 设备的ID
     * @return 设备所有数据的映射表
     */
    QMap<QString, QVariant> getDeviceData(const QString& deviceId) const;

    /**
     * @brief 返回所有设备的所有数据
     * @return 所有数据的映射表
     */
    QMap<QString, QMap<QString, QVariant>> getAllData() const;

signals:
    /**
     * @brief 数据更新时发出此信号
     * @param deviceId 设备的ID
     * @param key 数据的键
     * @param value 数据的新值
     */
    void dataUpdated(const QString& deviceId, const QString& key, const QVariant& value);

private:
    QMap<QString, QMap<QString, QVariant>> m_data; ///< 来自所有设备的数据
    mutable QReadWriteLock m_lock;                  ///< 用于保护数据访问的读写锁
};

#endif // DATAMANAGER_H