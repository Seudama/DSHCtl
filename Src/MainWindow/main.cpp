#include "DshCtl.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "shell32.lib")
#endif

// ---------------------------------------------------------------- CLI

static void out(const QString& s)
{
    fprintf(stdout, "%s\n", s.toUtf8().constData());
    fflush(stdout);
}

static QStringList argsFromArgv(int argc, char* argv[])
{
    QStringList list;
    for (int i = 1; i < argc; ++i)
        list << QString::fromLocal8Bit(argv[i]);
    return list;
}

static int cliStart(const QCommandLineParser& p)
{
    QString cmd = p.value(QStringLiteral("cmd"));
    if (cmd.isEmpty())
        cmd = dsh::kDefaultCmd;
    int port = p.value(QStringLiteral("port")).toInt();
    if (port <= 0)
        port = dsh::kDefaultPort;
    int wait = p.value(QStringLiteral("wait")).toInt();
    QString cwd = p.value(QStringLiteral("cwd"));
    if (cwd.isEmpty())
        cwd = dsh::baseDir();

    dsh::InstanceInfo info = dsh::managedInfo();
    if (info.managed) {
        out(QStringLiteral("[跳过] DSH 已在运行: PID=%1, 端口=%2").arg(info.pid).arg(info.port));
        return 0;
    }
    dsh::removePidFile();

    if (dsh::portListening(port)) {
        out(QStringLiteral("[警告] 端口 %1 已被占用, 且不是本程序管理的实例。").arg(port));
        out(QStringLiteral("       可用 stop --scan 清理后重试。"));
        return 2;
    }

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
    if (!dsh::spawnHidden(cmd, cwd, dsh::logFilePath(), dsh::errFilePath(), &pid)) {
        out(QStringLiteral("[错误] 启动失败。"));
        return 1;
    }
    dsh::writePidFile(pid, port, cmd);
    out(QStringLiteral("[启动] 已在后台启动: %1").arg(cmd));
    out(QStringLiteral("       PID=%1 (进程树根), 端口=%2").arg(pid).arg(port));

    if (wait <= 0)
        return 0;
    for (int i = 0; i < wait; ++i) {
        if (dsh::portListening(port)) {
            out(QStringLiteral("[就绪] 服务已可访问: http://127.0.0.1:%1").arg(port));
            return 0;
        }
        if (!dsh::processAlive(pid)) {
            out(QStringLiteral("[失败] 进程已退出, 最近的日志:"));
            out(dsh::tailOfFile(dsh::errFilePath(), 15));
            out(dsh::tailOfFile(dsh::logFilePath(), 8));
            dsh::removePidFile();
            return 1;
        }
        QThread::msleep(1000);
    }
    out(QStringLiteral("[提示] 已等待 %1s, 端口仍未就绪(首次启动可能要下载依赖)。").arg(wait));
    return 0;
}

static int cliScanKill(const QCommandLineParser& p)
{
    QString marker = p.value(QStringLiteral("match"));
    if (marker.isEmpty())
        marker = dsh::kDefaultMatch;
    QList<qint64> pids = dsh::scanMarkedPids(marker);
    if (pids.isEmpty()) {
        out(QStringLiteral("[扫描] 未找到命令行包含 '%1' 的进程。").arg(marker));
        return 0;
    }
    QStringList strPids;
    for (qint64 v : pids)
        strPids << QString::number(v);
    out(QStringLiteral("[扫描] 找到 %1 个匹配进程: %2").arg(pids.size()).arg(strPids.join(QStringLiteral(", "))));
    out(QStringLiteral("[警告] 将强制结束这些进程及其全部子进程!"));
    if (!p.isSet(QStringLiteral("yes"))) {
        out(QStringLiteral("确认继续? [y/N] "));
        QTextStream in(stdin);
        QString ans = in.readLine().trimmed().toLower();
        if (ans != QLatin1String("y") && ans != QLatin1String("yes")) {
            out(QStringLiteral("[取消] 未执行任何操作。"));
            return 0;
        }
    }
    for (qint64 pid : pids)
        if (dsh::processAlive(pid))
            dsh::killTree(pid);
    QThread::msleep(1000);
    QStringList left;
    for (qint64 pid : pids)
        if (dsh::processAlive(pid))
            left << QString::number(pid);
    if (!left.isEmpty()) {
        out(QStringLiteral("[警告] 以下进程仍存活: %1").arg(left.join(QStringLiteral(", "))));
        return 1;
    }
    out(QStringLiteral("[完成] 已清理。"));
    return 0;
}

