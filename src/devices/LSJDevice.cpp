/**
 * @file LSJDevice.cpp
 * @brief LSJDevice类的实现
 */
#include "LSJDevice.h"
#include <QModbusRtuSerialMaster>
#include <QModbusDataUnit>
#include <QTimer>
#include <QVariant>
#include <QDebug>
#include <QSerialPort>
#include <QJsonArray>
LSJDevice::LSJDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent)
    : Device(id, name, parent)
    , m_config(config)
    , m_modbusDevice(nullptr)
    , m_requestTimer(nullptr)
{
    initDataMap();
    m_serverAddress = m_config["server_address"].toInt();
}

LSJDevice::~LSJDevice()
{
}

void LSJDevice::initInThread()
{
    m_modbusDevice = new QModbusRtuSerialMaster(this);
    connect(m_modbusDevice, &QModbusClient::stateChanged, this, &LSJDevice::onStateChanged);

    QJsonObject rtuParams = m_config["rtu_params"].toObject();
    m_modbusDevice->setConnectionParameter(QModbusDevice::SerialPortNameParameter, rtuParams["port_name"].toString());
    m_modbusDevice->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, 9600);
    m_modbusDevice->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
    m_modbusDevice->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::NoParity);
    m_modbusDevice->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);

    QJsonObject protocolParams = m_config["protocol_params"].toObject();
    m_modbusDevice->setTimeout(protocolParams["response_timeout"].toInt());
    m_modbusDevice->setNumberOfRetries(protocolParams["retry_count"].toInt());

    m_requestTimer = new QTimer(this);
    m_requestTimer->setInterval(100); // 帧间隔100ms
    m_requestTimer->setSingleShot(true);
    connect(m_requestTimer, &QTimer::timeout, this, &LSJDevice::processRequestQueue);
}

void LSJDevice::stop()
{
    if (m_requestTimer && m_requestTimer->isActive()) {
        m_requestTimer->stop();
    }

    if (m_modbusDevice) {
        m_modbusDevice->disconnectDevice();
        m_modbusDevice->deleteLater(); // Use deleteLater for safety within a slot
        m_modbusDevice = nullptr;
    }
}

void LSJDevice::writeData2Device(const QString &key, const QString &value)
{
    // 1. 检查 key 是否存在
    if (!m_keyIndexMap.contains(key)) {
        qWarning() << "LSJDevice::writeData2Device: Key not found:" << key;
        return;
    }

    // 2. 从 m_keyIndexMap 获取 address 和 index
    QPair<quint16, int> indices = m_keyIndexMap.value(key);
    quint16 address = indices.first;
    int index = indices.second;

    // 3. 更新 m_dataMap 中的值
    // 注意：确保 address 和 index 是有效的。
    // 由于 m_keyIndexMap 是在 initDataMap 中构建的，理论上它们是有效的。
    // 但为了健壮性，可以再检查一次 m_dataMap 是否包含 address。
    auto it = m_dataMap.find(address);
    if (it != m_dataMap.end())
    {
        if (index >= 0 && index < it.value().spList.size())
        {
            // 执行更新。假设 ModbusParameter::value 是 quint64 类型
            // QString 转 quint64 可以使用 toULongLong() 或 toLongLong() 等，根据需要选择
            bool conversionOk = false;
            quint64 numericValue = value.toULongLong(&conversionOk);
            if (conversionOk)
            {
                it.value().spList[index].value = numericValue;
//                qDebug() << "LSJDevice::writeData2Device: Updated" << key << "at address" << address << "index" << index << "to value" << numericValue;
            }
        }
    }
}

bool LSJDevice::connectDevice()
{
    if (!m_modbusDevice)
        return false;

    if (m_modbusDevice->state() != QModbusDevice::UnconnectedState) {
        m_modbusDevice->disconnectDevice();
    }

    return m_modbusDevice->connectDevice();
}

