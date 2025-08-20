#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <QObject>
#include <QJsonObject>
#include <QVariantMap>
#include <QByteArray>

/**
 * @brief 协议处理器基类，所有协议处理器的父类
 */
class ProtocolHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个协议处理器对象
     * @param parent 父对象
     */
    explicit ProtocolHandler(QObject *parent = nullptr);
    virtual ~ProtocolHandler();

    /**
     * @brief 初始化协议处理器
     * @param config 协议的配置
     * @return 如果初始化成功，则返回true，否则返回false
     */
    virtual bool initialize(const QJsonObject& config) = 0;

    /**
     * @brief 将数据打包成字节数组
     * @param data 要打包的数据
     * @return 打包后的数据
     */
    virtual QByteArray packData(const QVariantMap& data) = 0;

    /**
     * @brief 从字节数组中解包数据
     * @param rawData 要解包的原始数据
     * @return 解包后的数据
     */
    virtual QVariantMap unpackData(const QByteArray& rawData) = 0;

signals:
    /**
     * @brief 解析数据时发出此信号
     * @param data 解析后的数据
     */
    void dataParsed(const QVariantMap& data);
};

#endif // PROTOCOLHANDLER_H