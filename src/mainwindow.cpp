#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMetaObject>
#include "core/DeviceManager.h"
#include "core/ThreadManager.h"
#include "core/DataManager.h"
#include "core/Device.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QJsonArray>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>
#include <QTableWidget>
#include <QPushButton>
#include <QTime>
#include <QTextCursor>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceManager(new DeviceManager(this))
    , m_threadManager(new ThreadManager(m_deviceManager, this))
    , m_dataManager(new DataManager(this))
    , m_isInternalChange(false)
{
    ui->setupUi(this);
//    showMaximized();
    setWindowTitle("设备通信控制服务系统");

    initDeivceTableUI();
    initModbusTableUI();
    initZMotionUI();

    // 连接信号和槽
    connect(ui->dataTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
    connect(ui->deviceTableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onDeviceSelectionChanged);
    connect(m_dataManager, &DataManager::dataUpdated, this, &MainWindow::onDeviceDataUpdated);

    ui->stackedWidget->setCurrentIndex(0);

    // 自动加载默认设备
    QString configPath = QCoreApplication::applicationDirPath() + "/config/";
    loadDevice(QDir::toNativeSeparators(configPath + "lsj_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "jgq_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "jgt_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "zmotion_device.json"));
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::loadDevice(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Couldn't open device config file:" << filePath;
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc(QJsonDocument::fromJson(data));
    QJsonObject config = doc.object();
    QString deviceId = config["device_id"].toString();

    if (deviceId.isEmpty()) {
        qWarning() << "Device ID is empty in file:" << filePath;
        return;
    }

    // 防止重复加载
    if (m_deviceManager->getDevice(deviceId)) {
        qWarning() << "Device already loaded:" << deviceId;
        return;
    }

    if (m_deviceManager->addDevice(config)) {
        Device* device = m_deviceManager->getDevice(deviceId);
        if (device) {
            m_threadManager->startDeviceThread(device);
            
            int newRow = ui->deviceTableWidget->rowCount();
            ui->deviceTableWidget->insertRow(newRow);
            auto nameItem = new QTableWidgetItem(device->deviceName());
            nameItem->setData(Qt::UserRole, deviceId);
            auto statusItem = new QTableWidgetItem("连接中...");
            ui->deviceTableWidget->setItem(newRow, 0, nameItem);
            ui->deviceTableWidget->setItem(newRow, 1, statusItem);

            auto reconnectButton = new QPushButton("重连");
            ui->deviceTableWidget->setCellWidget(newRow, 2, reconnectButton);
            connect(reconnectButton, &QPushButton::clicked, this, [this, deviceId](){
                onReconnectButtonClicked(deviceId);
            });

            connect(device, &Device::dataUpdated, m_dataManager, &DataManager::updateDeviceData);
            connect(device, &Device::connectedChanged, this, &MainWindow::onDeviceConnectionChanged);
            connect(device, &Device::sig_printLog, this, &MainWindow::onPrintLog);

            // 如果这是第一个设备，则立即显示其数据
            if (ui->deviceTableWidget->rowCount() == 1) {
                ui->deviceTableWidget->selectRow(0);
            }
        }
    }
}

void MainWindow::onDeviceSelectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        QTableWidgetItem* item = selectedItems.first();
        QString deviceId = item->data(Qt::UserRole).toString();
        if(deviceId == "jgq_001" || deviceId == "lsj_001" )
        {
            ui->stackedWidget->setCurrentIndex(0);
            updateDataTable(deviceId);
        }
        else if(deviceId == "jgt_001")
        {
            ui->stackedWidget->setCurrentIndex(1);
        }
        else if(deviceId == "zmotion_001")
        {
            ui->stackedWidget->setCurrentIndex(2);
        }
    }
}

void MainWindow::updateDataTable(const QString& deviceId)
{
    Device* device = m_deviceManager->getDevice(deviceId);
    if (!device) return;

    m_isInternalChange = true;
    ui->dataTableWidget->clearContents();
    ui->dataTableWidget->setRowCount(0);
    m_dataRowMap.clear();

    const QJsonObject& config = device->getConfig();
    QJsonArray registers = config["registers"].toArray();
    for (const QJsonValue &val : registers) {
        QJsonObject obj = val.toObject();
        int address = obj["address"].toInt();
        QString key = obj["key"].toString();
        QString name = obj["name"].toString();
        QString access = obj["access"].toString();
        int bitpos = obj["bitpos"].toInt();
        int length = obj["length"].toInt();

        int newRow = ui->dataTableWidget->rowCount();
        ui->dataTableWidget->insertRow(newRow);

        auto addressItem = new QTableWidgetItem(QString::number(address));
        auto bitposItem = new QTableWidgetItem(QString::number(bitpos));
        auto lengthItem = new QTableWidgetItem(QString::number(length));
        auto keyItem = new QTableWidgetItem(key);
        auto nameItem = new QTableWidgetItem(name);
        auto accessItem = new QTableWidgetItem(access);
        auto valueItem = new QTableWidgetItem("0"); // 初始值

        // 设置只读属性
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        bitposItem->setFlags(bitposItem->flags() & ~Qt::ItemIsEditable);
        lengthItem->setFlags(lengthItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);
        if (!access.contains("write")) {
            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        }

        ui->dataTableWidget->setItem(newRow, 0, addressItem);
        ui->dataTableWidget->setItem(newRow, 1, bitposItem);
        ui->dataTableWidget->setItem(newRow, 2, lengthItem);
        ui->dataTableWidget->setItem(newRow, 3, keyItem);
        ui->dataTableWidget->setItem(newRow, 4, nameItem);
        ui->dataTableWidget->setItem(newRow, 5, accessItem);
        ui->dataTableWidget->setItem(newRow, 6, valueItem);
        m_dataRowMap[key] = newRow;
    }
    m_isInternalChange = false;
}

QByteArray MainWindow::toHex(const QByteArray &bytes)
{
    QByteArray hexBytes;
    const char *pBytes = bytes.data();
    for(int i = 0;i < bytes.size();i++) {
        hexBytes += QByteArray(1,(uchar)pBytes[i]).toHex().toUpper() + " ";
    }
    return hexBytes;
}

void MainWindow::initDeivceTableUI()
{
    // 配置新的设备列表表格
    ui->deviceTableWidget->setColumnCount(3);
    ui->deviceTableWidget->setHorizontalHeaderLabels({"Device Name", "Status", "Action"});
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->deviceTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->deviceTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->deviceTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

//    ui->splitter->setStretchFactor(0, 2); // 左侧比例为1
//    ui->splitter->setStretchFactor(1, 5); // 右侧宽度是左侧的3倍
}

void MainWindow::initModbusTableUI()
{
    // 设置数据表格
    ui->dataTableWidget->setColumnCount(7);
    ui->dataTableWidget->setHorizontalHeaderLabels({"Address", "Bitpos", "Length", "Key", "Name", "Access", "Value"});
    QHeaderView* header = ui->dataTableWidget->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Address
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents); // Bitpos
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Length
    header->setSectionResizeMode(3, QHeaderView::Stretch);          // Key
    header->setSectionResizeMode(4, QHeaderView::Stretch);          // Name
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents); // Access
    header->setSectionResizeMode(6, QHeaderView::Stretch);          // Value
}


