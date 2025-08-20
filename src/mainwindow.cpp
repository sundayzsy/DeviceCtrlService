/**
 * @file mainwindow.cpp
 * @brief MainWindow类的实现
 */

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_deviceManager(new DeviceManager(this))
    , m_threadManager(new ThreadManager(this))
    , m_dataManager(new DataManager(this))
    , m_isInternalChange(false)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(1, 3); // 右侧宽度是左侧的3倍

    // 设置数据表格
    ui->dataTableWidget->setColumnCount(5);
    ui->dataTableWidget->setHorizontalHeaderLabels({"Address", "Key", "Name", "Access", "Value"});
    ui->dataTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->dataTableWidget, &QTableWidget::cellChanged, this, &MainWindow::onTableCellChanged);

    loadDevices();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadDevices()
{
    QFile file(":/config/lsj_device.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open devices config file.");
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc(QJsonDocument::fromJson(data));
    QJsonObject config = doc.object();

    if (m_deviceManager->addDevice(config)) {
        Device* device = m_deviceManager->getDevice(config["device_id"].toString());
        if (device) {
            m_threadManager->startDeviceThread(device);
            ui->deviceListWidget->addItem(device->deviceId());
            connect(device, &Device::dataUpdated, m_dataManager, &DataManager::updateDeviceData);
        }
    }

    // 注意：这里有一个设计问题，UI需要知道key对应的name，但这个信息在Device内部。
    // 为了快速实现，我们可以在MainWindow中也加载一次配置来获取name。
    // 更好的方法是让Device发出一个包含所有信息的信号。
    QJsonArray registers = config["registers"].toArray();
    for (const QJsonValue &val : registers) {
        QJsonObject obj = val.toObject();
        int address = obj["address"].toInt();
        QString key = obj["key"].toString();
        QString name = obj["name"].toString();
        if (!m_dataRowMap.contains(key)) {
            int newRow = ui->dataTableWidget->rowCount();
            ui->dataTableWidget->insertRow(newRow);

            auto addressItem = new QTableWidgetItem(QString::number(address));
            auto keyItem = new QTableWidgetItem(key);
            auto nameItem = new QTableWidgetItem(name);
            auto valueItem = new QTableWidgetItem("0");
            QString access = obj["access"].toString();
            auto accessItem = new QTableWidgetItem(access);

            // 根据access权限设置是否可编辑
            if (!access.contains("write")) {
                valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
            }
            // access列本身总是不可编辑的
            accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);

            ui->dataTableWidget->setItem(newRow, 0, addressItem);
            ui->dataTableWidget->setItem(newRow, 1, keyItem);
            ui->dataTableWidget->setItem(newRow, 2, nameItem);
            ui->dataTableWidget->setItem(newRow, 3, accessItem);
            ui->dataTableWidget->setItem(newRow, 4, valueItem);
            m_dataRowMap[key] = newRow;
        }
    }


    connect(m_dataManager, &DataManager::dataUpdated, this, &MainWindow::onDeviceDataUpdated);
}

void MainWindow::onDeviceDataUpdated(const QString& deviceId, const QString& key, const QVariant& value)
{
    Q_UNUSED(deviceId);
    if (m_dataRowMap.contains(key)) {
        int row = m_dataRowMap[key];
        m_isInternalChange = true;
        if(ui->dataTableWidget->item(row, 4))
             ui->dataTableWidget->item(row, 4)->setText(value.toString());
        m_isInternalChange = false;
    } else {
        // 理论上不应该发生，因为我们在loadDevices时已经填充了所有key
        qWarning() << "Received data for unknown key:" << key;
    }
}

void MainWindow::onTableCellChanged(int row, int column)
{
    if (m_isInternalChange || column != 4)
        return;

    QTableWidgetItem* keyItem = ui->dataTableWidget->item(row, 1);
    QTableWidgetItem* valueItem = ui->dataTableWidget->item(row, 4);

    if (!keyItem || !valueItem)
        return;

    QString key = keyItem->text();
    QString value = valueItem->text();

    Device* device = m_deviceManager->getDevice("lsj_001"); // 假设只有一个设备
    if (device) {
//        qDebug() << "UI sending write request for" << key << "with value" << value;
        device->writeData2Device(key, value);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_threadManager->cleanup();
    event->accept();
}