static int cliStop(const QCommandLineParser& p, bool doScan)
{
    dsh::InstanceInfo info = dsh::managedInfo();
    if (info.managed) {
        out(QStringLiteral("[停止] 正在终止 PID=%1 及其子进程树 ...").arg(info.pid));
        dsh::killTree(info.pid);
        for (int i = 0; i < 30 && dsh::processAlive(info.pid); ++i)
            QThread::msleep(500);
        dsh::removePidFile();
        if (dsh::processAlive(info.pid)) {
            out(QStringLiteral("[警告] PID=%1 似乎仍在运行, 请手动检查。").arg(info.pid));
            return 1;
        }
        out(QStringLiteral("[完成] 已停止 (PID=%1)。").arg(info.pid));
    } else {
        if (QFile::exists(dsh::pidFilePath())) {
            dsh::removePidFile();
            out(QStringLiteral("[停止] 未发现运行中的受管理实例, 已清理残留记录。"));
        } else {
            out(QStringLiteral("[停止] 未发现运行中的受管理实例。"));
        }
    }
    if (doScan)
        return cliScanKill(p);
    return 0;
}

static int cliRestart(const QCommandLineParser& p)
{
    cliStop(p, false);
    QThread::msleep(1500);
    return cliStart(p);
}

static int cliStatus(const QCommandLineParser& p)
{
    dsh::InstanceInfo info = dsh::managedInfo();
    if (info.managed) {
        bool ready = dsh::portListening(info.port);
        qint64 up = QDateTime::currentSecsSinceEpoch() - info.startedTs;
        out(QStringLiteral("[状态] 运行中"));
        out(QStringLiteral("       命令 : %1").arg(info.cmd));
        out(QStringLiteral("       PID  : %1").arg(info.pid));
        out(QStringLiteral("       端口 : %1 (%2)")
                .arg(info.port)
                .arg(ready ? QStringLiteral("可访问") : QStringLiteral("尚未就绪")));
        out(QStringLiteral("       启动 : %1 (已运行 %2)")
                .arg(QDateTime::fromSecsSinceEpoch(info.startedTs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                .arg(dsh::formatUptime(up)));
        out(QStringLiteral("       日志 : %1").arg(dsh::logFilePath()));
        return 0;
    }
    if (QFile::exists(dsh::pidFilePath()))
        dsh::removePidFile();
    int port = p.value(QStringLiteral("port")).toInt();
    if (port <= 0)
        port = dsh::kDefaultPort;
    if (dsh::portListening(port)) {
        out(QStringLiteral("[状态] 未管理任何实例, 但端口 %1 上有实例在监听(可能是旧窗口启动的)。").arg(port));
        return 1;
    }
    out(QStringLiteral("[状态] 已停止。"));
    return 0;
}

static int cliLogs(const QCommandLineParser& p)
{
    if (p.isSet(QStringLiteral("clear"))) {
        dsh::clearFile(dsh::logFilePath());
        dsh::clearFile(dsh::errFilePath());
        out(QStringLiteral("[日志] 已清空。"));
        return 0;
    }
    int n = p.value(QStringLiteral("lines")).toInt();
    if (n <= 0)
        n = 20;
    out(QStringLiteral("----- dsh.log -----"));
    out(dsh::tailOfFile(dsh::logFilePath(), n));
    out(QStringLiteral("----- dsh.err.log -----"));
    out(dsh::tailOfFile(dsh::errFilePath(), n));
    return 0;
}

static int runCli(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DSH 后台控制器 CLI"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("action"),
                                 QStringLiteral("start | stop | restart | status | logs"),
                                 QStringLiteral("[action]"));
    QCommandLineOption optCmd(QStringLiteral("cmd"),
                              QStringLiteral("启动命令(默认: npx @deepseek-ai/dsh web)"),
                              QStringLiteral("cmd"));
    QCommandLineOption optPort(QStringLiteral("port"),
                               QStringLiteral("HTTP 端口(默认 3080)"),
                               QStringLiteral("port"), QStringLiteral("3080"));
    QCommandLineOption optWait(QStringLiteral("wait"),
                               QStringLiteral("start 后等待端口就绪的秒数(默认 60)"),
                               QStringLiteral("sec"), QStringLiteral("60"));
    QCommandLineOption optScan(QStringLiteral("scan"),
                               QStringLiteral("stop 时扫描清理未受管理的匹配进程"));
    QCommandLineOption optMatch(QStringLiteral("match"),
                                QStringLiteral("扫描关键字(默认 @deepseek-ai/dsh)"),
                                QStringLiteral("s"));
    QCommandLineOption optYes(QStringLiteral("yes"),
                              QStringLiteral("扫描清理时跳过确认"));
    QCommandLineOption optLines(QStringLiteral("lines"),
                                QStringLiteral("logs 显示行数(默认 20)"),
                                QStringLiteral("n"), QStringLiteral("20"));
    QCommandLineOption optClear(QStringLiteral("clear"),
                                QStringLiteral("logs 时清空日志"));
    QCommandLineOption optCwd(QStringLiteral("cwd"),
                              QStringLiteral("启动命令的工作目录(默认 exe 目录)"),
                              QStringLiteral("dir"));
    parser.addOptions({optCmd, optPort, optWait, optScan, optMatch, optYes,
                       optLines, optClear, optCwd});
    // QCommandLineParser::process 要求列表首元素为程序名
    QStringList processArgs = argsFromArgv(argc, argv);
    processArgs.prepend(QString::fromLocal8Bit(argv[0]));
    parser.process(processArgs);

    QStringList pos = parser.positionalArguments();
    if (pos.isEmpty()) {
        parser.showHelp(0);
    }
    QString action = pos.first().toLower();
    if (action == QLatin1String("start"))
        return cliStart(parser);
    if (action == QLatin1String("stop"))
        return cliStop(parser, parser.isSet(QStringLiteral("scan")));
    if (action == QLatin1String("restart"))
        return cliRestart(parser);
    if (action == QLatin1String("status"))
        return cliStatus(parser);
    if (action == QLatin1String("logs"))
        return cliLogs(parser);
    parser.showHelp(1);
    return 1;
}

// ---------------------------------------------------------------- 入口

int main(int argc, char* argv[])
{
    QStringList raw = argsFromArgv(argc, argv);
    static const QStringList cliActions = {
        QStringLiteral("start"), QStringLiteral("stop"), QStringLiteral("restart"),
        QStringLiteral("status"), QStringLiteral("logs")};
    if (!raw.isEmpty()) {
        QString first = raw.first().toLower();
        if (cliActions.contains(first))
            return runCli(argc, argv);
        if (first == QLatin1String("help") || first == QLatin1String("--help") ||
            first == QLatin1String("-h")) {
            return runCli(argc, argv); // 打印帮助
        }
    }
    bool selftest = raw.contains(QStringLiteral("--gui-selftest"));

#ifdef Q_OS_WIN
    // 固定到任务栏时保持独立分组与图标
    SetCurrentProcessExplicitAppUserModelID(L"DSHCtl.DSHCtl.1");
#endif

    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("DSHCtl"));
    a.setApplicationDisplayName(QStringLiteral("DSH 后台控制器"));
    a.setQuitOnLastWindowClosed(false);

    // 单实例
    QLockFile lock(dsh::baseDir() + QStringLiteral("/DSHCtl.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("DSH 控制器"),
                                 QStringLiteral("DSH 控制器已经在运行, 请查看系统托盘图标。"));
        return 0;
    }

    MainWindow w;
    w.show();

    if (selftest) {
        QTimer::singleShot(1500, &a, [&a] {
            fprintf(stdout, "GUI_SELFTEST_OK\n");
            fflush(stdout);
            a.quit();
        });
    }

    int rc = a.exec();
    lock.unlock();
    return rc;
}