void LSJDevice::disconnectDevice()
{
    if (m_modbusDevice)
        m_modbusDevice->disconnectDevice();
}

void LSJDevice::onStateChanged(int state)
{
    setConnected(state == QModbusDevice::ConnectedState);
    if (isConnected()) {
        // 连接成功后，启动第一次请求处理
        processRequestQueue();
    }
}

void LSJDevice::onReadReady()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError)
    {
        const QModbusDataUnit unit = reply->result();
        if(unit.valueCount() == 1)
        {
            quint16 regAddr = unit.startAddress();
            quint16 regValue = unit.value(0);
            quint64 regValueCombine = regValue;
            updateParamValue(regAddr, regValueCombine);
//            qDebug()<<"regAddr:"<<regAddr<<"regValue:"<<regValue;
        }
        else if(unit.valueCount() == 2)
        {
            //寄存器到SignalMap
            quint16 regAddr = unit.startAddress();
            quint16 regValueHigh = unit.value(0);
            quint16 regValueLow = unit.value(1);
            quint64 regValueCombine = (static_cast<quint32>(regValueHigh) << 16) | regValueLow;
//            qDebug()<<"regAddr:"<<regAddr<<"regValueHigh:"<<regValueHigh<<"regValueLow:"<<regValueLow<<"regValueCombine"<<regValueCombine;

            updateParamValue(regAddr, regValueCombine);
        }
        else if(unit.valueCount() == 4)
        {
            quint16 regAddr = unit.startAddress();
            quint16 regValueHigh2 = unit.value(0);
            quint16 regValueHigh1 = unit.value(1);
            quint16 regValueLow2 = unit.value(2);
            quint16 regValueLow1 = unit.value(3);
            quint64 regValueCombine =   (static_cast<quint64>(regValueHigh2) << 16*3)
                                      | (static_cast<quint64>(regValueHigh1) << 16*2)
                                      | (static_cast<quint64>(regValueLow2) << 16)
                                      | (static_cast<quint64>(regValueLow1));

            updateParamValue(regAddr, regValueCombine);
        }
    }
    else if (reply->error() == QModbusDevice::ProtocolError)
    {
        qDebug()<<QString("LSJDevice：Read response ProtocolError: %1 (Mobus exception: 0x%2)")
                        .arg(reply->errorString())
                        .arg(reply->rawResult().exceptionCode());
    }
    else
    {
        qDebug()<<QString("LSJDevice：Read response error: %1 (code: 0x%2)")
                        .arg(reply->errorString())
                        .arg(reply->error());
    }

    reply->deleteLater();
}


void LSJDevice::processRequestQueue()
{
    if (!isConnected())
        return;

    if (m_requestQueue.isEmpty()) {
        generatePollingRequests();
        // 如果生成请求后队列仍为空（例如没有可读寄存器），则等待下一个写请求
        if (m_requestQueue.isEmpty())
            return;
    }
//    qDebug()<<"queue:"<<m_requestQueue.size();
    ModbusSturct infoStruct = m_requestQueue.dequeue();

    if (infoStruct.isReadReg)
    {
        sendReadRequest(infoStruct);
    }
    else
    {
        sendWriteRequest(infoStruct);
    }

    if (!m_requestTimer->isActive()) {
        m_requestTimer->start();
    }
}

void LSJDevice::generatePollingRequests()
{
    QMap<quint16, ModbusSturct>::iterator itr = m_dataMap.begin();
    while(itr != m_dataMap.end())
    {
        m_requestQueue.enqueue(itr.value());
        itr++;
    }
}

