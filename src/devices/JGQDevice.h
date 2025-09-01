#ifndef JGQDEVICE_H
#define JGQDEVICE_H

#include "core/Device.h"
#include <QJsonObject>
#include <QModbusDataUnit>
#include <QQueue>
#include "modbusdata.h"

class QModbusTcpClient;
class QTimer;

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
    explicit JGQDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent = nullptr);
    ~JGQDevice();
    void writeData2Device(const QString &key,const QString &value);
    bool connectDevice() override;
    void disconnectDevice() override;
    const QJsonObject& getConfig() const override;

public slots:
    void initInThread() override;
    void stop() override;

private slots:
    void onStateChanged(int state);
    void onReadReady();
    void processRequestQueue();

private:
    void sendReadRequest(const ModbusSturct &infoStruct);
    void sendWriteRequest(const ModbusSturct &infoStruct);
    void generatePollingRequests();
    void initDataMap();
    QModbusDataUnit readRequest(QModbusDataUnit::RegisterType regType, quint16 qRegAddr, int iRegCount) const;
    QModbusDataUnit writeRequest(QModbusDataUnit::RegisterType regType,quint16 qRegAddr, int iRegCount) const;
    QVector<quint16> getWriteRegValues(quint16 qRegAddr);
    //qRegAddr：寄存器地址  qRegValue：寄存器值
    void updateParamValue(quint16 qRegAddr, quint64 qRegValue);



    QJsonObject m_config;                   ///< 设备的配置
    QModbusTcpClient* m_modbusDevice;   ///< Modbus TCP客户端
    int m_serverAddress;                    ///< Modbus从站地址
    QQueue<ModbusSturct> m_requestQueue;    ///< 请求队列
    QTimer* m_requestTimer;                 ///< 用于控制帧间隔的定时器
    QMap<quint16,ModbusSturct> m_dataMap;  //保存参数Map Key:寄存器地址 QList<SignalParameter>寄存器下对应的参数列表

    //键是 ModbusParameter 的 key ,值 ModbusSturct  address（键），spList 中的索引。
    QMap<QString, QPair<quint16, int>> m_keyIndexMap ;
};

#endif // JGQDEVICE_H
