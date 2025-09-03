#include "ZMotionDevice.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTime>
#include "zmotion.h" // 包含ZMotion库的基础定义
#include "zauxdll2.h" // 包含ZMotion库的函数声明

// ZMotion常量定义（如果头文件中没有定义）
#ifndef ZMC_ETH
#define ZMC_ETH 1
#endif

ZMotionDevice::ZMotionDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent)
    : Device(id, name, parent)
    , m_config(config)
    , m_statusTimer(nullptr)
    , m_zmcHandle(nullptr)
{
    // 读取轴配置
    QJsonArray axesConfig = m_config["axes"].toArray();
    for (const QJsonValue& axisVal : axesConfig) {
        QJsonObject axisObj = axisVal.toObject();
        if (axisObj["enabled"].toBool()) {
            m_enabledAxes.append(axisObj["id"].toInt());
        }
    }
    QString tmpInfo = QString("ZMotionDevice created: %1 with %2 enabled axes.").arg(id).arg(m_enabledAxes.size());
    emit sig_printLog(tmpInfo.toUtf8(),false);
}

ZMotionDevice::~ZMotionDevice()
{
    if (m_zmcHandle) {
        ZAux_Close(m_zmcHandle);
        m_zmcHandle = nullptr;
    }
}

void ZMotionDevice::initInThread()
{
    // 在工作线程中创建定时器
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &ZMotionDevice::onStatusTimer);
    m_statusTimer->setInterval(500); // 500ms更新间隔
}

bool ZMotionDevice::connectDevice()
{
    // 从配置读取连接参数
    QJsonObject connParams = m_config["connection"].toObject();
    QString ipAddress = connParams["ip"].toString("192.168.1.100");
    int port = connParams["port"].toInt(8089);
    
    // 如果已经连接，先断开
    if (m_zmcHandle) {
        ZAux_Close(m_zmcHandle);
        m_zmcHandle = nullptr;
    }
    
    // 连接ZMotion控制卡
    QByteArray ipBytes = ipAddress.toLocal8Bit();
    const char* ipCStr = ipBytes.data();
    int result = ZAux_OpenEth(const_cast<char*>(ipCStr), &m_zmcHandle);

    if (result != 0) {
        handleZMotionError(result, "Connect");
        setConnected(false);
        return false;
    }
    
    setConnected(true);
    m_statusTimer->start();
    
    QString logMsg = QString("ZMotion connected to %1:%2").arg(ipAddress).arg(port);
    emit sig_printLog(logMsg.toUtf8(), false);
    
    return true;
}

void ZMotionDevice::disconnectDevice()
{
    if (m_statusTimer) {
        m_statusTimer->stop();
    }
    
    if (m_zmcHandle) {
        ZAux_Close(m_zmcHandle);
        m_zmcHandle = nullptr;
    }
    
    setConnected(false);
    emit sig_printLog("ZMotion disconnected", false);
}

const QJsonObject& ZMotionDevice::getConfig() const
{
    return m_config;
}

void ZMotionDevice::writeData2Device(const QString &key, const QString &value)
{
    if (!m_zmcHandle) {
        emit sig_printLog(QString("ZMotion not connected, cannot write %1=%2").arg(key).arg(value).toLocal8Bit(), false);
        return;
    }
    
    processCommand(key, value);
}

void ZMotionDevice::writeText2Device(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    
    // ZMotion设备不支持直接文本命令，记录日志
    QString logMsg = QString("ZMotion received text command: %1").arg(text);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::stop()
{
    disconnectDevice();
}

void ZMotionDevice::onStatusTimer()
{
    if (m_zmcHandle) {
        readAllAxisStatus();
        readAllIOStatus();
    }
}

void ZMotionDevice::moveAxisAbsolute(int axisId, double position, double speed)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }
    
    // 设置轴速度
    int result = ZAux_Direct_SetSpeed(m_zmcHandle, axisId, speed);
    if (result != 0) {
        handleZMotionError(result, QString("SetSpeed Axis%1").arg(axisId));
        return;
    }

    // 绝对位置移动
    result = ZAux_Direct_Single_Move(m_zmcHandle, axisId, position);
    if (result != 0) {
        handleZMotionError(result, QString("MoveAbs Axis%1").arg(axisId));
        return;
    }
    
    QString logMsg = QString("Axis%1 move absolute to %2 at speed %3").arg(axisId).arg(position).arg(speed);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::moveAxisRelative(int axisId, double distance, double speed)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }
    
    // 设置轴速度
    int result = ZAux_Direct_SetSpeed(m_zmcHandle, axisId, speed);
    if (result != 0) {
        handleZMotionError(result, QString("SetSpeed Axis%1").arg(axisId));
        return;
    }

    // 相对位置移动
