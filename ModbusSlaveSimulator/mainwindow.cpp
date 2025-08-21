#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QModbusRtuSerialSlave>
#include <QSerialPortInfo>
#include <QDebug>
#include <QSerialPort>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_modbusSlave(nullptr)
{
    ui->setupUi(this);
    QString configPath = QDir(QCoreApplication::applicationDirPath()).filePath("config/lsj_device.json");
    initDataMap(configPath);
    setupUIFromDataMap();
    initSerialPorts();
}

MainWindow::~MainWindow()
{
    if (m_modbusSlave)
        m_modbusSlave->disconnectDevice();
    delete ui;
}

void MainWindow::initSerialPorts()
{
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos)
        ui->portComboBox->addItem(info.portName());
}

void MainWindow::initDataMap(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open config file: " + file.errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject rootObj = doc.object();
    QJsonArray registersArray = rootObj["registers"].toArray();

    for (const QJsonValue& val : registersArray) {
        QJsonObject obj = val.toObject();
        quint16 address = static_cast<quint16>(obj["address"].toInt());
        QString key = obj["key"].toString();

        ModbusParameter param;
        param.address = address;
        param.key = key;
        param.name = obj["name"].toString();
        param.length = static_cast<quint16>(obj["length"].toInt());
        param.bitpos = static_cast<quint16>(obj["bitpos"].toInt());
        param.access = obj["access"].toString();
        param.value = 0; // 初始值为0

        QString regTypeStr = obj["regtype"].toString();
        if (regTypeStr == "coil") param.regType = QModbusDataUnit::Coils;
        else if (regTypeStr == "discrete_input") param.regType = QModbusDataUnit::DiscreteInputs;
        else if (regTypeStr == "input_register") param.regType = QModbusDataUnit::InputRegisters;
        else if (regTypeStr == "holding_register") param.regType = QModbusDataUnit::HoldingRegisters;
        else param.regType = QModbusDataUnit::Invalid;

        int regCount = 1;
        if (param.length == 32) regCount = 2;
        else if (param.length == 64) regCount = 4;

        if (m_dataMap.contains(address)) {
            m_dataMap[address].spList.append(param);
            int newIndex = m_dataMap[address].spList.size() - 1;
            m_keyIndexMap[key] = qMakePair(address, newIndex);
        } else {
            ModbusSturct modbusStruct;
            modbusStruct.address = address;
            modbusStruct.regCount = regCount;
            modbusStruct.regType = param.regType;
            modbusStruct.spList.append(param);
            m_dataMap.insert(address, modbusStruct);
            m_keyIndexMap[key] = qMakePair(address, 0);
            // 记录地址的原始顺序
            m_addressOrder.append(address);
        }
    }
    ui->logTextEdit->append(QString("Loaded %1 unique register addresses.").arg(m_dataMap.count()));
}

void MainWindow::setupUIFromDataMap()
{
    ui->registerTableWidget->setColumnCount(5);
    ui->registerTableWidget->setHorizontalHeaderLabels({"Address", "Key", "Name", "Access", "Value"});
    ui->registerTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 按照 m_addressOrder 中记录的顺序来填充UI
    for (quint16 address : m_addressOrder) {
        const ModbusSturct& modbusStruct = m_dataMap.value(address);
        for (const ModbusParameter& param : modbusStruct.spList) {
            int row = ui->registerTableWidget->rowCount();
            ui->registerTableWidget->insertRow(row);

            auto* addressItem = new QTableWidgetItem(QString::number(param.address));
            auto* keyItem = new QTableWidgetItem(param.key);
            auto* nameItem = new QTableWidgetItem(param.name);
            auto* accessItem = new QTableWidgetItem(param.access);
            auto* valueItem = new QTableWidgetItem(QString::number(param.value));

            addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
            keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);

            // 从站逻辑：如果主站可以写，UI上就不能编辑。如果主站只读，UI上可以编辑以模拟传感器变化。
            if (param.access.contains("write")) {
                valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
            }

            ui->registerTableWidget->setItem(row, 0, addressItem);
            ui->registerTableWidget->setItem(row, 1, keyItem);
            ui->registerTableWidget->setItem(row, 2, nameItem);
            ui->registerTableWidget->setItem(row, 3, accessItem);
            ui->registerTableWidget->setItem(row, 4, valueItem);

            m_uiRowMap[param.key] = row;
        }
    }
    connect(ui->registerTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
}

