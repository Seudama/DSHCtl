#include "MainWindow.h"
#include "DshCtl.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("DSH 后台控制器"));
    setWindowIcon(makeAppIcon());
    resize(760, 560);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // 状态
    m_statusLabel = new QLabel(QStringLiteral("正在检测 ..."), central);
    m_statusLabel->setWordWrap(true);
    QFont sf = m_statusLabel->font();
    sf.setPointSize(11);
    sf.setBold(true);
    m_statusLabel->setFont(sf);
    root->addWidget(m_statusLabel);

    // 按钮行
    auto* btnRow = new QHBoxLayout();
    auto* btnStart = new QPushButton(QStringLiteral("启动"), central);
    auto* btnStop = new QPushButton(QStringLiteral("停止"), central);
    auto* btnRestart = new QPushButton(QStringLiteral("重启"), central);
    auto* btnOpen = new QPushButton(QStringLiteral("打开网页"), central);
    auto* btnScan = new QPushButton(QStringLiteral("清理旧实例"), central);
    btnScan->setToolTip(QStringLiteral(
        "强制结束未被本程序管理的 DSH 进程(例如旧 PowerShell 窗口启动的)"));
    btnRow->addWidget(btnStart);
    btnRow->addWidget(btnStop);
    btnRow->addWidget(btnRestart);
    btnRow->addWidget(btnOpen);
    btnRow->addWidget(btnScan);
    btnRow->addStretch(1);
    root->addLayout(btnRow);
    connect(btnStart, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(btnRestart, &QPushButton::clicked, this, &MainWindow::onRestart);
    connect(btnOpen, &QPushButton::clicked, this, &MainWindow::onOpenBrowser);
    connect(btnScan, &QPushButton::clicked, this, &MainWindow::onScanLegacy);

    // 设置行
    auto* cfgRow = new QHBoxLayout();
    cfgRow->addWidget(new QLabel(QStringLiteral("端口:"), central));
    m_portSpin = new QSpinBox(central);
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(dsh::kDefaultPort);
    m_portSpin->setFixedWidth(84);
    cfgRow->addWidget(m_portSpin);
    cfgRow->addWidget(new QLabel(QStringLiteral("启动命令:"), central));
    m_cmdEdit = new QLineEdit(dsh::kDefaultCmd, central);
    m_cmdEdit->setToolTip(QStringLiteral("默认: npx @deepseek-ai/dsh web"));
    cfgRow->addWidget(m_cmdEdit, 1);
    root->addLayout(cfgRow);

    QSettings settings(QStringLiteral("DSHCtl"), QStringLiteral("DSHCtl"));
    m_portSpin->setValue(settings.value(QStringLiteral("port"), dsh::kDefaultPort).toInt());
    m_cmdEdit->setText(settings.value(QStringLiteral("cmd"), dsh::kDefaultCmd).toString());

    // 日志
    m_logView = new QPlainTextEdit(central);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setFont(QFont(QStringLiteral("Consolas"), 9));
    root->addWidget(m_logView, 1);

    auto* logRow = new QHBoxLayout();
    auto* btnRefreshLogs = new QPushButton(QStringLiteral("刷新日志"), central);
    auto* btnClearLogs = new QPushButton(QStringLiteral("清空日志"), central);
    logRow->addWidget(btnRefreshLogs);
    logRow->addWidget(btnClearLogs);
    logRow->addWidget(new QLabel(
        QStringLiteral("服务日志写入 exe 目录下的 dsh.log / dsh.err.log"), central));
    logRow->addStretch(1);
    root->addLayout(logRow);
    connect(btnRefreshLogs, &QPushButton::clicked, this, &MainWindow::onRefreshLogs);
    connect(btnClearLogs, &QPushButton::clicked, this, &MainWindow::onClearLogs);

    setCentralWidget(central);

    // 系统托盘
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(makeAppIcon(), this);
        m_tray->setToolTip(QStringLiteral("DSH 后台控制器"));
        auto* menu = new QMenu(this);
        menu->addAction(QStringLiteral("显示主窗口"), this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        menu->addAction(QStringLiteral("启动服务"), this, &MainWindow::onStart);
        menu->addAction(QStringLiteral("停止服务"), this, &MainWindow::onStop);
        menu->addSeparator();
        menu->addAction(QStringLiteral("退出"), qApp, &QCoreApplication::quit);
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);
        m_tray->show();
    }

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::refreshStatus);
    m_statusTimer->start(2000);

    m_healthTimer = new QTimer(this);
    connect(m_healthTimer, &QTimer::timeout, this, &MainWindow::healthTick);

    refreshStatus();
    loadLogTail();
    logLine(QStringLiteral("DSH 控制器已就绪。点击「启动」让 DSH 服务在后台运行。"));
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_tray && m_tray->isVisible()) {
        hide();
        if (!m_trayTipShown) {
            m_trayTipShown = true;
            m_tray->showMessage(QStringLiteral("DSH 后台控制器"),
                                QStringLiteral("程序仍在后台运行, 点击托盘图标可重新打开窗口。"),
                                QSystemTrayIcon::Information, 3000);
        }
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        raise();
        activateWindow();
    }
}

