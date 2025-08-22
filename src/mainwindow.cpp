#include "mainwindow.h"
#include "ui_mainwindow.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceManager(new DeviceManager(this))
    , m_threadManager(new ThreadManager(this))
    , m_dataManager(new DataManager(this))
    , m_deviceTableWidget(new QTableWidget(this))
    , m_isInternalChange(false)
{
    ui->setupUi(this);

    // 创建并配置新的设备列表表格
    m_deviceTableWidget->setColumnCount(2);
    m_deviceTableWidget->setHorizontalHeaderLabels({"Device ID", "Status"});
    m_deviceTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_deviceTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 替换旧的 QListWidget
    QWidget* oldListWidget = ui->splitter->widget(0);
    oldListWidget->setParent(nullptr);
    delete oldListWidget;
    ui->splitter->insertWidget(0, m_deviceTableWidget);
    ui->deviceListWidget = nullptr; // 避免悬空指针

    ui->splitter->setStretchFactor(0, 1); // 左侧比例为1
    ui->splitter->setStretchFactor(1, 3); // 右侧宽度是左侧的3倍

    // 设置数据表格
    ui->dataTableWidget->setColumnCount(5);
    ui->dataTableWidget->setHorizontalHeaderLabels({"Address", "Key", "Name", "Access", "Value"});
    ui->dataTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // 连接信号和槽
    connect(ui->dataTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);
    connect(m_deviceTableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onDeviceSelectionChanged);
    connect(m_dataManager, &DataManager::dataUpdated, this, &MainWindow::onDeviceDataUpdated);

    // 自动加载默认设备
    QString configPath = QCoreApplication::applicationDirPath() + "/config/";
    loadDevice(QDir::toNativeSeparators(configPath + "lsj_device.json"));
    loadDevice(QDir::toNativeSeparators(configPath + "jgq_device.json"));
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
            
            int newRow = m_deviceTableWidget->rowCount();
            m_deviceTableWidget->insertRow(newRow);
            m_deviceRowMap[deviceId] = newRow;

            auto idItem = new QTableWidgetItem(deviceId);
            auto statusItem = new QTableWidgetItem("Connecting...");
            m_deviceTableWidget->setItem(newRow, 0, idItem);
            m_deviceTableWidget->setItem(newRow, 1, statusItem);

            connect(device, &Device::dataUpdated, m_dataManager, &DataManager::updateDeviceData);
            connect(device, &Device::connectedChanged, this, &MainWindow::onDeviceConnectionChanged);

            // 如果这是第一个设备，则立即显示其数据
            if (m_deviceTableWidget->rowCount() == 1) {
                m_deviceTableWidget->selectRow(0);
            }
        }
    }
}

void MainWindow::onDeviceSelectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = m_deviceTableWidget->selectedItems();
    if (!selectedItems.isEmpty()) {
        QString deviceId = m_deviceTableWidget->item(selectedItems.first()->row(), 0)->text();
        updateDataTable(deviceId);
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

        int newRow = ui->dataTableWidget->rowCount();
        ui->dataTableWidget->insertRow(newRow);

        auto addressItem = new QTableWidgetItem(QString::number(address));
        auto keyItem = new QTableWidgetItem(key);
        auto nameItem = new QTableWidgetItem(name);
        auto accessItem = new QTableWidgetItem(access);
        auto valueItem = new QTableWidgetItem("0"); // 初始值

        // 设置只读属性
        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);
        if (!access.contains("write")) {
            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        }

        ui->dataTableWidget->setItem(newRow, 0, addressItem);
        ui->dataTableWidget->setItem(newRow, 1, keyItem);
        ui->dataTableWidget->setItem(newRow, 2, nameItem);
        ui->dataTableWidget->setItem(newRow, 3, accessItem);
        ui->dataTableWidget->setItem(newRow, 4, valueItem);
        m_dataRowMap[key] = newRow;
    }
    m_isInternalChange = false;
}


void MainWindow::onDeviceDataUpdated(const QString& deviceId, const QString& key, const QVariant& value)
{
    // 仅当数据显示的是当前活动设备时才更新
    QList<QTableWidgetItem*> selectedItems = m_deviceTableWidget->selectedItems();
    if (selectedItems.isEmpty() || m_deviceTableWidget->item(selectedItems.first()->row(), 0)->text() != deviceId) {
        return;
    }

    if (m_dataRowMap.contains(key)) {
        int row = m_dataRowMap[key];
        m_isInternalChange = true;
        if(ui->dataTableWidget->item(row, 4))
             ui->dataTableWidget->item(row, 4)->setText(value.toString());
        m_isInternalChange = false;
    }
}

void MainWindow::onTableCellChanged(int row, int column)
{
    if (m_isInternalChange || column != 4)
        return;

    QList<QTableWidgetItem*> selectedItems = m_deviceTableWidget->selectedItems();
    if (selectedItems.isEmpty())
        return;

    QString currentDeviceId = m_deviceTableWidget->item(selectedItems.first()->row(), 0)->text();

    QTableWidgetItem* keyItem = ui->dataTableWidget->item(row, 1);
    QTableWidgetItem* valueItem = ui->dataTableWidget->item(row, 4);

    if (!keyItem || !valueItem)
        return;

    QString key = keyItem->text();
    QString value = valueItem->text();

    Device* device = m_deviceManager->getDevice(currentDeviceId);
    if (device) {
        device->writeData2Device(key, value);
    }
}

void MainWindow::onDeviceConnectionChanged(const QString& deviceId, bool connected)
{
    if (m_deviceRowMap.contains(deviceId)) {
        int row = m_deviceRowMap[deviceId];
        QTableWidgetItem* statusItem = m_deviceTableWidget->item(row, 1);
        if (statusItem) {
            statusItem->setText(connected ? "Connected" : "Disconnected");
            statusItem->setForeground(connected ? Qt::green : Qt::red);
        }
    }
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    m_threadManager->cleanup();
    event->accept();
}
