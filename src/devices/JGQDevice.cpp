#include "JGQDevice.h"
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QTimer>
#include <QVariant>
#include <QDebug>
#include <QJsonArray>

JGQDevice::JGQDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent)
    : Device(id, name, parent)
    , m_config(config)
    , m_modbusDevice(nullptr) // 初始化为空指针
    , m_requestTimer(nullptr) // 初始化为空指针
{
    initDataMap();
    m_serverAddress = m_config["server_address"].toInt();
}

JGQDevice::~JGQDevice()
{
}

void JGQDevice::initInThread()
{
    // 创建和配置 Modbus 客户端
    m_modbusDevice = new QModbusTcpClient(this);
    QJsonObject tcpParams = m_config["tcp_params"].toObject();
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkAddressParameter, tcpParams["ip_address"].toString());
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkPortParameter, tcpParams["port"].toInt());

    QJsonObject protocolParams = m_config["protocol_params"].toObject();
    m_modbusDevice->setTimeout(protocolParams["response_timeout"].toInt());
    m_modbusDevice->setNumberOfRetries(protocolParams["retry_count"].toInt());

    connect(m_modbusDevice, &QModbusClient::stateChanged, this, &JGQDevice::onStateChanged);

    // 创建和配置请求定时器
    m_requestTimer = new QTimer(this);
    connect(m_requestTimer, &QTimer::timeout, this, &JGQDevice::processRequestQueue);
    m_requestTimer->setInterval(50); // 帧间隔50ms
    m_requestTimer->setSingleShot(true);
}

void JGQDevice::stop()
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

void JGQDevice::writeData2Device(const QString &key, const QString &value)
{
    // 1. 检查 key 是否存在
    if (!m_keyIndexMap.contains(key)) {
        qWarning() << "JGQDevice::writeData2Device: Key not found:" << key;
        return;
    }

    // 2. 从 m_keyIndexMap 获取 address 和 index
    QPair<quint16, int> indices = m_keyIndexMap.value(key);
    quint16 address = indices.first;
    int index = indices.second;

    // 3. 更新 m_dataMap 中的值
    auto it = m_dataMap.find(address);
    if (it != m_dataMap.end())
    {
        if (index >= 0 && index < it.value().spList.size())
        {
            bool conversionOk = false;
            quint64 numericValue = value.toULongLong(&conversionOk);
            if (conversionOk)
            {
                it.value().spList[index].value = numericValue;
            }
        }
    }
}

bool JGQDevice::connectDevice()
{
    if (!m_modbusDevice)
        return false;

    if (m_modbusDevice->state() != QModbusDevice::UnconnectedState) {
        m_modbusDevice->disconnectDevice();
    }
    
    // 复用已有的m_modbusDevice实例进行连接
    return m_modbusDevice->connectDevice();
}

void JGQDevice::disconnectDevice()
{
    if (m_modbusDevice)
        m_modbusDevice->disconnectDevice();
}

void JGQDevice::onStateChanged(int state)
{
    setConnected(state == QModbusDevice::ConnectedState);
    if (isConnected()) {
        // 连接成功后，启动第一次请求处理
        processRequestQueue();
    }
}

void JGQDevice::onReadReady()
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
        }
        else if(unit.valueCount() == 2)
        {
            quint16 regAddr = unit.startAddress();
            quint16 regValueHigh = unit.value(0);
            quint16 regValueLow = unit.value(1);
            quint64 regValueCombine = (static_cast<quint32>(regValueHigh) << 16) | regValueLow;
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
        qDebug()<<QString("JGQDevice：Read response ProtocolError: %1 (Mobus exception: 0x%2)")
                        .arg(reply->errorString())
                        .arg(reply->rawResult().exceptionCode());
    }
    else
    {
        qDebug()<<QString("JGQDevice：Read response error: %1 (code: 0x%2)")
                        .arg(reply->errorString())
                        .arg(reply->error());
    }

    reply->deleteLater();
}

void JGQDevice::processRequestQueue()
{
    if (!isConnected())
        return;

    if (m_requestQueue.isEmpty()) {
        generatePollingRequests();
        if (m_requestQueue.isEmpty())
            return;
    }
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

void JGQDevice::generatePollingRequests()
{
    QMap<quint16, ModbusSturct>::iterator itr = m_dataMap.begin();
    while(itr != m_dataMap.end())
    {
        m_requestQueue.enqueue(itr.value());
        itr++;
    }
}

void JGQDevice::initDataMap()
{
    QJsonArray registers = m_config["registers"].toArray();
    for (const QJsonValue& value : registers)
    {
        QJsonObject obj = value.toObject();
        quint16 address = obj["address"].toInt();
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
            int newIndex = m_dataMap[address].spList.size() - 1;
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
            m_keyIndexMap[key] = qMakePair(address, 0);
        }
    }
}

QModbusDataUnit JGQDevice::readRequest(QModbusDataUnit::RegisterType regType, quint16 qRegAddr, int iRegCount) const
{
    return QModbusDataUnit(regType, qRegAddr, iRegCount);
}

QModbusDataUnit JGQDevice::writeRequest(QModbusDataUnit::RegisterType regType,quint16 qRegAddr, int iRegCount) const
{
    return QModbusDataUnit(regType, qRegAddr, iRegCount);
}

QVector<quint16> JGQDevice::getWriteRegValues(quint16 qRegAddr)
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

void JGQDevice::updateParamValue(quint16 qRegAddr, quint64 qRegValue)
{
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

void JGQDevice::sendReadRequest(const ModbusSturct& infoStruct)
{
    if (auto *reply = m_modbusDevice->sendReadRequest(readRequest(infoStruct.regType,infoStruct.address,infoStruct.regCount), m_serverAddress))
    {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &JGQDevice::onReadReady);
        else
            delete reply; // broadcast replies return immediately
    }
}

void JGQDevice::sendWriteRequest(const ModbusSturct &infoStruct)
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
                    qDebug()<<QString("JGQDevice：Write response error: %1 (Mobus exception: 0x%2)")
                                    .arg(reply->errorString())
                                    .arg(reply->rawResult().exceptionCode());
                }
                else if (reply->error() != QModbusDevice::NoError)
                {
                    qDebug()<<QString("JGQDevice：Write response error: %1 (code: 0x%2)")
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
        qDebug()<<"JGQDevice：Write error: " + m_modbusDevice->errorString();
    }
}

const QJsonObject& JGQDevice::getConfig() const
{
    return m_config;
}