void MainWindow::onDeviceDataUpdated(const QString& deviceId, const QString& key, const QVariant& value)
{
    // 仅当数据显示的是当前活动设备时才更新
    QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }
    QString currentDeviceId = selectedItems.first()->data(Qt::UserRole).toString();
    if (currentDeviceId != deviceId) {
        return;
    }

    // 处理ZMotion设备的特殊数据更新
    if (deviceId == "zmotion_001" && ui->stackedWidget->currentIndex() == 2) {
        // 处理轴位置数据
        if (key.contains("_position")) {
            int axisId = key.mid(4, 1).toInt(); // 从"axis0_position"中提取轴号
            if (axisId < ui->axisStatusTable->rowCount()) {
                ui->axisStatusTable->item(axisId, 1)->setText(QString::number(value.toDouble(), 'f', 3));
            }
        }
        // 处理轴状态数据
        else if (key.contains("_status")) {
            int axisId = key.mid(4, 1).toInt();
            if (axisId < ui->axisStatusTable->rowCount()) {
                int status = value.toInt();
                QString statusText = (status == 0) ? "停止" : ((status & 0x01) ? "运动中" : "就绪");
                ui->axisStatusTable->item(axisId, 3)->setText(statusText);
            }
        }
        // 处理输入IO状态
        else if (key.startsWith("input")) {
            int inputId = key.mid(5).toInt(); // 从"input0"中提取IO号
            if (inputId < 8) {
                QLabel* inputLed = findChild<QLabel*>(QString("input%1Led").arg(inputId));
                if (inputLed) {
                    bool state = (value.toString() == "1");
                    if (state) {
                        inputLed->setStyleSheet("QLabel { background-color: #4CAF50; color: white; border: 1px solid #BDBDBD; border-radius: 3px; padding: 2px; }");
                    } else {
                        inputLed->setStyleSheet("QLabel { background-color: #E0E0E0; border: 1px solid #BDBDBD; border-radius: 3px; padding: 2px; }");
                    }
                }
            }
        }
        
        // 在ZMotion日志中显示数据更新
        QString logMsg = QString("[%1] %2 = %3").arg(QTime::currentTime().toString("hh:mm:ss")).arg(key).arg(value.toString());
        ui->zmotionLogEdit->appendPlainText(logMsg);
        
        // 自动滚动到底部
        if (ui->autoScrollCheckBox->isChecked()) {
            ui->zmotionLogEdit->moveCursor(QTextCursor::End);
        }
    }
    // 处理其他设备的数据表格更新
    else if (m_dataRowMap.contains(key)) {
        int dataRow = m_dataRowMap[key];
        m_isInternalChange = true;
        if(ui->dataTableWidget->item(dataRow, 6))
             ui->dataTableWidget->item(dataRow, 6)->setText(value.toString());
        m_isInternalChange = false;
    }
}