//    result = ZAux_Direct_Single_MoveRel(m_zmcHandle, axisId, distance);
//    if (result != 0) {
//        handleZMotionError(result, QString("MoveRel Axis%1").arg(axisId));
//        return;
//    }
    
    QString logMsg = QString("Axis%1 move relative %2 at speed %3").arg(axisId).arg(distance).arg(speed);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::stopAxis(int axisId)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }
    
    // 模式2：减速停止
    int result = ZAux_Direct_Single_Cancel(m_zmcHandle, axisId, 2);
    if (result != 0) {
        handleZMotionError(result, QString("Stop Axis%1").arg(axisId));
        return;
    }
    
    QString logMsg = QString("Axis%1 stopped").arg(axisId);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::stopAllAxes()
{
    if (!m_zmcHandle) {
        return;
    }
    
    for (int axisId : m_enabledAxes) {
        // 模式2：减速停止
        ZAux_Direct_Single_Cancel(m_zmcHandle, axisId, 2);
    }
    
    emit sig_printLog("All axes emergency stopped", true);
}

void ZMotionDevice::homeAxis(int axisId)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }
    
    // ZAux库中没有直接的Home指令，这通常是一个更复杂的过程
    // 这里暂时留空或发出警告
    // int result = ZAux_Direct_Single_Home(m_zmcHandle, axisId, ...);
    emit sig_printLog(QString("Homing for axis %1 is not implemented with ZAux API yet.").arg(axisId).toLocal8Bit(), false);
    int result = 0; // 假设成功，避免错误处理
    if (result != 0) {
        handleZMotionError(result, QString("Home Axis%1").arg(axisId));
        return;
    }
    
    QString logMsg = QString("Axis%1 homing started").arg(axisId);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::setOutput(int outputId, bool state)
{
    if (!m_zmcHandle) {
        return;
    }
    
    int result = ZAux_Direct_SetOp(m_zmcHandle, outputId, state);
    if (result != 0) {
        handleZMotionError(result, QString("SetOutput %1").arg(outputId));
        return;
    }
    
    m_outputStates[outputId] = state;
    updateIOData(outputId, "output", state);
    
    QString logMsg = QString("Output%1 set to %2").arg(outputId).arg(state ? "ON" : "OFF");
    emit sig_printLog(logMsg.toUtf8(), true);
}

bool ZMotionDevice::getInput(int inputId)
{
    if (!m_zmcHandle) {
        return false;
    }
    
    uint32 value = 0;
    int result = ZAux_Direct_GetIn(m_zmcHandle, inputId, &value);
    
    if (result != 0) {
        handleZMotionError(result, QString("GetInput %1").arg(inputId));
        return false;
    }
    
    return (value != 0);
}