// ---------------------------------------------------------------- 动作

void MainWindow::onStart()
{
    dsh::InstanceInfo info = dsh::managedInfo();
    if (info.managed) {
        logLine(QStringLiteral("[跳过] DSH 已在运行: PID=%1, 端口=%2")
                    .arg(info.pid)
                    .arg(info.port));
        return;
    }
    dsh::removePidFile();

    int port = m_portSpin->value();
    if (dsh::portListening(port)) {
        logLine(QStringLiteral("[警告] 端口 %1 已被占用, 且不是本程序管理的实例。").arg(port));
        logLine(QStringLiteral("       如果是旧 PowerShell 窗口启动的, 可点「清理旧实例」后再启动。"));
        QMessageBox::warning(this, QStringLiteral("端口被占用"),
                             QStringLiteral("端口 %1 已被其他实例占用。\n\n"
                                            "如果它是旧 PowerShell 窗口启动的 DSH, 请点「清理旧实例」后再启动。")
                                 .arg(port));
        return;
    }

    QString cmd = m_cmdEdit->text().trimmed();
    if (cmd.isEmpty())
        cmd = dsh::kDefaultCmd;
    QSettings settings(QStringLiteral("DSHCtl"), QStringLiteral("DSHCtl"));
    settings.setValue(QStringLiteral("port"), port);
    settings.setValue(QStringLiteral("cmd"), cmd);

    QFile lf(dsh::logFilePath());
    if (lf.open(QIODevice::Append)) {
        lf.write(QStringLiteral("\n%1\n[%2] 启动: %3\n")
                     .arg(QString(50, QLatin1Char('=')))
                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                     .arg(cmd)
                     .toUtf8());
        lf.close();
    }

    qint64 pid = 0;
    if (!dsh::spawnHidden(cmd, dsh::baseDir(), dsh::logFilePath(), dsh::errFilePath(), &pid)) {
        logLine(QStringLiteral("[错误] 启动失败(请确认启动命令可执行)。"));
        return;
    }
    dsh::writePidFile(pid, port, cmd);
    logLine(QStringLiteral("[启动] 已后台启动 PID=%1, 端口=%2, 等待端口就绪 ...")
                .arg(pid)
                .arg(port));
    startHealthWait(pid);
}

void MainWindow::startHealthWait(qint64 pid)
{
    m_healthPid = pid;
    m_healthWaited = 0;
    m_healthTimer->start(1000);
}

void MainWindow::healthTick()
{
    int port = m_portSpin->value();
    if (dsh::portListening(port)) {
        m_healthTimer->stop();
        logLine(QStringLiteral("[就绪] 服务已可访问: http://127.0.0.1:%1").arg(port));
        refreshStatus();
        return;
    }
    if (!dsh::processAlive(m_healthPid)) {
        m_healthTimer->stop();
        logLine(QStringLiteral("[失败] 进程已退出, 最近的日志:"));
        logLine(dsh::tailOfFile(dsh::errFilePath(), 15));
        logLine(dsh::tailOfFile(dsh::logFilePath(), 8));
        dsh::removePidFile();
        refreshStatus();
        return;
    }
    if (++m_healthWaited >= 60) {
        m_healthTimer->stop();
        logLine(QStringLiteral("[提示] 已等待 60s, 端口仍未就绪(首次启动可能要下载依赖)。可稍后查看状态或日志。"));
    }
}

void MainWindow::onStop()
{
    dsh::InstanceInfo info = dsh::managedInfo();
    if (!info.managed) {
        logLine(QStringLiteral("[停止] 未发现运行中的受管理实例。"));
        return;
    }
    logLine(QStringLiteral("[停止] 正在终止 PID=%1 及其子进程树 ...").arg(info.pid));
    dsh::killTree(info.pid);
    for (int i = 0; i < 30 && dsh::processAlive(info.pid); ++i) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }
    dsh::removePidFile();
    if (dsh::processAlive(info.pid))
        logLine(QStringLiteral("[警告] PID=%1 似乎仍在运行, 请手动检查。").arg(info.pid));
    else
        logLine(QStringLiteral("[完成] 已停止 (PID=%1)。").arg(info.pid));
    refreshStatus();
}

