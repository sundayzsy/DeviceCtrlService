#ifndef MODBUSDEVICE_H
#define MODBUSDEVICE_H

#include "core/Device.h"
#include <QJsonObject>
#include <QSerialPort>
#include <QTcpSocket>

/**
 * @brief Modbus设备类，表示一个Modbus设备
 */
class ModbusDevice : public Device
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个ModbusDevice对象
     * @param id 设备的唯一标识符
     * @param config 设备的配置
     * @param parent 父对象
     */
    explicit ModbusDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent = nullptr);
    ~ModbusDevice();

    bool connectDevice() override;
    void disconnectDevice() override;
    const QJsonObject& getConfig() const override;
    void sendData(const QByteArray& data);

private slots:
    /**
     * @brief 当设备有数据可读时调用此槽
     */
    void onReadyRead();

private:
    QJsonObject m_config;       ///< 设备的配置
    QSerialPort* m_serialPort;  ///< 用于Modbus RTU的串口
    QTcpSocket* m_tcpSocket;    ///< 用于Modbus TCP的TCP套接字
};

#endif // MODBUSDEVICE_H
