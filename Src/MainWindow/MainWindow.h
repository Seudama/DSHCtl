#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStart();
    void onStop();
    void onRestart();
    void onOpenBrowser();
    void onScanLegacy();
    void onRefreshLogs();
    void onClearLogs();
    void refreshStatus();
    void healthTick();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void logLine(const QString& text);
    void loadLogTail();
    void startHealthWait(qint64 pid);
    bool logViewAtBottom() const;
    QIcon makeAppIcon() const;

    QLabel* m_statusLabel = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QLineEdit* m_cmdEdit = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QTimer* m_statusTimer = nullptr;
    QTimer* m_healthTimer = nullptr;
    int m_healthWaited = 0;
    qint64 m_healthPid = 0;
    bool m_trayTipShown = false;
};

#endif // MAINWINDOW_H
