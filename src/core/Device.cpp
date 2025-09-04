#include "Device.h"

/**
 * @file Device.cpp
 * @brief Device类的实现
 */

 Device::Device(const QString& id, const QString& name, QObject *parent)
     : QObject(parent)
     , m_deviceId(id)
     , m_deviceName(name)
     , m_connected(false)
 {
 }
 
 Device::~Device()
 {
 }

 void Device::writeText2Device(const QString &text)
 {
     Q_UNUSED(text);
 }
 
 QString Device::deviceId() const
 {
     return m_deviceId;
 }
 
 QString Device::deviceName() const
 {
     return m_deviceName;
 }
 
 bool Device::isConnected() const
 {
     return m_connected;
 }
 
 void Device::setConnected(bool connected)
 {
     if (m_connected != connected) {
         m_connected = connected;
         emit connectedChanged(m_deviceId, m_connected);
     }
 }
