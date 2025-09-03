#ifndef ZMOTIONDEVICE_H
#define ZMOTIONDEVICE_H

#include "core/Device.h"
#include <QJsonObject>
#include <QMap>
#include <QTimer>

// ZMotion基础类型定义
#include "zmotion.h" // For ZMC_HANDLE

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

private slots:
    void onStatusTimer();

private:
    // 轴控制功能
    void moveAxisAbsolute(int axisId, double position, double speed);
    void moveAxisRelative(int axisId, double distance, double speed);
    void stopAxis(int axisId);
    void stopAllAxes();
    void homeAxis(int axisId);
    
    // IO控制功能
    void setOutput(int outputId, bool state);
    bool getInput(int inputId);
    
    // 状态读取和数据处理
    void readAllAxisStatus();
    void readAllIOStatus();
    void processCommand(const QString& key, const QString& value);
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
