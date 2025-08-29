#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QCloseEvent>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DeviceManager;
class ThreadManager;
class DataManager;

/**
 * @brief 主窗口类，应用程序的主窗口
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 构造一个主窗口对象
     * @param parent 父窗口部件
     */
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /**
     * @brief 从设备接收到数据时调用此槽
     * @param data 接收到的数据
     */
    void onDeviceDataUpdated(const QString& deviceId, const QString& key, const QVariant& value);
    void onTableCellChanged(int row, int column);
    void onDeviceSelectionChanged();
    void onDeviceConnectionChanged(const QString& deviceId, bool connected);
    void on_sendBtn_clicked();
    void onPrintLog(const QByteArray &bytes, bool isWrite);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    /**
     * @brief 从配置文件加载设备
     */
    void loadDevice(const QString& filePath);
    void updateDataTable(const QString& deviceId);
    QByteArray toHex(const QByteArray &bytes);

private:
    Ui::MainWindow *ui;                 ///< 主窗口的UI
    DeviceManager* m_deviceManager;     ///< 设备管理器
    ThreadManager* m_threadManager;     ///< 线程管理器
    DataManager* m_dataManager;         ///< 数据管理器
    QMap<QString, int> m_dataRowMap;    ///< 用于跟踪数据显示在哪一行
    QMap<QString, int> m_deviceRowMap;  ///< 用于根据设备ID查找其在设备列表中的行
    bool m_isInternalChange;            ///< 用于防止cellChanged信号重入
};
#endif // MAINWINDOW_H
