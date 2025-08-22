#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QModbusRtuSerialSlave>
#include <QModbusTcpServer>
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
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_modbusRtuSlave(nullptr)
    , m_modbusTcpServer(nullptr)
{
    ui->setupUi(this);

    // 设置分割器的初始大小，让日志区域更小
    ui->splitter->setSizes({400, 100});

    initSerialPorts();

    // 填充配置文件下拉列表
    QDir configDir(QCoreApplication::applicationDirPath() + "/config");
    QStringList nameFilters;
    nameFilters << "*.json";
    QStringList configFiles = configDir.entryList(nameFilters, QDir::Files | QDir::NoDotAndDotDot);
    ui->configFileComboBox->addItems(configFiles);

    // 连接信号和槽
    connect(ui->protocolComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onProtocolChanged);
    connect(ui->configFileComboBox, &QComboBox::currentTextChanged, this, &MainWindow::onConfigFileChanged);

    // 初始加载
    if (!configFiles.isEmpty()) {
        onConfigFileChanged(configFiles.first());
    }
    onProtocolChanged(ui->protocolComboBox->currentIndex());
}

MainWindow::~MainWindow()
{
    if (m_modbusRtuSlave)
        m_modbusRtuSlave->disconnectDevice();
    if (m_modbusTcpServer)
        m_modbusTcpServer->disconnectDevice();
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
    // 清理旧数据
    m_dataMap.clear();
    m_keyIndexMap.clear();
    m_uiRowMap.clear();
    m_addressOrder.clear();
    ui->registerTableWidget->setRowCount(0);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open config file: " + file.errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject rootObj = doc.object();

    // 根据配置文件设置协议
    QString protocol = rootObj.value("protocol").toString("modbus_rtu");
    if (protocol.toLower() == "modbus_tcp") {
        ui->protocolComboBox->setCurrentIndex(1);
        QJsonObject tcpParams = rootObj.value("tcp_params").toObject();
        ui->ipAddressEdit->setText(tcpParams.value("ip_address").toString("127.0.0.1"));
        ui->portEdit->setText(QString::number(tcpParams.value("port").toInt(502)));
        ui->tcpSlaveIdEdit->setText(QString::number(rootObj.value("server_address").toInt(1)));
    } else {
        ui->protocolComboBox->setCurrentIndex(0);
        QJsonObject rtuParams = rootObj.value("rtu_params").toObject();
        ui->portComboBox->setCurrentText(rtuParams.value("port_name").toString());
        ui->slaveIdEdit->setText(QString::number(rootObj.value("server_address").toInt(1)));
    }

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
    ui->registerTableWidget->setColumnCount(7);
    ui->registerTableWidget->setHorizontalHeaderLabels({"Address", "Bitpos", "Length", "Key", "Name", "Access", "Value"});
    QHeaderView* header = ui->registerTableWidget->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Address
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Bitpos
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Length
    header->setSectionResizeMode(3, QHeaderView::Stretch);          // Key
    header->setSectionResizeMode(4, QHeaderView::Stretch);          // Name
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents); // Access
    header->setSectionResizeMode(6, QHeaderView::Stretch);          // Value

    // 按照 m_addressOrder 中记录的顺序来填充UI
    for (quint16 address : m_addressOrder) {
        const ModbusSturct& modbusStruct = m_dataMap.value(address);
        for (const ModbusParameter& param : modbusStruct.spList) {
            int row = ui->registerTableWidget->rowCount();
            ui->registerTableWidget->insertRow(row);

            auto* addressItem = new QTableWidgetItem(QString::number(param.address));
            auto* bitposItem = new QTableWidgetItem(QString::number(param.bitpos));
            auto* lengthItem = new QTableWidgetItem(QString::number(param.length));
            auto* keyItem = new QTableWidgetItem(param.key);
            auto* nameItem = new QTableWidgetItem(param.name);
            auto* accessItem = new QTableWidgetItem(param.access);
            auto* valueItem = new QTableWidgetItem(QString::number(param.value));

            addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
            bitposItem->setFlags(bitposItem->flags() & ~Qt::ItemIsEditable);
            lengthItem->setFlags(lengthItem->flags() & ~Qt::ItemIsEditable);
            keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);

            // 从站逻辑：如果主站可以写，UI上就不能编辑。如果主站只读，UI上可以编辑以模拟传感器变化。
            if (param.access.contains("write")) {
                valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
            }

            ui->registerTableWidget->setItem(row, 0, addressItem);
            ui->registerTableWidget->setItem(row, 1, bitposItem);
            ui->registerTableWidget->setItem(row, 2, lengthItem);
            ui->registerTableWidget->setItem(row, 3, keyItem);
            ui->registerTableWidget->setItem(row, 4, nameItem);
            ui->registerTableWidget->setItem(row, 5, accessItem);
            ui->registerTableWidget->setItem(row, 6, valueItem);

            m_uiRowMap[param.key] = row;
        }
    }
    connect(ui->registerTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
}

