#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include "DshCtl.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>
#include <QTcpSocket>

namespace dsh {

const QString kDefaultCmd = QStringLiteral("npx @deepseek-ai/dsh web");
const int kDefaultPort = 3080;
const QString kDefaultMatch = QStringLiteral("@deepseek-ai/dsh");

QString baseDir()
{
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return QFileInfo(QString::fromWCharArray(buf)).absolutePath();
    return QDir::currentPath();
}

QString pidFilePath() { return QDir(baseDir()).filePath(QStringLiteral("dsh.pid")); }
QString logFilePath() { return QDir(baseDir()).filePath(QStringLiteral("dsh.log")); }
QString errFilePath() { return QDir(baseDir()).filePath(QStringLiteral("dsh.err.log")); }

// ---------------------------------------------------------------- 进程工具

static HANDLE openProc(qint64 pid, DWORD access)
{
    return OpenProcess(access, FALSE, static_cast<DWORD>(pid));
}

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    HANDLE h = openProc(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if (!h)
        return false;
    DWORD code = 0;
    bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return ok;
}

quint64 processCreationFt(qint64 pid)
{
    if (pid <= 0)
        return 0;
    HANDLE h = openProc(pid, PROCESS_QUERY_LIMITED_INFORMATION);
    if (!h)
        return 0;
    FILETIME ct, et, kt, ut;
    quint64 value = 0;
    if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
        ULARGE_INTEGER ul;
        ul.LowPart = ct.dwLowDateTime;
        ul.HighPart = ct.dwHighDateTime;
        value = ul.QuadPart;
    }
    CloseHandle(h);
    return value;
}

static bool terminateProc(qint64 pid)
{
    HANDLE h = openProc(pid, PROCESS_TERMINATE);
    if (!h)
        return false;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != FALSE;
}

static bool killTreeNative(qint64 rootPid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    QHash<qint64, QList<qint64>> children;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            children[static_cast<qint64>(pe.th32ParentProcessID)]
                .append(static_cast<qint64>(pe.th32ProcessID));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // 收集子树
    QList<qint64> order;
    QSet<qint64> seen;
    QList<qint64> stack{rootPid};
    while (!stack.isEmpty()) {
        qint64 p = stack.takeLast();
        if (seen.contains(p))
            continue;
        seen.insert(p);
        order.append(p);
        stack.append(children.value(p));
    }

    // 先杀子进程再杀根
    bool killedAny = false;
    for (int i = order.size() - 1; i >= 1; --i)
        killedAny = terminateProc(order.at(i)) || killedAny;
    bool rootKilled = terminateProc(rootPid);
    return rootKilled || killedAny;
}

bool killTree(qint64 pid)
{
    if (pid <= 0)
        return false;
    if (killTreeNative(pid))
        return true;
    return QProcess::execute(QStringLiteral("taskkill"),
                             {QStringLiteral("/PID"), QString::number(pid),
                              QStringLiteral("/T"), QStringLiteral("/F")}) == 0;
}

// ---------------------------------------------------------------- 状态记录

InstanceInfo managedInfo()
{
    InstanceInfo info;
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return info;
    QString line = QString::fromUtf8(f.readAll()).trimmed();
    f.close();

    QStringList parts = line.split('|');
    if (parts.size() < 4)
        return info;
    bool ok = false;
    qint64 pid = parts.at(0).toLongLong(&ok);
    if (!ok)
        return info;
    quint64 created = parts.at(1).toULongLong(&ok);
    if (!ok)
        return info;
    qint64 started = parts.at(2).toLongLong(&ok);
    if (!ok)
        return info;
    int port = parts.at(3).toInt(&ok);
    if (!ok)
        return info;

    if (!processAlive(pid))
        return info; // 进程已不在, 记录失效

    quint64 cur = processCreationFt(pid);
    if (cur != 0 && (cur > created ? cur - created : created - cur) > 20000000ULL)
        return info; // 创建时间差 > 2s: PID 被系统复用

    info.managed = true;
    info.pid = pid;
    info.createdFt = created;
    info.startedTs = started;
    info.port = port;
    info.cmd = parts.size() > 4 ? parts.at(4) : kDefaultCmd;
    return info;
}

bool writePidFile(qint64 pid, int port, const QString& cmd)
{
    QFile f(pidFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QString line = QStringLiteral("%1|%2|%3|%4|%5")
                       .arg(pid)
                       .arg(processCreationFt(pid))
                       .arg(QDateTime::currentSecsSinceEpoch())
                       .arg(port)
                       .arg(cmd);
    f.write(line.toUtf8());
    f.close();
    return true;
}

void removePidFile()
{
    QFile::remove(pidFilePath());
}

// ---------------------------------------------------------------- 端口检测

bool portListening(int port, int timeoutMs)
{
    if (port <= 0)
        return false;
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port));
    bool ok = socket.waitForConnected(timeoutMs);
    socket.disconnectFromHost();
    return ok;
}

// ---------------------------------------------------------------- 隐藏启动

