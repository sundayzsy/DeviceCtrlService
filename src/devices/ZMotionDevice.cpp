#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTime>
#include "zauxdll2.h" // 包含ZMotion库的函数声明
#include "ZMotionDevice.h"

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
    
    // processCommand(key, value); // 不再使用基于字符串的命令处理
    QString logMsg = QString("ZMotion writeData2Device received: key=%1, value=%2. This path is deprecated for UI control.").arg(key).arg(value);
    emit sig_printLog(logMsg.toUtf8(), true);
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

void ZMotionDevice::setAxisParameters(int axisId, double units, double speed, double accel, double decel, double sramp)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }

    int result;

    // 设置脉冲当量
    result = ZAux_Direct_SetUnits(m_zmcHandle, axisId, units);
    if (result != 0) {
        handleZMotionError(result, QString("SetUnits Axis%1").arg(axisId));
    }

    // 设置速度
    result = ZAux_Direct_SetSpeed(m_zmcHandle, axisId, speed);
    if (result != 0) {
        handleZMotionError(result, QString("SetSpeed Axis%1").arg(axisId));
    }

    // 设置加速度
    result = ZAux_Direct_SetAccel(m_zmcHandle, axisId, accel);
    if (result != 0) {
        handleZMotionError(result, QString("SetAccel Axis%1").arg(axisId));
    }

    // 设置减速度
    result = ZAux_Direct_SetDecel(m_zmcHandle, axisId, decel);
    if (result != 0) {
        handleZMotionError(result, QString("SetDecel Axis%1").arg(axisId));
    }

    // 设置S曲线时间
    result = ZAux_Direct_SetSramp(m_zmcHandle, axisId, sramp);
    if (result != 0) {
        handleZMotionError(result, QString("SetSramp Axis%1").arg(axisId));
    }

    QString logMsg = QString("Axis%1 params set: Units=%2, Speed=%3, Accel=%4, Decel=%5, S-Ramp=%6")
                         .arg(axisId).arg(units).arg(speed).arg(accel).arg(decel).arg(sramp);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::moveContinuous(int axisId, int direction)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }
    
    // 检查轴是否空闲
    int isIdle = 0;
    if (ZAux_Direct_GetIfIdle(m_zmcHandle, axisId, &isIdle) == 0 && isIdle == 0) {
        qWarning() << QString("ZMotion Axis %1 is busy, 'moveContinuous' command ignored.").arg(axisId);
        return;
    }

    // direction: 1 for positive, -1 for negative. API: 1 for positive, 0 for negative.
    int apiDirection = (direction > 0) ? 1 : 0;
    
    int result = ZAux_Direct_Single_Vmove(m_zmcHandle, axisId, apiDirection);
    if (result != 0) {
        handleZMotionError(result, QString("VMove Axis%1").arg(axisId));
        return;
    }

    QString dirStr = (direction > 0) ? "positive" : "negative";
    QString logMsg = QString("Axis%1 continuous move started in %2 direction").arg(axisId).arg(dirStr);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::moveRelative(int axisId, double distance)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }

    // 检查轴是否空闲
    int isIdle = 0;
    if (ZAux_Direct_GetIfIdle(m_zmcHandle, axisId, &isIdle) == 0 && isIdle == 0) {
        qWarning() << QString("ZMotion Axis %1 is busy, 'moveRelative' command ignored.").arg(axisId);
        return;
    }

    // ZAux库没有直接的相对运动指令，因此我们通过计算实现
    // 1. 获取当前位置
    float currentPos = 0.0f;
    ZAux_Direct_GetDpos(m_zmcHandle, axisId, &currentPos);
    
    // 2. 计算目标绝对位置
    double targetPos = currentPos + distance;

    // 3. 使用绝对位置移动指令
    int result = ZAux_Direct_Single_Move(m_zmcHandle, axisId, targetPos);
    if (result != 0) {
        handleZMotionError(result, QString("MoveRel (via Abs) Axis%1").arg(axisId));
        return;
    }

    QString logMsg = QString("Axis%1 move relative by %2 (to %3)").arg(axisId).arg(distance).arg(targetPos);
    emit sig_printLog(logMsg.toUtf8(), true);
}

void ZMotionDevice::startHoming(int axisId, int mode, int homingIoPort, bool invertIo, double creepSpeed)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }

    // 检查轴是否空闲
    int isIdle = 0;
    if (ZAux_Direct_GetIfIdle(m_zmcHandle, axisId, &isIdle) == 0 && isIdle == 0) {
        qWarning() << QString("ZMotion Axis %1 is busy, 'startHoming' command ignored.").arg(axisId);
        return;
    }

    int result;

    // 1. 设置爬行速度 (可选，但推荐)
    result = ZAux_Direct_SetCreep(m_zmcHandle, axisId, creepSpeed);
    if (result != 0) {
        handleZMotionError(result, QString("SetCreep Axis%1").arg(axisId));
        // 此处不返回，因为不是致命错误
    }

    // 2. 设置原点开关IO口
    result = ZAux_Direct_SetDatumIn(m_zmcHandle, axisId, homingIoPort);
    if (result != 0) {
        handleZMotionError(result, QString("SetDatumIn Axis%1").arg(axisId));
        return; // 这是关键步骤，失败则不继续
    }

    // 3. 设置IO口是否反转
    // ZMC系列控制器默认原点信号为常闭(NC)，即低电平有效。如果使用常开(NO)传感器，则需要反转输入信号。
    int invertValue = invertIo ? 1 : 0;
    result = ZAux_Direct_SetInvertIn(m_zmcHandle, homingIoPort, invertValue);
	if (result != 0) {
        handleZMotionError(result, QString("SetInvertIn Port%1").arg(homingIoPort));
        return;
    }

    // 4. 设置回零模式并启动
    result = ZAux_Direct_Single_Datum(m_zmcHandle, axisId, mode);
    if (result != 0) {
        handleZMotionError(result, QString("Home Axis%1 with mode %2").arg(axisId).arg(mode));
        return;
    }

    QString logMsg = QString("Axis%1 homing started: mode=%2, IO=%3, Invert=%4, Creep=%5")
                         .arg(axisId).arg(mode).arg(homingIoPort).arg(invertIo).arg(creepSpeed);
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

void ZMotionDevice::zeroPosition(int axisId)
{
    if (!m_zmcHandle || !m_enabledAxes.contains(axisId)) {
        return;
    }

    int result = ZAux_Direct_SetDpos(m_zmcHandle, axisId, 0);
    if (result != 0) {
        handleZMotionError(result, QString("zeroPosition Axis%1").arg(axisId));
        return;
    }

    // 手动更新内部缓存和UI，因为硬件状态可能不会立即通过轮询反映出来
    m_axisPositions[axisId] = 0.0;
    updateAxisData(axisId, "position", 0.0);

    QString logMsg = QString("Axis%1 position zeroed").arg(axisId);
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


void ZMotionDevice::setDigitalOutput(int outputId, bool state)
{
    if (!m_zmcHandle) {
        return;
    }

    // state: true for ON (1), false for OFF (0)
    int result = ZAux_Direct_SetOp(m_zmcHandle, outputId, state ? 1 : 0);
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
