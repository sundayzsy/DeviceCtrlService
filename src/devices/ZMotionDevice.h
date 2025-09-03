#ifndef ZMOTIONDEVICE_H
#define ZMOTIONDEVICE_H

#include "core/Device.h"
#include <QJsonObject>
#include <QMap>
#include <QTimer>
#include "zmotion.h" // 包含ZMotion库的基础定义

/**
 * @brief ZMotion运动控制卡设备类
 */
class ZMotionDevice : public Device
{
    Q_OBJECT

public:
    explicit ZMotionDevice(const QString& id, const QString& name, const QJsonObject& config, QObject *parent = nullptr);
    ~ZMotionDevice();

    // 重写基类虚函数
    bool connectDevice() override;
    void disconnectDevice() override;
    const QJsonObject& getConfig() const override;
    void writeData2Device(const QString &key, const QString &value) override;
    void writeText2Device(const QString &text) override;

public slots:
    void initInThread() override;
    void stop() override;

    // 新的轴控制接口
    void setAxisParameters(int axisId, double units, double speed, double accel, double decel, double sramp);
    void moveContinuous(int axisId, int direction); // direction: 1 for positive, -1 for negative
    void moveRelative(int axisId, double distance);
    void startHoming(int axisId, int mode, int homingIoPort, bool invertIo, double creepSpeed);
    void stopAxis(int axisId);
    void zeroPosition(int axisId);
    void setDigitalOutput(int outputId, bool state);

private slots:
    void onStatusTimer();

private:
    // 轴控制功能
    void stopAllAxes();

    // IO控制功能
    bool getInput(int inputId);
    
    // 状态读取和数据处理
    void readAllAxisStatus();
    void readAllIOStatus();
    void updateAxisData(int axisId, const QString& parameter, const QVariant& value);
    void updateIOData(int ioId, const QString& type, bool state);
    
    // 错误处理
    void handleZMotionError(int errorCode, const QString& operation);
    QString getZMotionErrorString(int errorCode);

private:
    QJsonObject m_config;           // 设备配置
    QTimer* m_statusTimer;          // 状态轮询定时器
    
    // ZMotion连接
    ZMC_HANDLE m_zmcHandle;         // ZMotion连接句柄
    
    // 轴配置 (构造函数中从config读取)
    QList<int> m_enabledAxes;       // 启用的轴列表
    
    // 状态数据缓存 (运行时需要的)
    QMap<int, double> m_axisPositions;      // 轴当前位置
    QMap<int, int> m_axisStatus;            // 轴状态
    QMap<int, bool> m_inputStates;          // 输入IO状态
    QMap<int, bool> m_outputStates;         // 输出IO状态
};

#endif // ZMOTIONDEVICE_H
