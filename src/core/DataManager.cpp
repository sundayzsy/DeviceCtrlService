#include "DataManager.h"

/**
 * @file DataManager.cpp
 * @brief DataManager类的实现
 */

DataManager::DataManager(QObject *parent)
    : QObject(parent)
{
}

DataManager::~DataManager()
{
}

void DataManager::updateDeviceData(const QString& deviceId, const QString& key, const QVariant& value)
{
    QWriteLocker locker(&m_lock);
    m_data[deviceId][key] = value;
    emit dataUpdated(deviceId, key, value);
}

QVariant DataManager::getDeviceData(const QString& deviceId, const QString& key) const
{
    QReadLocker locker(&m_lock);
    return m_data.value(deviceId).value(key);
}

QMap<QString, QVariant> DataManager::getDeviceData(const QString& deviceId) const
{
    QReadLocker locker(&m_lock);
    return m_data.value(deviceId);
}

QMap<QString, QMap<QString, QVariant>> DataManager::getAllData() const
{
    QReadLocker locker(&m_lock);
    return m_data;
}