void MainWindow::onTableCellChanged(int row, int column)
{
    if (m_isInternalChange || column != 6)
        return;

    QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
    if (selectedItems.isEmpty())
        return;

    QString currentDeviceId = selectedItems.first()->data(Qt::UserRole).toString();

    QTableWidgetItem* keyItem = ui->dataTableWidget->item(row, 3);
    QTableWidgetItem* valueItem = ui->dataTableWidget->item(row, 6);

    if (!keyItem || !valueItem)
        return;

    QString key = keyItem->text();
    QString value = valueItem->text();

    Device* device = m_deviceManager->getDevice(currentDeviceId);
    if (device) {
        QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                  Q_ARG(QString, key), Q_ARG(QString, value));
    }
}

void MainWindow::onDeviceConnectionChanged(const QString& deviceId, bool connected)
{
    for (int row = 0; row < ui->deviceTableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = ui->deviceTableWidget->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == deviceId) {
            QTableWidgetItem* statusItem = ui->deviceTableWidget->item(row, 1);
            if (statusItem) {
                statusItem->setText(connected ? "已连接" : "未连接");
                statusItem->setForeground(connected ? Qt::darkGreen : Qt::red);
            }
            return; // Found and updated
        }
    }
}

void MainWindow::on_sendBtn_clicked()
{
    QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
    if (selectedItems.isEmpty())
        return;

    QString currentDeviceId = selectedItems.first()->data(Qt::UserRole).toString();
    Device* device = m_deviceManager->getDevice(currentDeviceId);
    if (device) {
        QMetaObject::invokeMethod(device, "writeText2Device", Qt::QueuedConnection,
                                  Q_ARG(QString, ui->sendEdit->toPlainText()));
    }
}

