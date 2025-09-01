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
     explicit Device(const QString& id, const QString& name, QObject *parent = nullptr);
     virtual ~Device();

     /**
      * @brief 返回设备ID
      */
     QString deviceId() const;

     /**
      * @brief 返回设备名称
      */
     QString deviceName() const;
 
     /**
      * @brief 如果设备已连接，则返回true
      */
     bool isConnected() const;
 
     /**
      * @brief 连接设备
      * @return 如果连接成功，则返回true，否则返回false
      */
 
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
     * @brief 在工作线程中执行一次性初始化。
     *        此槽函数设计为由线程的 started() 信号触发，
     *        以确保在设备对象移动到新线程后才执行关键的初始化操作，
     *        例如创建QObject子对象(如QTimer, QModbusClient)和建立信号槽连接。
     */
    virtual void initInThread() = 0;

     /**
      * @brief 连接设备。此方法负责建立与物理设备的通信。
      *        可以被重复调用以实现断线重连。
      * @return 如果连接请求成功发出，则返回true，否则返回false
      */
     virtual bool connectDevice() = 0;
     /**
      * @brief 将文本数据写入设备。必须在子类中实现。
      * @param text 要写入的文本
      */
     virtual void writeText2Device(const QString &text);

     /**
      * @brief 根据键值对写入数据。必须在子类中实现。
      * @param key 数据的键
      * @param value 要写入的值
      */
     virtual void writeData2Device(const QString &key,const QString &value) = 0;

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
     void connectedChanged(const QString& deviceId, bool connected);
 
     /**
      * @brief 数据更新时发出此信号
      * @param deviceId 设备的ID
      * @param key 数据的键
      * @param value 数据的新值
      */
     void dataUpdated(const QString& deviceId, const QString& key, const QVariant& value);

     /**
      * @brief 写日志
      * @param bytes 数据
      */
     void sig_printLog(const QByteArray &bytes, bool isWrite);
 
 protected:
     /**
      * @brief 设置设备的连接状态
      * @param connected 新的连接状态
      */
     void setConnected(bool connected);
 
 private:
     QString m_deviceId;   ///< 设备的唯一标识符
     QString m_deviceName; ///< 设备的名称
     bool m_connected;    ///< 设备的连接状态
 };

#endif // DEVICE_H