void MainWindow::setupModbusMap()
{
    if (!m_modbusSlave) return;

    QModbusDataUnitMap regMap;
    QMap<QModbusDataUnit::RegisterType, QPair<int, int>> ranges; // Min/Max addresses

    // 1. Find the min and max address for each register type
    for (const auto& modbusStruct : m_dataMap) {
        if (modbusStruct.regType == QModbusDataUnit::Invalid) continue;

        int startAddress = modbusStruct.address;
        int endAddress = startAddress + modbusStruct.regCount - 1;

        if (!ranges.contains(modbusStruct.regType)) {
            ranges[modbusStruct.regType] = qMakePair(startAddress, endAddress);
        } else {
            if (startAddress < ranges[modbusStruct.regType].first) {
                ranges[modbusStruct.regType].first = startAddress;
            }
            if (endAddress > ranges[modbusStruct.regType].second) {
                ranges[modbusStruct.regType].second = endAddress;
            }
        }
    }

    // 2. Create a single DataUnit for each type covering the full range
    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it) {
        QModbusDataUnit::RegisterType type = it.key();
        int minAddr = it.value().first;
        int maxAddr = it.value().second;
        quint16 count = maxAddr - minAddr + 1;
        regMap.insert(type, { type, static_cast<quint16>(minAddr), count });
        ui->logTextEdit->append(QString("Exposing %1 from address %2 to %3 (count: %4)")
                                .arg(type).arg(minAddr).arg(maxAddr).arg(count));
    }

    m_modbusSlave->setMap(regMap);

    // 3. Fill the map with initial values
    for (auto it = m_dataMap.constBegin(); it != m_dataMap.constEnd(); ++it) {
        updateSlaveData(it.key());
    }
}

void MainWindow::on_connectButton_clicked()
{
    if (!m_modbusSlave) {
        m_modbusSlave = new QModbusRtuSerialSlave(this);
        setupModbusMap();
        connect(m_modbusSlave, &QModbusServer::dataWritten, this, &MainWindow::onDataWritten);
    }

    if (m_modbusSlave->state() == QModbusDevice::ConnectedState) {
        m_modbusSlave->disconnectDevice();
        ui->connectButton->setText("Connect");
        ui->logTextEdit->append("Disconnected.");
    } else {
        m_modbusSlave->setConnectionParameter(QModbusDevice::SerialPortNameParameter, ui->portComboBox->currentText());
        m_modbusSlave->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, 9600);
        m_modbusSlave->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
        m_modbusSlave->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::NoParity);
        m_modbusSlave->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);
        m_modbusSlave->setServerAddress(ui->slaveIdEdit->text().toInt());

        if (m_modbusSlave->connectDevice()) {
            ui->connectButton->setText("Disconnect");
            ui->logTextEdit->append("Connected successfully.");
        } else {
            ui->logTextEdit->append("Connect failed: " + m_modbusSlave->errorString());
        }
    }
}