void LSJDevice::initDataMap()
{
    int addrOffSet = m_config["modbus_offset"].toInt();
    QJsonArray registers = m_config["registers"].toArray();
    for (const QJsonValue& value : registers)
    {
        QJsonObject obj = value.toObject();
        quint16 address = obj["address"].toInt() + addrOffSet;
        QString key = obj["key"].toString();
        QString name = obj["name"].toString();
        quint16 length = obj["length"].toInt();
        quint16 bitpos = obj["bitpos"].toInt();
        QString access = obj["access"].toString();
        QModbusDataUnit::RegisterType regType;
        if(obj["regtype"].toString() == "coil"){
            regType = QModbusDataUnit::Coils;
        }else if(obj["regtype"].toString() == "discrete_input"){
            regType = QModbusDataUnit::DiscreteInputs;
        }
        else if(obj["regtype"].toString() == "input_register"){
            regType = QModbusDataUnit::InputRegisters;
        }
        else if(obj["regtype"].toString() == "holding_register"){
            regType = QModbusDataUnit::HoldingRegisters;
        }else{
            regType = QModbusDataUnit::Invalid;
        }
        ModbusParameter infoParam;
        infoParam.address = address;
        infoParam.key = key;
        infoParam.name = name;
        infoParam.length = length;
        infoParam.bitpos = bitpos;
        infoParam.access = access;
        infoParam.regType = regType;
        infoParam.value = 0; // 初始化为0

        int regCount = 1;
        if(length == 32)
            regCount = 2;
        else if(length == 64)
            regCount = 4;

        bool isReadReg = true;
        if(access.contains("write"))
            isReadReg = false;

        if(m_dataMap.find(address) != m_dataMap.end())
        {
            m_dataMap[address].spList.append(infoParam);

            // 更新 m_keyIndexMap：记录新添加的 ModbusParameter 的位置
            int newIndex = m_dataMap[address].spList.size() - 1; // 新元素的索引
            m_keyIndexMap[key] = qMakePair(address, newIndex);
        }
        else
        {
            ModbusSturct infoStruct;
            infoStruct.address = address;
            infoStruct.regCount = regCount;
            infoStruct.isReadReg = isReadReg;
            infoStruct.regType = regType;
            infoStruct.spList.append(infoParam);
            m_dataMap.insert(address,infoStruct);

            // 更新 m_keyIndexMap：记录新创建的 ModbusSturct 中第一个 ModbusParameter 的位置
            // 因为是新创建的 spList，第一个元素索引必然是 0
            m_keyIndexMap[key] = qMakePair(address, 0);
        }
    }
}

QModbusDataUnit LSJDevice::readRequest(QModbusDataUnit::RegisterType regType, quint16 qRegAddr, int iRegCount) const
{
    return QModbusDataUnit(regType, qRegAddr, iRegCount);
}

QModbusDataUnit LSJDevice::writeRequest(QModbusDataUnit::RegisterType regType,quint16 qRegAddr, int iRegCount) const
{
    return QModbusDataUnit(regType, qRegAddr, iRegCount);
}

QVector<quint16> LSJDevice::getWriteRegValues(quint16 qRegAddr)
{
    QVector<quint16> regValuesList;
    QMap<quint16, ModbusSturct>::iterator itr = m_dataMap.find(qRegAddr);
    if(itr != m_dataMap.end())
    {
        int regCount = itr.value().regCount;
        QList<ModbusParameter> mList = itr.value().spList;
        if(regCount == 1)
        {
            quint16 qRegValue = 0x0;
            for(int j=0; j<mList.size();j++)
            {
                quint16 qPos = mList.at(j).bitpos;
                quint16 qBitLen = mList.at(j).length;
                quint16 setValue = mList.at(j).value;
                quint16 qNewValue = 0;
                setParamValue16(qRegValue, qPos, qBitLen, setValue, qNewValue);
                qRegValue  = qNewValue;
            }
            regValuesList.append(qRegValue);
        }
        else if(regCount == 2)
        {
            if(mList.size() > 0)
            {
                quint32 setValue = mList.at(0).value;
                quint16 qNewValueHigh = setValue >> 16;
                quint16 qNewValueLow = setValue;
                regValuesList.append(qNewValueHigh);
                regValuesList.append(qNewValueLow);
            }
        }
        else if(regCount == 4)
        {
            if(mList.size() > 0)
            {
                quint64 setValue = mList.at(0).value;
                quint16 qNewValueHigh2 = setValue >> 16*3;
                quint16 qNewValueHigh1 = setValue >> 16*2;
                quint16 qNewValueLow2 = setValue >> 16;
                quint16 qNewValueLow1 = setValue;
                regValuesList.append(qNewValueHigh2);
                regValuesList.append(qNewValueHigh1);
                regValuesList.append(qNewValueLow2);
                regValuesList.append(qNewValueLow1);
            }
        }
    }
    return regValuesList;
}