void MainWindow::onPrintLog(const QByteArray &bytes, bool isWrite)
{
    if(bytes.isEmpty())
        return;

    Device* senderDevice = qobject_cast<Device*>(sender());
    if (!senderDevice) {
        return; // 无法确定发送者
    }
    QString senderDeviceId = senderDevice->deviceId();

    QPlainTextEdit* logEdit = nullptr;
    bool hexDisplay = false;
    bool autoScroll = false;

    // 根据设备ID选择UI控件和配置
    if (senderDeviceId == "zmotion_001") {
        logEdit = ui->zmotionLogEdit;
        hexDisplay = ui->zmotionHexDisplayCheckBox->isChecked();
        autoScroll = ui->autoScrollCheckBox->isChecked();
    } else if (senderDeviceId == "jgt_001") {
        logEdit = ui->logEdit;
        hexDisplay = ui->jgtHexDisplayCheckBox->isChecked();
        autoScroll = ui->jgtAutoScrollCheckBox->isChecked();
    } else {
        return; // 没有对应的日志界面
    }

    if (!logEdit) return;

    // 准备日志内容
    QString timeStr = QTime::currentTime().toString("hh:mm:ss.zzz");
    QString direction = isWrite ? "Send" : "Recv";
    
    // 始终显示文本内容
    QString textContent = QString::fromUtf8(bytes);
    QString logMessage = QString("[%1] %2(text): %3").arg(timeStr).arg(direction).arg(textContent);
    logEdit->appendPlainText(logMessage);

    // 如果勾选了Hex，则额外显示Hex内容
    if (hexDisplay) {
        QString hexContent = toHex(bytes);
        QString hexLogMessage = QString("[%1] %2(hex): %3").arg(timeStr).arg(direction).arg(hexContent);
        logEdit->appendPlainText(hexLogMessage);
    }

    // 追加一个空行以分隔日志条目
    logEdit->appendPlainText("");

    // 滚动
    if (autoScroll) {
        logEdit->moveCursor(QTextCursor::End);
    }
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    m_deviceManager->cleanup();
    event->accept();
}

void MainWindow::onReconnectButtonClicked(const QString &deviceId)
{
    Device* device = m_deviceManager->getDevice(deviceId);
    if (device) {
        // 先在UI上立刻更新状态
        for (int row = 0; row < ui->deviceTableWidget->rowCount(); ++row) {
            QTableWidgetItem* item = ui->deviceTableWidget->item(row, 0);
            if (item && item->data(Qt::UserRole).toString() == deviceId) {
                QTableWidgetItem* statusItem = ui->deviceTableWidget->item(row, 1);
                if (statusItem) {
                    statusItem->setText("连接中...");
                    statusItem->setForeground(Qt::black);
                }
                break; // Found and updated
            }
        }
        // 使用invokeMethod安全地调用工作线程中的连接方法
        QMetaObject::invokeMethod(device, "connectDevice", Qt::QueuedConnection);
    }
}


