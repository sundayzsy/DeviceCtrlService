#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMetaObject>
#include "core/DeviceManager.h"
#include "core/ThreadManager.h"
#include "core/DataManager.h"
#include "core/Device.h"
#include "devices/ZMotionDevice.h"
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

            // 如果是ZMotion设备，则在设备加载后设置信号槽连接
            if (deviceId == "zmotion_001") {
                setupZmotionDeviceConnections();
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
        int keyAxis = -1;

        // 解析 key 中的轴号
        if (key.startsWith("axis")) {
            keyAxis = key.mid(4, 1).toInt();
        }

        // --- 以下是全局状态更新，不受选中轴影响 ---

        // 1. 更新轴状态总览表 (axisStatusTable)
        if (key.contains("_position")) {
            if (keyAxis >= 0 && keyAxis < ui->axisStatusTable->rowCount()) {
                ui->axisStatusTable->item(keyAxis, 1)->setText(QString::number(value.toDouble(), 'f', 3));
            }
        } else if (key.contains("_status_code")) { // 假设状态码以 _status_code 结尾
            if (keyAxis >= 0 && keyAxis < ui->axisStatusTable->rowCount()) {
                int status = value.toInt();
                // 这里可以添加更复杂的状态解析逻辑
                QString statusText = (status == 0) ? "空闲" : "运动中";
                ui->axisStatusTable->item(keyAxis, 3)->setText(statusText);
            }
        }

        // 2. 更新IO输入指示灯
        if (key.startsWith("input")) {
            int inputId = key.mid(5).toInt();
            if (inputId >= 0 && inputId < 8) {
                QLabel* inputLed = findChild<QLabel*>(QString("input%1Led").arg(inputId));
                if (inputLed) {
                    bool state = value.toBool();
                    inputLed->setStyleSheet(state ? "background-color: #4CAF50; color: white;" : "background-color: #E0E0E0;");
                }
            }
        }
 
        // 3. 更新IO输出按钮状态 (反馈)
        if (key.startsWith("output")) {
            int outputId = key.mid(6).toInt();
            if (outputId >= 0 && outputId < 8) { // 假设有8个输出按钮
                QPushButton* outputBtn = findChild<QPushButton*>(QString("output%1Btn").arg(outputId));
                if (outputBtn) {
                    bool state = value.toBool();
                    // 避免触发 toggled 信号的无限循环
                    if (outputBtn->isChecked() != state) {
                        outputBtn->setChecked(state);
                    }
                }
            }
        }

        // 4. 在日志区域显示所有原始数据更新
        QString logMsg = QString("[%1] %2 = %3").arg(QTime::currentTime().toString("hh:mm:ss.zzz")).arg(key).arg(value.toString());
        ui->zmotionLogEdit->appendPlainText(logMsg);
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
    // --- 1. 动态加载和初始化UI控件 ---

    // 1.1 从JSON文件加载轴信息到zmotionAxis_comboBox
    ui->zmotionAxis_comboBox->clear();
    QString configPath = QCoreApplication::applicationDirPath() + "/config/zmotion_device.json";
    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QByteArray data = configFile.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject rootObj = doc.object();
            if (rootObj.contains("axes") && rootObj["axes"].isArray()) {
                QJsonArray axesArray = rootObj["axes"].toArray();
                for (const QJsonValue& value : axesArray) {
                    QJsonObject axisObj = value.toObject();
                    if (axisObj.contains("id") && axisObj.contains("name")) {
                        // 只添加启用的轴
                        if (axisObj.value("enabled").toBool(true)) {
                           QString name = axisObj["name"].toString();
                           int id = axisObj["id"].toInt();
                           ui->zmotionAxis_comboBox->addItem(name, id);
                        }
                    }
                }
            }
        }
        configFile.close();
    } else {
        qWarning() << "Could not open zmotion_device.json for reading, using fallback.";
        // 如果配置文件读取失败，提供备用选项
        for (int i = 0; i < 6; ++i) {
            ui->zmotionAxis_comboBox->addItem(QString("轴 %1 (Fallback)").arg(i), i);
        }
    }

    // 1.2 初始化回零模式下拉框 homingMode_comboBox
    ui->homingMode_comboBox->clear();
    ui->homingMode_comboBox->addItem("Z相正向回零", 1);
    ui->homingMode_comboBox->addItem("Z相负向回零", 2);
    ui->homingMode_comboBox->addItem("原点正向回零+反找", 3);
    ui->homingMode_comboBox->addItem("原点负向回零+反找", 4);
    ui->homingMode_comboBox->addItem("原点正向回零", 13);
    ui->homingMode_comboBox->addItem("原点负向回零", 14);

    // 1.3 为QLineEdit设置默认文本
    ui->units_lineEdit->setText("1000");
    ui->lspeed_lineEdit->setText("0");
    ui->speed_lineEdit->setText("10");
    ui->accel_lineEdit->setText("100");
    ui->decel_lineEdit->setText("100");
    ui->sramp_lineEdit->setText("20");
    ui->distance_lineEdit->setText("10");
    ui->homingCreepSpeed_lineEdit->setText("10");

    // 1.4 初始化轴状态总览表 (保持不变)
    ui->axisStatusTable->setRowCount(6);
    ui->axisStatusTable->setHorizontalHeaderLabels({"轴号", "位置(pulse)", "速度(pulse/s)", "状态"});
    ui->axisStatusTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < 6; i++) {
        ui->axisStatusTable->setItem(i, 0, new QTableWidgetItem(QString("轴%1").arg(i)));
        ui->axisStatusTable->setItem(i, 1, new QTableWidgetItem("0.0"));
        ui->axisStatusTable->setItem(i, 2, new QTableWidgetItem("0.0"));
        ui->axisStatusTable->setItem(i, 3, new QTableWidgetItem("未知"));
        for (int j = 0; j < 4; ++j) {
            if(ui->axisStatusTable->item(i, j))
                ui->axisStatusTable->item(i, j)->setFlags(ui->axisStatusTable->item(i, j)->flags() & ~Qt::ItemIsEditable);
        }
    }
