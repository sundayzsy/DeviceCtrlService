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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceManager(new DeviceManager(this))
    , m_threadManager(new ThreadManager(this))
    , m_dataManager(new DataManager(this))
    , m_isInternalChange(false)
{
    ui->setupUi(this);

    setWindowTitle("设备通信控制服务系统");
    // 配置新的设备列表表格
    ui->deviceTableWidget->setColumnCount(3);
    ui->deviceTableWidget->setHorizontalHeaderLabels({"Device Name", "Status", "Action"});
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->deviceTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->deviceTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->deviceTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->deviceTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->splitter->setStretchFactor(0, 2); // 左侧比例为1
    ui->splitter->setStretchFactor(1, 5); // 右侧宽度是左侧的3倍

    ui->stackedWidget->setCurrentIndex(0);

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

    // 连接信号和槽
    connect(ui->dataTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
    connect(ui->deviceTableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onDeviceSelectionChanged);
    connect(m_dataManager, &DataManager::dataUpdated, this, &MainWindow::onDeviceDataUpdated);

    // 自动加载默认设备
    QString configPath = QCoreApplication::applicationDirPath() + "/config/";
    loadDevice(QDir::toNativeSeparators(configPath + "lsj_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "jgq_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "jgt_device.json"));
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

    if (m_dataRowMap.contains(key)) {
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
                statusItem->setForeground(connected ? (connected ? Qt::darkGreen : Qt::red) : Qt::black);
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

    if(isWrite)
    {
        ui->logEdit->appendPlainText(QTime::currentTime().toString(tr("hh:mm:ss")));
        ui->logEdit->appendPlainText(QString(tr("Send(text): ")) + QString::fromUtf8(bytes));
        ui->logEdit->appendPlainText(QString(tr("Send(hex ): ")) + toHex(bytes));
        ui->logEdit->appendPlainText(QString(" "));
    }
    else
    {
        ui->logEdit->appendPlainText(QTime::currentTime().toString(tr("hh:mm:ss")));
        ui->logEdit->appendPlainText(QString(tr("Recv(text): ")) + QString::fromUtf8(bytes));
        ui->logEdit->appendPlainText(QString(tr("Recv(hex ): ")) + toHex(bytes));
        ui->logEdit->appendPlainText(QString(" "));
    }
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    m_threadManager->cleanup();
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
