#ifndef DEVICE_H
#define DEVICE_H

#include <QObject>
#include <QString>
#include <QJsonObject>

/**
 * @brief 设备基类，所有设备的父类
 */
class Device : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个设备对象
     * @param id 设备的唯一标识符
     * @param parent 父对象
     */
    explicit Device(const QString& id, QObject *parent = nullptr);
    virtual ~Device();

    virtual void writeData2Device(const QString &key,const QString &value) = 0;
    /**
     * @brief 返回设备ID
     */
    QString deviceId() const;

    /**
     * @brief 如果设备已连接，则返回true
     */
    bool isConnected() const;

    /**
     * @brief 连接设备
     * @return 如果连接成功，则返回true，否则返回false
     */
    virtual bool connectDevice() = 0;

    /**
     * @brief 断开设备连接
     */
    virtual void disconnectDevice() = 0;

    /**
     * @brief 返回设备的配置
     */
    virtual const QJsonObject& getConfig() const = 0;

public slots:
    /**
     * @brief 停止设备的工作。此槽函数设计为在设备所属线程中被调用。
     *        主要用于在程序退出前，安全地停止设备内部的定时器等资源。
     */
    virtual void stop() {}

signals:
    /**
     * @brief 从设备接收到数据时发出此信号
     * @param data 接收到的数据
     */
    void dataReceived(const QByteArray& data);

    /**
     * @brief 当设备的连接状态更改时发出此信号
     * @param connected 如果已连接，则为true，否则为false
     */
    void connectionStatusChanged(bool connected);

    /**
     * @brief 数据更新时发出此信号
     * @param deviceId 设备的ID
     * @param key 数据的键
     * @param value 数据的新值
     */
    void dataUpdated(const QString& deviceId, const QString& key, const QVariant& value);

protected:
    /**
     * @brief 设置设备的连接状态
     * @param connected 新的连接状态
     */
    void setConnected(bool connected);

private:
    QString m_deviceId; ///< 设备的唯一标识符
    bool m_connected;   ///< 设备的连接状态
};

#endif // DEVICE_H