//    ui->axisStatusTable->resizeColumnsToContents();

    // --- 日志工具栏 (保持不变) ---
    connect(ui->clearLogBtn, &QPushButton::clicked, ui->zmotionLogEdit, &QPlainTextEdit::clear);
}


void MainWindow::setupZmotionDeviceConnections()
{
    ZMotionDevice* zmotionDevice = static_cast<ZMotionDevice*>(m_deviceManager->getDevice("zmotion_001"));
    if (!zmotionDevice) {
        qWarning() << "ZMotion device not found at setup time, UI connections skipped.";
        return;
    }

    // --- 运动控制 ---
    // JOG+
    connect(ui->jogPositive_pushButton, &QPushButton::pressed, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "moveContinuous", Qt::QueuedConnection, Q_ARG(int, axisId), Q_ARG(int, 1));
    });
    connect(ui->jogPositive_pushButton, &QPushButton::released, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "stopAxis", Qt::QueuedConnection, Q_ARG(int, axisId));
    });

    // JOG-
    connect(ui->jogNegative_pushButton, &QPushButton::pressed, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "moveContinuous", Qt::QueuedConnection, Q_ARG(int, axisId), Q_ARG(int, -1));
    });
    connect(ui->jogNegative_pushButton, &QPushButton::released, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "stopAxis", Qt::QueuedConnection, Q_ARG(int, axisId));
    });

    // 相对移动
    connect(ui->moveRelative_pushButton, &QPushButton::clicked, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        double distance = ui->distance_lineEdit->text().toDouble();
        QMetaObject::invokeMethod(zmotionDevice, "moveRelative", Qt::QueuedConnection, Q_ARG(int, axisId), Q_ARG(double, distance));
    });

    // 停止
    connect(ui->stop_pushButton, &QPushButton::clicked, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "stopAxis", Qt::QueuedConnection, Q_ARG(int, axisId));
    });

    // 位置清零
    connect(ui->zeroPosition_pushButton, &QPushButton::clicked, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        QMetaObject::invokeMethod(zmotionDevice, "zeroPosition", Qt::QueuedConnection, Q_ARG(int, axisId));
    });

    // --- 回零控制 ---
    connect(ui->startHoming_pushButton, &QPushButton::clicked, this, [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        int mode = ui->homingMode_comboBox->currentData().toInt();
        int homingIoPort = ui->homingIo_spinBox->value();
        // 常开 (index 0) 对应 invertIo = false, 常闭 (index 1) 对应 invertIo = true
        bool invertIo = (ui->homingIoLogic_comboBox->currentIndex() == 1);
        double creepSpeed = ui->homingCreepSpeed_lineEdit->text().toDouble();

        QMetaObject::invokeMethod(zmotionDevice, "startHoming", Qt::QueuedConnection,
                                  Q_ARG(int, axisId),
                                  Q_ARG(int, mode),
                                  Q_ARG(int, homingIoPort),
                                  Q_ARG(bool, invertIo),
                                  Q_ARG(double, creepSpeed));
    });

    // --- 参数自动应用 ---
    auto applyParameters = [zmotionDevice, this]() {
        int axisId = ui->zmotionAxis_comboBox->currentData().toInt();
        double units = ui->units_lineEdit->text().toDouble();
        double speed = ui->speed_lineEdit->text().toDouble();
        double accel = ui->accel_lineEdit->text().toDouble();
        double decel = ui->decel_lineEdit->text().toDouble();
        double sramp = ui->sramp_lineEdit->text().toDouble();

        QMetaObject::invokeMethod(zmotionDevice, "setAxisParameters", Qt::QueuedConnection,
                                  Q_ARG(int, axisId),
                                  Q_ARG(double, units),
                                  Q_ARG(double, speed),
                                  Q_ARG(double, accel),
                                  Q_ARG(double, decel),
                                  Q_ARG(double, sramp));
    };

    connect(ui->jogPositive_pushButton, &QPushButton::pressed, this, applyParameters);
    connect(ui->jogNegative_pushButton, &QPushButton::pressed, this, applyParameters);
    connect(ui->moveRelative_pushButton, &QPushButton::clicked, this, applyParameters);
    connect(ui->startHoming_pushButton, &QPushButton::clicked, this, applyParameters);

    // --- IO控制 ---
    QList<QPushButton*> outputButtons = {
        ui->output0Btn, ui->output1Btn, ui->output2Btn, ui->output3Btn,
        ui->output4Btn, ui->output5Btn, ui->output6Btn, ui->output7Btn
    };
    for (int i = 0; i < outputButtons.size(); i++) {
        connect(outputButtons[i], &QPushButton::toggled, this, [zmotionDevice, i](bool checked) {
            QMetaObject::invokeMethod(zmotionDevice, "setDigitalOutput", Qt::QueuedConnection, Q_ARG(int, i), Q_ARG(bool, checked));
        });
    }
}


void MainWindow::on_jgtClearLogBtn_clicked()
{
    ui->logEdit->clear();
}
