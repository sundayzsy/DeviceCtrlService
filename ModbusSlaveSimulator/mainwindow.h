#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModbusDataUnit>
#include "src/core/modbusdata.h" // 包含共享的数据结构
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QModbusRtuSerialSlave;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_connectButton_clicked();
    void onDataWritten(QModbusDataUnit::RegisterType table, int address, int size);
    void onTableCellChanged(int row, int column);

private:
    void initDataMap(const QString& path);
    void setupUIFromDataMap();
    void initSerialPorts();
    void setupModbusMap();
    void updateSlaveData(quint16 address);
    void updateUI(const QString& key, const QVariant& value);


    Ui::MainWindow *ui;
    QModbusRtuSerialSlave *m_modbusSlave;
    QMap<quint16, ModbusSturct> m_dataMap;
    QMap<QString, QPair<quint16, int>> m_keyIndexMap;
    QMap<QString, int> m_uiRowMap; // key -> row index in UI table
    QList<quint16> m_addressOrder; // 存储地址的原始顺序
};
#endif // MAINWINDOW_H