void MainWindow::onRestart()
{
    onStop();
    QTimer::singleShot(1500, this, &MainWindow::onStart);
}

void MainWindow::onOpenBrowser()
{
    int port = m_portSpin->value();
    dsh::InstanceInfo info = dsh::managedInfo();
    if (info.managed)
        port = info.port;
    QDesktopServices::openUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
}

void MainWindow::onScanLegacy()
{
    auto ret = QMessageBox::question(
        this, QStringLiteral("清理旧实例"),
        QStringLiteral("将扫描并强制结束命令行包含 '@deepseek-ai/dsh' 的进程(及其全部子进程)。\n\n"
                       "这会关闭对应的 DSH 会话, 包括浏览器里正在使用的页面。确认继续?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    logLine(QStringLiteral("[扫描] 正在查找旧实例 ..."));
    QList<qint64> pids = dsh::scanMarkedPids(dsh::kDefaultMatch);
    if (pids.isEmpty()) {
        logLine(QStringLiteral("[扫描] 未找到匹配进程。"));
        return;
    }
    QStringList strPids;
    for (qint64 pid : pids)
        strPids << QString::number(pid);
    logLine(QStringLiteral("[扫描] 找到 %1 个进程: %2, 正在清理 ...")
                .arg(pids.size())
                .arg(strPids.join(QStringLiteral(", "))));
    for (qint64 pid : pids)
        if (dsh::processAlive(pid))
            dsh::killTree(pid);
    QThread::msleep(800);
    QStringList left;
    for (qint64 pid : pids)
        if (dsh::processAlive(pid))
            left << QString::number(pid);
    if (!left.isEmpty())
        logLine(QStringLiteral("[警告] 仍存活的进程: %1").arg(left.join(QStringLiteral(", "))));
    else
        logLine(QStringLiteral("[完成] 已清理。"));
    refreshStatus();
}

// ---------------------------------------------------------------- 状态/日志

void MainWindow::refreshStatus()
{
    dsh::InstanceInfo info = dsh::managedInfo();
    int port = m_portSpin->value();
    if (info.managed) {
        bool ready = dsh::portListening(info.port);
        qint64 up = QDateTime::currentSecsSinceEpoch() - info.startedTs;
        m_statusLabel->setStyleSheet(QStringLiteral("color:#1a7f37;"));
        m_statusLabel->setText(QStringLiteral("● 运行中  http://127.0.0.1:%1   PID=%2   %3 (已运行 %4)")
                                   .arg(info.port)
                                   .arg(info.pid)
                                   .arg(ready ? QStringLiteral("端口就绪") : QStringLiteral("端口未就绪"))
                                   .arg(dsh::formatUptime(up)));
    } else if (dsh::portListening(port)) {
        m_statusLabel->setStyleSheet(QStringLiteral("color:#b35900;"));
        m_statusLabel->setText(QStringLiteral("○ 已停止, 但端口 %1 被未受管理的实例占用(可点「清理旧实例」)")
                                   .arg(port));
    } else {
        m_statusLabel->setStyleSheet(QStringLiteral("color:#57606a;"));
        m_statusLabel->setText(QStringLiteral("○ 已停止"));
    }
    if (logViewAtBottom())
        loadLogTail();
}

bool MainWindow::logViewAtBottom() const
{
    QScrollBar* sb = m_logView->verticalScrollBar();
    return sb->value() >= sb->maximum() - 4;
}

void MainWindow::loadLogTail()
{
    QString out = dsh::tailOfFile(dsh::logFilePath(), 60);
    QString err = dsh::tailOfFile(dsh::errFilePath(), 30);
    QString text;
    if (!out.isEmpty())
        text += QStringLiteral("----- dsh.log -----\n") + out;
    if (!err.isEmpty())
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) +
                QStringLiteral("----- dsh.err.log -----\n") + err;
    if (text.isEmpty())
        text = QStringLiteral("(暂无日志)");
    m_logView->setPlainText(text);
    QScrollBar* sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::logLine(const QString& text)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
    QScrollBar* sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::onRefreshLogs()
{
    loadLogTail();
    logLine(QStringLiteral("[日志] 已刷新。"));
}

void MainWindow::onClearLogs()
{
    dsh::clearFile(dsh::logFilePath());
    dsh::clearFile(dsh::errFilePath());
    logLine(QStringLiteral("[日志] 已清空。"));
}

QIcon MainWindow::makeAppIcon() const
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, 64);
    g.setColorAt(0, QColor(QStringLiteral("#2676d4")));
    g.setColorAt(1, QColor(QStringLiteral("#0f3a8c")));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(2, 2, 60, 60, 14, 14);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(26);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, QStringLiteral("DSH"));
    return QIcon(pm);
}