void ZMotionDevice::readAllAxisStatus()
{
    if (!m_zmcHandle) {
        return;
    }
    
    for (int axisId : m_enabledAxes) {
        // 读取轴位置
        float position = 0.0f;
        int result = ZAux_Direct_GetDpos(m_zmcHandle, axisId, &position);
        
        if (result == 0) {
            double oldPos = m_axisPositions.value(axisId, 0.0);
            if (qAbs(oldPos - position) > 0.001) { // 位置变化超过0.001mm才更新
                m_axisPositions[axisId] = position;
                updateAxisData(axisId, "position", position);
            }
        }
        
        // 读取轴状态
        uint32 status = 0;
        int isIdle = 0;
        result = ZAux_Direct_GetIfIdle(m_zmcHandle, axisId, &isIdle);
        
        if (result == 0) {
            // ZAux_Direct_GetIfIdle: 1表示空闲, 0表示运动
            // 为了与旧逻辑保持某种程度的一致性，我们可以定义一个简单的状态
            // 0: 运动中, 1: 停止
            int currentStatus = isIdle ? 1 : 0;
            int oldStatus = m_axisStatus.value(axisId, -1);
            if (oldStatus != currentStatus) {
                m_axisStatus[axisId] = currentStatus;
                updateAxisData(axisId, "status", currentStatus);
            }
        }
    }
}

void ZMotionDevice::readAllIOStatus()
{
    if (!m_zmcHandle) {
        return;
    }
    
    // 读取输入状态 (假设有16个输入)
    for (int i = 0; i < 16; i++) {
        uint32 value = 0;
        int result = ZAux_Direct_GetIn(m_zmcHandle, i, &value);
        
        if (result == 0) {
            bool state = (value != 0);
            bool oldState = m_inputStates.value(i, false);
            if (oldState != state) {
                m_inputStates[i] = state;
                updateIOData(i, "input", state);
            }
        }
    }
}

void ZMotionDevice::processCommand(const QString& key, const QString& value)
{
    QStringList parts = key.split('_');
    if (parts.size() < 2) {
        return;
    }
    
    QString command = parts.last();
    
    // 解析轴命令
    if (key.startsWith("axis")) {
        int axisId = parts[0].mid(4).toInt(); // 从"axis0"中提取"0"
        QStringList values = value.split(',');
        
        if (command == "move" && parts.size() >= 3) {
            QString moveType = parts[1]; // abs 或 rel
            double position = values[0].toDouble();
            double speed = values.size() > 1 ? values[1].toDouble() : 10.0;
            
            if (moveType == "abs") {
                moveAxisAbsolute(axisId, position, speed);
            } else if (moveType == "rel") {
                moveAxisRelative(axisId, position, speed);
            }
        } else if (command == "stop") {
            stopAxis(axisId);
        } else if (command == "home") {
            homeAxis(axisId);
        }
    }
    // 解析IO命令
    else if (key.startsWith("output")) {
        int outputId = key.mid(6).toInt(); // 从"output0"中提取"0"
        bool state = (value == "1" || value.toLower() == "true" || value.toLower() == "on");
        setOutput(outputId, state);
    }
    // 全局命令
    else if (key == "stop_all") {
        stopAllAxes();
    }
}

void ZMotionDevice::updateAxisData(int axisId, const QString& parameter, const QVariant& value)
{
    QString key = QString("axis%1_%2").arg(axisId).arg(parameter);
    emit dataUpdated(deviceId(), key, value);
}

void ZMotionDevice::updateIOData(int ioId, const QString& type, bool state)
{
    QString key = QString("%1%2").arg(type).arg(ioId);
    emit dataUpdated(deviceId(), key, state ? "1" : "0");
}

void ZMotionDevice::handleZMotionError(int errorCode, const QString& operation)
{
    QString errorMsg = QString("ZMotion Error in %1: Code=%2, %3")
                       .arg(operation)
                       .arg(errorCode)
                       .arg(getZMotionErrorString(errorCode));
    
    emit sig_printLog(errorMsg.toUtf8(), false);
    qWarning() << errorMsg;
}

QString ZMotionDevice::getZMotionErrorString(int errorCode)
{
    // ZMotion错误码对应的错误信息
    switch (errorCode) {
        case 0: return "Success";
        case -1: return "Communication failed";
        case -2: return "Invalid parameter";
        case -3: return "Device not found";
        case -4: return "Device busy";
        case -5: return "Timeout";
        case -6: return "Invalid axis";
        case -7: return "Axis not enabled";
        case -8: return "Motion in progress";
        case -9: return "Limit switch triggered";
        case -10: return "Emergency stop active";
        default: return QString("Unknown error (%1)").arg(errorCode);
    }
}