void LSJDevice::updateParamValue(quint16 qRegAddr, quint64 qRegValue)
{
    //寄存器到m_dataMap
    QMap<quint16, ModbusSturct>::iterator itr = m_dataMap.find(qRegAddr);
    if(itr != m_dataMap.end())
    {
        int regCount = itr.value().regCount;
        QList<ModbusParameter> mList = itr.value().spList;
        if(regCount == 1)
        {
            for(int j=0; j<mList.size();j++)
            {
                quint16 qPos = mList.at(j).bitpos;
                quint16 qBitLen = mList.at(j).length;
                quint16 paramValue = 0;
                bool bRet = getParamValue16(qRegValue, qPos, qBitLen, paramValue);
                if(bRet)
                {
                    m_dataMap[itr.key()].spList[j].value = paramValue;
                    QString strkey = mList.at(j).key;
                    QString strValue = QString::number(paramValue);
                    emit dataUpdated(deviceId(), strkey, strValue);
                }
            }
        }
        else if(regCount == 2 || regCount == 4)
        {
            if(mList.size() > 0)
            {
                quint64 paramValue = qRegValue;
                m_dataMap[itr.key()].spList[0].value = paramValue;
                QString strkey = mList.at(0).key;
                QString strValue = QString::number(paramValue);
                emit dataUpdated(deviceId(), strkey, strValue);
            }
        }
    }
}

void LSJDevice::sendReadRequest(const ModbusSturct& infoStruct)
{
    if (auto *reply = m_modbusDevice->sendReadRequest(readRequest(infoStruct.regType,infoStruct.address,infoStruct.regCount)
                                                          ,m_serverAddress))
    {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &LSJDevice::onReadReady);
        else
            delete reply; // broadcast replies return immediately
    }
}

void LSJDevice::sendWriteRequest(const ModbusSturct &infoStruct)
{
    QModbusDataUnit writeUnit = writeRequest(infoStruct.regType,infoStruct.address, infoStruct.regCount);
    QVector<quint16> mList = getWriteRegValues(infoStruct.address);
    writeUnit.setValues(mList);
    if (auto *reply = m_modbusDevice->sendWriteRequest(writeUnit, m_serverAddress))
    {
        if (!reply->isFinished()) {
            connect(reply, &QModbusReply::finished, this, [this, reply](){
                if (reply->error() == QModbusDevice::ProtocolError)
                {
                    qDebug()<<QString("LSJDevice：Write response error: %1 (Mobus exception: 0x%2)")
                                    .arg(reply->errorString())
                                    .arg(reply->rawResult().exceptionCode());
                }
                else if (reply->error() != QModbusDevice::NoError)
                {
                    qDebug()<<QString("LSJDevice：Write response error: %1 (code: 0x%2)")
                                    .arg(reply->errorString())
                                    .arg(reply->error(),-1,16);
                }
                reply->deleteLater();
            });
        }
        else
        {
            // broadcast replies return immediately
            reply->deleteLater();
        }
    }
    else
    {
        qDebug()<<"LSJDevice：Write error: " + m_modbusDevice->errorString();
    }
}



const QJsonObject& LSJDevice::getConfig() const
{
    return m_config;
}
