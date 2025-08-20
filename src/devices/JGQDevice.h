#ifndef JGQDEVICE_H
#define JGQDEVICE_H

#include "core/Device.h"
#include <QJsonObject>

class QModbusTcpClient;

/**
 * @brief 激光器设备类
 */
class JGQDevice : public Device
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个JGQDevice对象
     * @param id 设备的唯一标识符
     * @param config 设备的配置
     * @param parent 父对象
     */
    explicit JGQDevice(const QString& id, const QJsonObject& config, QObject *parent = nullptr);
    ~JGQDevice();
    void writeData2Device(const QString &key,const QString &value);
    bool connectDevice() override;
    void disconnectDevice() override;
    const QJsonObject& getConfig() const override;

private slots:
    void onStateChanged(int state);
    void onReadReady();

private:
    QJsonObject m_config;               ///< 设备的配置
    QModbusTcpClient* m_modbusDevice;   ///< Modbus TCP客户端
};

#endif // JGQDEVICE_H
