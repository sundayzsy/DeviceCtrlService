#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModbusDataUnit>
#include <QJsonObject>
#include <QJsonArray>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QModbusRtuSerialSlave;

struct RegisterDefinition {
    int address;
    QString key;
    QString name;
    int length;
    int bitpos;
    QString access;
    QModbusDataUnit::RegisterType type;
};

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
    void loadConfig(const QString& path);
    void setupRegistersFromConfig();
    void initSerialPorts();
    const RegisterDefinition* findRegister(int address, QModbusDataUnit::RegisterType table) const;


    Ui::MainWindow *ui;
    QModbusRtuSerialSlave *m_modbusSlave;
    QList<RegisterDefinition> m_registers;
};
#endif // MAINWINDOW_H