void MainWindow::onDataWritten(QModbusDataUnit::RegisterType table, int address, int size)
{
    Q_UNUSED(table);
    ui->logTextEdit->append(QString("Master wrote %1 register(s) at address %2").arg(size).arg(address));

    // 假设写入不会跨越多个ModbusSturct边界
    if (!m_dataMap.contains(address)) {
        ui->logTextEdit->append(QString("Warning: Write to unmapped address %1").arg(address));
        return;
    }

    ModbusSturct& modbusStruct = m_dataMap[address];
    if (modbusStruct.regCount != size) {
         ui->logTextEdit->append(QString("Warning: Write size mismatch for address %1. Expected %2, got %3")
                                 .arg(address).arg(modbusStruct.regCount).arg(size));
    }

    // 从Modbus Slave读取原始数据
    QVector<quint16> values(size);
    for(int i=0; i<size; ++i) {
        m_modbusSlave->data(modbusStruct.regType, address + i, &values[i]);
    }

    // 将原始数据解析到m_dataMap
    quint64 combinedValue = 0;
    if (size == 1) combinedValue = values[0];
    else if (size == 2) combinedValue = (static_cast<quint64>(values[0]) << 16) | values[1];
    else if (size == 4) {
        combinedValue = (static_cast<quint64>(values[0]) << 48) |
                        (static_cast<quint64>(values[1]) << 32) |
                        (static_cast<quint64>(values[2]) << 16) |
                        (static_cast<quint64>(values[3]));
    }

    // 更新所有相关的ModbusParameter
    for (int i = 0; i < modbusStruct.spList.size(); ++i) {
        ModbusParameter& param = modbusStruct.spList[i];

        // 对于线圈(Coil)写入，直接使用接收到的原始值(例如0xFF00)，不进行位解析
        if (param.regType == QModbusDataUnit::Coils) {
            param.value = combinedValue;
        } else {
            quint16 paramValue = 0;
            if (param.length <= 16) {
                getParamValue16(static_cast<quint16>(combinedValue), param.bitpos, param.length, paramValue);
                param.value = paramValue;
            } else {
                param.value = combinedValue;
            }
        }
        updateUI(param.key, QVariant(param.value));
    }
}

void MainWindow::onTableCellChanged(int row, int column)
{
    if (column != 4) return; // Value column

    QString key = ui->registerTableWidget->item(row, 1)->text();
    if (!m_keyIndexMap.contains(key)) return;

    QPair<quint16, int> indices = m_keyIndexMap.value(key);
    quint16 address = indices.first;
    int index = indices.second;

    bool ok;
    quint64 newValue = ui->registerTableWidget->item(row, 4)->text().toULongLong(&ok);
    if (ok) {
        m_dataMap[address].spList[index].value = newValue;
        updateSlaveData(address);
        ui->logTextEdit->append(QString("User changed '%1' to %2").arg(key).arg(newValue));
    }
}

void MainWindow::updateSlaveData(quint16 address)
{
    if (!m_modbusSlave || !m_dataMap.contains(address)) return;

    const ModbusSturct& modbusStruct = m_dataMap.value(address);

    // 组合所有参数的值到一个或多个寄存器
    quint64 combinedValue = 0;
    if (modbusStruct.regCount > 1) { // 32-bit or 64-bit
        if (!modbusStruct.spList.isEmpty()) {
            combinedValue = modbusStruct.spList.first().value;
        }
    } else { // 16-bit packed values
        quint16 packedValue = 0;
        for (const auto& param : modbusStruct.spList) {
            setParamValue16(packedValue, param.bitpos, param.length, param.value, packedValue);
        }
        combinedValue = packedValue;
    }

    // 将组合后的值写入Modbus Slave的内部数据
    if (modbusStruct.regCount == 1) {
        m_modbusSlave->setData(modbusStruct.regType, address, static_cast<quint16>(combinedValue));
    } else if (modbusStruct.regCount == 2) {
        m_modbusSlave->setData(modbusStruct.regType, address, static_cast<quint16>(combinedValue >> 16));
        m_modbusSlave->setData(modbusStruct.regType, address + 1, static_cast<quint16>(combinedValue & 0xFFFF));
    } else if (modbusStruct.regCount == 4) {
        m_modbusSlave->setData(modbusStruct.regType, address,     static_cast<quint16>(combinedValue >> 48));
        m_modbusSlave->setData(modbusStruct.regType, address + 1, static_cast<quint16>((combinedValue >> 32) & 0xFFFF));
        m_modbusSlave->setData(modbusStruct.regType, address + 2, static_cast<quint16>((combinedValue >> 16) & 0xFFFF));
        m_modbusSlave->setData(modbusStruct.regType, address + 3, static_cast<quint16>(combinedValue & 0xFFFF));
    }
}

void MainWindow::updateUI(const QString& key, const QVariant& value)
{
    if (!m_uiRowMap.contains(key)) return;
    int row = m_uiRowMap[key];

    bool oldSignalsState = ui->registerTableWidget->blockSignals(true);
    ui->registerTableWidget->item(row, 4)->setText(value.toString());
    ui->registerTableWidget->blockSignals(oldSignalsState);
}