void MainWindow::initZMotionUI()
{
    // 初始化ZMotion页面UI
    ui->axisStatusTable->setRowCount(6);
    ui->axisStatusTable->setHorizontalHeaderLabels({"轴号", "位置", "速度", "状态"});
    ui->axisStatusTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->axisStatusTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->axisStatusTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->axisStatusTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    for (int i = 0; i < 6; i++) {
        ui->axisStatusTable->setItem(i, 0, new QTableWidgetItem(QString("轴%1").arg(i)));
        ui->axisStatusTable->setItem(i, 1, new QTableWidgetItem("0.000"));
        ui->axisStatusTable->setItem(i, 2, new QTableWidgetItem("0.000"));
        ui->axisStatusTable->setItem(i, 3, new QTableWidgetItem("未知"));

        for (int j = 0; j < 4; j++) {
            if(ui->axisStatusTable->item(i,j)) {
                ui->axisStatusTable->item(i, j)->setFlags(ui->axisStatusTable->item(i, j)->flags() & ~Qt::ItemIsEditable);
            }
        }
    }
    ui->axisStatusTable->resizeColumnsToContents();


    // 轴控制按钮连接
    connect(ui->moveAbsBtn, &QPushButton::clicked, this, [this]() {
        int axisId = ui->axisComboBox->currentIndex();
        double position = ui->absPosSpinBox->value();
        double speed = ui->speedSpinBox->value();
        
        QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
            Device* device = m_deviceManager->getDevice(deviceId);
            if (device) {
                QString command = QString("axis%1_abs_move").arg(axisId);
                QString value = QString("%1,%2").arg(position).arg(speed);
                QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                          Q_ARG(QString, command), Q_ARG(QString, value));
            }
        }
    });
    
    connect(ui->moveRelBtn, &QPushButton::clicked, this, [this]() {
        int axisId = ui->axisComboBox->currentIndex();
        double distance = ui->relPosSpinBox->value();
        double speed = ui->speedSpinBox->value();
        
        QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
            Device* device = m_deviceManager->getDevice(deviceId);
            if (device) {
                QString command = QString("axis%1_rel_move").arg(axisId);
                QString value = QString("%1,%2").arg(distance).arg(speed);
                QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                          Q_ARG(QString, command), Q_ARG(QString, value));
            }
        }
    });
    
    connect(ui->stopAxisBtn, &QPushButton::clicked, this, [this]() {
        int axisId = ui->axisComboBox->currentIndex();
        
        QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
            Device* device = m_deviceManager->getDevice(deviceId);
            if (device) {
                QString command = QString("axis%1_stop").arg(axisId);
                QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                          Q_ARG(QString, command), Q_ARG(QString, "1"));
            }
        }
    });
    
    connect(ui->homeAxisBtn, &QPushButton::clicked, this, [this]() {
        int axisId = ui->axisComboBox->currentIndex();
        
        QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
            Device* device = m_deviceManager->getDevice(deviceId);
            if (device) {
                QString command = QString("axis%1_home").arg(axisId);
                QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                          Q_ARG(QString, command), Q_ARG(QString, "1"));
            }
        }
    });
    
    connect(ui->stopAllBtn, &QPushButton::clicked, this, [this]() {
        QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
        if (!selectedItems.isEmpty()) {
            QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
            Device* device = m_deviceManager->getDevice(deviceId);
            if (device) {
                QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                          Q_ARG(QString, "stop_all"), Q_ARG(QString, "1"));
            }
        }
    });
    
    // 输出控制按钮连接
    QList<QPushButton*> outputButtons = {
        ui->output0Btn, ui->output1Btn, ui->output2Btn, ui->output3Btn,
        ui->output4Btn, ui->output5Btn, ui->output6Btn, ui->output7Btn
    };
    
    for (int i = 0; i < outputButtons.size(); i++) {
        connect(outputButtons[i], &QPushButton::toggled, this, [this, i](bool checked) {
            QList<QTableWidgetItem*> selectedItems = ui->deviceTableWidget->selectedItems();
            if (!selectedItems.isEmpty()) {
                QString deviceId = selectedItems.first()->data(Qt::UserRole).toString();
                Device* device = m_deviceManager->getDevice(deviceId);
                if (device) {
                    QString command = QString("output%1").arg(i);
                    QString value = checked ? "1" : "0";
                    QMetaObject::invokeMethod(device, "writeData2Device", Qt::QueuedConnection,
                                              Q_ARG(QString, command), Q_ARG(QString, value));
                }
            }
        });
    }
    
    // 日志工具栏按钮连接
    connect(ui->clearLogBtn, &QPushButton::clicked, this, [this]() {
        ui->zmotionLogEdit->clear();
    });
}


void MainWindow::updateZMotionAxisStatus()
{
    // 这个方法会在接收到设备数据更新时被调用
    // 具体实现可以根据实际需要进行扩展
}

void MainWindow::updateZMotionIOStatus()
{
    // 这个方法会在接收到IO状态更新时被调用
    // 具体实现可以根据实际需要进行扩展
}


void MainWindow::on_jgtClearLogBtn_clicked()
{
    ui->logEdit->clear();
}

void MainWindow::on_jgtAutoScrollCheckBox_toggled(bool checked)
{
    Q_UNUSED(checked);
    // 实际的滚动逻辑在 onPrintLog 中处理
}

void MainWindow::on_jgtHexDisplayCheckBox_toggled(bool checked)
{
    Q_UNUSED(checked);
    // 实际的显示逻辑在 onPrintLog 中处理
}

void MainWindow::on_zmotionHexDisplayCheckBox_toggled(bool checked)
{
    Q_UNUSED(checked);
    // 实际的显示逻辑在 onPrintLog 中处理
}