void MainWindow::setupModbusMap()
{
    QModbusServer *currentServer = (ui->protocolComboBox->currentIndex() == 0) ? static_cast<QModbusServer*>(m_modbusRtuSlave) : static_cast<QModbusServer*>(m_modbusTcpServer);
    if (!currentServer) return;

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

    currentServer->setMap(regMap);

    // 3. Fill the map with initial values
    for (auto it = m_dataMap.constBegin(); it != m_dataMap.constEnd(); ++it) {
        updateSlaveData(it.key());
    }
}

void MainWindow::on_connectButton_clicked()
{
    if (ui->protocolComboBox->currentIndex() == 0) { // RTU
        if (!m_modbusRtuSlave) {
            m_modbusRtuSlave = new QModbusRtuSerialSlave(this);
            connect(m_modbusRtuSlave, &QModbusServer::dataWritten, this, &MainWindow::onDataWritten);
        }
        setupModbusMap(); // Always setup map before connect

        if (m_modbusRtuSlave->state() == QModbusDevice::ConnectedState) {
            m_modbusRtuSlave->disconnectDevice();
            ui->connectButton->setText("Connect");
            ui->logTextEdit->append("RTU Disconnected.");
        } else {
            m_modbusRtuSlave->setConnectionParameter(QModbusDevice::SerialPortNameParameter, ui->portComboBox->currentText());
            m_modbusRtuSlave->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, 9600);
            m_modbusRtuSlave->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
            m_modbusRtuSlave->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::NoParity);
            m_modbusRtuSlave->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);
            m_modbusRtuSlave->setServerAddress(ui->slaveIdEdit->text().toInt());

            if (m_modbusRtuSlave->connectDevice()) {
                ui->connectButton->setText("Disconnect");
                ui->logTextEdit->append("RTU Connected successfully.");
            } else {
                ui->logTextEdit->append("RTU Connect failed: " + m_modbusRtuSlave->errorString());
            }
        }
    } else { // TCP
        if (!m_modbusTcpServer) {
            m_modbusTcpServer = new QModbusTcpServer(this);
            connect(m_modbusTcpServer, &QModbusServer::dataWritten, this, &MainWindow::onDataWritten);
        }
        setupModbusMap(); // Always setup map before connect

        if (m_modbusTcpServer->state() == QModbusDevice::ConnectedState) {
            m_modbusTcpServer->disconnectDevice();
            ui->connectButton->setText("Connect");
            ui->logTextEdit->append("TCP Disconnected.");
        } else {
            const QUrl url = QUrl::fromUserInput(ui->ipAddressEdit->text() + ":" + ui->portEdit->text());
            m_modbusTcpServer->setConnectionParameter(QModbusDevice::NetworkAddressParameter, url.host());
            m_modbusTcpServer->setConnectionParameter(QModbusDevice::NetworkPortParameter, url.port());
            m_modbusTcpServer->setServerAddress(ui->tcpSlaveIdEdit->text().toInt());

            if (m_modbusTcpServer->connectDevice()) {
                ui->connectButton->setText("Disconnect");
                ui->logTextEdit->append("TCP Connected successfully to " + url.toString());
            } else {
                ui->logTextEdit->append("TCP Connect failed: " + m_modbusTcpServer->errorString());
            }
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
    QModbusServer *currentServer = (ui->protocolComboBox->currentIndex() == 0) ? static_cast<QModbusServer*>(m_modbusRtuSlave) : static_cast<QModbusServer*>(m_modbusTcpServer);
    if (!currentServer) return;

    for(int i=0; i<size; ++i) {
        currentServer->data(modbusStruct.regType, address + i, &values[i]);
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
    if (column != 6) return; // Value column

    QString key = ui->registerTableWidget->item(row, 3)->text();
    if (!m_keyIndexMap.contains(key)) return;

    QPair<quint16, int> indices = m_keyIndexMap.value(key);
    quint16 address = indices.first;
    int index = indices.second;

    bool ok;
    quint64 newValue = ui->registerTableWidget->item(row, 6)->text().toULongLong(&ok);
    if (ok) {
        m_dataMap[address].spList[index].value = newValue;
        updateSlaveData(address);
        ui->logTextEdit->append(QString("User changed '%1' to %2").arg(key).arg(newValue));
    }
}

void MainWindow::updateSlaveData(quint16 address)
{
    QModbusServer *currentServer = (ui->protocolComboBox->currentIndex() == 0) ? static_cast<QModbusServer*>(m_modbusRtuSlave) : static_cast<QModbusServer*>(m_modbusTcpServer);
    if (!currentServer || !m_dataMap.contains(address)) return;

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
        currentServer->setData(modbusStruct.regType, address, static_cast<quint16>(combinedValue));
    } else if (modbusStruct.regCount == 2) {
        currentServer->setData(modbusStruct.regType, address, static_cast<quint16>(combinedValue >> 16));
        currentServer->setData(modbusStruct.regType, address + 1, static_cast<quint16>(combinedValue & 0xFFFF));
    } else if (modbusStruct.regCount == 4) {
        currentServer->setData(modbusStruct.regType, address,     static_cast<quint16>(combinedValue >> 48));
        currentServer->setData(modbusStruct.regType, address + 1, static_cast<quint16>((combinedValue >> 32) & 0xFFFF));
        currentServer->setData(modbusStruct.regType, address + 2, static_cast<quint16>((combinedValue >> 16) & 0xFFFF));
        currentServer->setData(modbusStruct.regType, address + 3, static_cast<quint16>(combinedValue & 0xFFFF));
    }
}

void MainWindow::updateUI(const QString& key, const QVariant& value)
{
    if (!m_uiRowMap.contains(key)) return;
    int row = m_uiRowMap[key];

    bool oldSignalsState = ui->registerTableWidget->blockSignals(true);
    ui->registerTableWidget->item(row, 6)->setText(value.toString());
    ui->registerTableWidget->blockSignals(oldSignalsState);
}

void MainWindow::onProtocolChanged(int index)
{
    ui->connectionStackedWidget->setCurrentIndex(index);
}

void MainWindow::onConfigFileChanged(const QString &fileName)
{
    if (fileName.isEmpty()) return;
    QString configPath = QDir(QCoreApplication::applicationDirPath()).filePath("config/" + fileName);
    initDataMap(configPath);
    setupUIFromDataMap();
    // After loading data, setup the modbus map for the currently selected protocol
    if (ui->protocolComboBox->currentIndex() == 0 && m_modbusRtuSlave) {
        setupModbusMap();
    } else if (ui->protocolComboBox->currentIndex() == 1 && m_modbusTcpServer) {
        setupModbusMap();
    }
}
