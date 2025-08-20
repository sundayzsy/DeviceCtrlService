#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QModbusRtuSerialSlave>
#include <QSerialPortInfo>
#include <QDebug>
#include <QSerialPort>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_modbusSlave(nullptr)
{
    ui->setupUi(this);
    loadConfig("../config/lsj_device.json"); // 假设配置文件在可执行文件上一级的config目录
    setupRegistersFromConfig();
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

void MainWindow::loadConfig(const QString& path)
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
        RegisterDefinition def;
        def.address = obj["address"].toInt();
        def.key = obj["key"].toString();
        def.name = obj["name"].toString();
        def.length = obj["length"].toInt();
        def.bitpos = obj["bitpos"].toInt();
        def.access = obj["access"].toString();

        if (def.length == 1) {
            // 根据Modbus规范，位操作通常对应线圈
            def.type = QModbusDataUnit::Coils;
        } else {
            def.type = QModbusDataUnit::HoldingRegisters;
        }
        m_registers.append(def);
    }
    ui->logTextEdit->append(QString("Loaded %1 register definitions.").arg(m_registers.count()));
}

void MainWindow::setupRegistersFromConfig()
{
    ui->registerTableWidget->setRowCount(m_registers.size());
    int row = 0;
    for(const auto& def : m_registers) {
        auto* addressItem = new QTableWidgetItem(QString::number(def.address));
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        auto* nameItem = new QTableWidgetItem(def.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        auto* valueItem = new QTableWidgetItem("0");
        // 如果主站可以'write'这个寄存器，那么在从站(模拟器)的UI上它应该是只读的。
        // 反之，如果主站只能'read'，那么UI上就应该是可编辑的，以模拟传感器值的变化。
        if (def.access.contains("write")) {
            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        }

        ui->registerTableWidget->setItem(row, 0, addressItem);
        ui->registerTableWidget->setItem(row, 1, nameItem);
        ui->registerTableWidget->setItem(row, 2, valueItem);
        row++;
    }
    connect(ui->registerTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
}


void MainWindow::on_connectButton_clicked()
{
    if (!m_modbusSlave) {
        m_modbusSlave = new QModbusRtuSerialSlave(this);

        QModbusDataUnitMap reg;
        // 动态构建寄存器映射
        if (!m_registers.isEmpty()) {
            // 简单处理，为每种类型找到最小和最大地址
            int minCoil = -1, maxCoil = -1;
            int minHolding = -1, maxHolding = -1;

            for(const auto& def : m_registers) {
                if (def.type == QModbusDataUnit::Coils) {
                    if (minCoil == -1 || def.address < minCoil) minCoil = def.address;
                    if (maxCoil == -1 || def.address > maxCoil) maxCoil = def.address;
                } else {
                    int endAddress = def.address + (def.length == 32 ? 1 : 0);
                    if (minHolding == -1 || def.address < minHolding) minHolding = def.address;
                    if (maxHolding == -1 || endAddress > maxHolding) maxHolding = endAddress;
                }
            }
            if (minCoil != -1) {
                reg.insert(QModbusDataUnit::Coils, { QModbusDataUnit::Coils, minCoil, quint16(maxCoil - minCoil + 1) });
            }
            if (minHolding != -1) {
                reg.insert(QModbusDataUnit::HoldingRegisters, { QModbusDataUnit::HoldingRegisters, minHolding, quint16(maxHolding - minHolding + 1) });
            }
        }
        m_modbusSlave->setMap(reg);
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
    for (int i = 0; i < size; ++i) {
        quint16 value;
        int currentAddress = address + i;
        m_modbusSlave->data(table, currentAddress, &value);

        const RegisterDefinition* def = findRegister(currentAddress, table);
        QString name = def ? def->name : "Unknown";

        ui->logTextEdit->append(QString("Written to %1 (Address %2), Value: %3")
                                .arg(name).arg(currentAddress).arg(value));

        // 更新UI
        for(int row = 0; row < ui->registerTableWidget->rowCount(); ++row) {
            if(ui->registerTableWidget->item(row, 0)->text().toInt() == currentAddress) {
                // 避免触发onTableCellChanged
                bool oldSignalsState = ui->registerTableWidget->blockSignals(true);
                ui->registerTableWidget->item(row, 2)->setText(QString::number(value));
                ui->registerTableWidget->blockSignals(oldSignalsState);
                break;
            }
        }
    }
}

void MainWindow::onTableCellChanged(int row, int column)
{
    if (column != 2 || !m_modbusSlave || m_modbusSlave->state() != QModbusDevice::ConnectedState)
        return;

    QTableWidgetItem* addressItem = ui->registerTableWidget->item(row, 0);
    if (!addressItem)
        return;
    int address = addressItem->text().toInt();
    const RegisterDefinition* def = nullptr;
    for(const auto& regDef : m_registers) {
        if (regDef.address == address) {
            def = &regDef;
            break;
        }
    }

    if (!def) {
        ui->logTextEdit->append(QString("Error: No register definition found for address %1").arg(address));
        return;
    }

    // 如果主站可以'write'这个寄存器，用户不应该在从站UI上修改它
    if (def->access.contains("write")) {
        ui->logTextEdit->append(QString("Logic Error: Register %1 (%2) is written by Master, cannot be changed in Slave UI.").arg(def->name).arg(address));
        // 恢复原值
        quint16 oldValue;
        m_modbusSlave->data(def->type, address, &oldValue);
        bool oldSignalsState = ui->registerTableWidget->blockSignals(true);
        ui->registerTableWidget->item(row, 2)->setText(QString::number(oldValue));
        ui->registerTableWidget->blockSignals(oldSignalsState);
        return;
    }

    QTableWidgetItem* valueItem = ui->registerTableWidget->item(row, 2);
    if (!valueItem)
        return;
    quint16 value = valueItem->text().toUShort();
    m_modbusSlave->setData(def->type, address, value);
    ui->logTextEdit->append(QString("User changed %1 (Address %2) to %3").arg(def->name).arg(address).arg(value));
}

const RegisterDefinition* MainWindow::findRegister(int address, QModbusDataUnit::RegisterType table) const
{
    for(const auto& def : m_registers) {
        if (def.address == address && def.type == table) {
            return &def;
        }
    }
    return nullptr;
}