bool spawnHidden(const QString& cmd, const QString& cwd,
                 const QString& logPath, const QString& errPath,
                 qint64* pidOut)
{
    if (pidOut)
        *pidOut = 0;

    QString comspec = QString::fromLocal8Bit(qgetenv("COMSPEC"));
    if (comspec.isEmpty())
        comspec = QStringLiteral("C:\\Windows\\system32\\cmd.exe");

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    std::wstring logW = QDir::toNativeSeparators(logPath).toStdWString();
    std::wstring errW = QDir::toNativeSeparators(errPath).toStdWString();
    HANDLE hOut = CreateFileW(logW.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE hErr = CreateFileW(errW.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE hIn = CreateFileW(L"NUL", GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE)
        hOut = nullptr;
    if (hErr == INVALID_HANDLE_VALUE)
        hErr = nullptr;
    if (hIn == INVALID_HANDLE_VALUE)
        hIn = nullptr;

    // 防止派生的服务进程继承控制器的标准句柄(否则调用方重定向输出时会一直占用,
    // 导致等待 EOF 的调用方挂起); 只让日志文件句柄可继承
    HANDLE stdHandles[3] = {GetStdHandle(STD_INPUT_HANDLE),
                            GetStdHandle(STD_OUTPUT_HANDLE),
                            GetStdHandle(STD_ERROR_HANDLE)};
    for (HANDLE h : stdHandles) {
        if (h && h != INVALID_HANDLE_VALUE)
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = hErr ? hErr : GetStdHandle(STD_ERROR_HANDLE);

    QString cmdLineStr = QStringLiteral("\"%1\" /c %2").arg(comspec, cmd);
    std::wstring cmdLine = cmdLineStr.toStdWString();
    std::wstring comspecW = comspec.toStdWString();
    std::wstring cwdW = QDir::toNativeSeparators(cwd).toStdWString();
    LPCWSTR cwdPtr = cwdW.empty() ? nullptr : cwdW.c_str();

    // CREATE_NO_WINDOW 让服务在隐藏控制台运行; 沙箱测试环境不支持该标志
    DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT;
    if (qgetenv("DSH_CTL_SANDBOX_TEST").isEmpty())
        flags |= CREATE_NO_WINDOW;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessW(comspecW.c_str(), cmdLine.data(), nullptr, nullptr,
                             TRUE, flags, nullptr, cwdPtr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (pidOut)
            *pidOut = static_cast<qint64>(pi.dwProcessId);
    }
    if (hOut)
        CloseHandle(hOut);
    if (hErr)
        CloseHandle(hErr);
    if (hIn)
        CloseHandle(hIn);
    return ok != FALSE;
}

// ---------------------------------------------------------------- 扫描

QString scanPsScript()
{
    // marker 通过环境变量 DSH_MARK 传入, 避免出现在脚本自身的命令行里造成自匹配
    return QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue';"
        "$re = [regex]::Escape($env:DSH_MARK);"
        "$anc = @{};"
        "$p = Get-CimInstance Win32_Process -Filter \"ProcessId=$PID\";"
        "while ($p) {"
        "  $anc[$p.ProcessId] = $true;"
        "  $p = Get-CimInstance Win32_Process -Filter \"ProcessId=$($p.ParentProcessId)\";"
        "};"
        "Get-CimInstance Win32_Process |"
        "  Where-Object { $_.CommandLine -match $re -and -not $anc.ContainsKey($_.ProcessId) } |"
        "  ForEach-Object { $_.ProcessId }");
}

QList<qint64> scanMarkedPids(const QString& marker)
{
    QList<qint64> pids;

    // 测试桩: 沙箱内 CIM 不可用, 仅供开发测试注入; 用户环境不会设置此变量
    QByteArray stub = qgetenv("DSH_CTL_SCAN_STUB");
    if (!stub.isEmpty()) {
        for (const QByteArray& part : stub.split(',')) {
            bool ok = false;
            qint64 v = part.trimmed().toLongLong(&ok);
            if (ok)
                pids.append(v);
        }
        return pids;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_MARK"), marker);

    QString tmp = QDir(baseDir()).filePath(QStringLiteral(".dsh_scan.tmp"));
    QProcess process;
    process.setProcessEnvironment(env);
    process.setStandardOutputFile(tmp, QIODevice::WriteOnly);
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-NonInteractive"),
                   QStringLiteral("-Command"), scanPsScript()});
    process.waitForFinished(120000);

    QFile f(tmp);
    if (f.open(QIODevice::ReadOnly)) {
        QTextStream ts(&f);
        ts.setAutoDetectUnicode(true);
        while (!ts.atEnd()) {
            bool ok = false;
            qint64 v = ts.readLine().trimmed().toLongLong(&ok);
            if (ok)
                pids.append(v);
        }
        f.close();
    }
    QFile::remove(tmp);
    return pids;
}

// ---------------------------------------------------------------- 日志工具

QString tailOfFile(const QString& path, int maxLines)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const qint64 cap = 256 * 1024;
    qint64 size = f.size();
    f.seek(size > cap ? size - cap : 0);
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts.setAutoDetectUnicode(true);
    QStringList lines;
    while (!ts.atEnd()) {
        lines.append(ts.readLine());
        if (lines.size() > maxLines)
            lines.removeFirst();
    }
    f.close();
    return lines.join('\n');
}

bool clearFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.close();
    return true;
}

QString formatUptime(qint64 seconds)
{
    if (seconds < 0)
        seconds = 0;
    qint64 d = seconds / 86400;
    qint64 h = (seconds % 86400) / 3600;
    qint64 m = (seconds % 3600) / 60;
    qint64 s = seconds % 60;
    if (d > 0)
        return QStringLiteral("%1天%2小时%3分").arg(d).arg(h).arg(m);
    if (h > 0)
        return QStringLiteral("%1小时%2分").arg(h).arg(m);
    return QStringLiteral("%1分%2秒").arg(m).arg(s);
}

} // namespace dsh
