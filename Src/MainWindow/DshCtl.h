#ifndef DSHCTL_H
#define DSHCTL_H

#include <QList>
#include <QString>
#include <QtGlobal>

// DSH (DeepSeek Harness) 后台服务控制核心
namespace dsh {

struct InstanceInfo {
    bool managed = false;   // 是否受本程序管理且存活
    qint64 pid = 0;         // 进程树根 PID (cmd.exe)
    quint64 createdFt = 0;  // 进程创建时间(FILETIME, 100ns), 防止 PID 复用误杀
    qint64 startedTs = 0;   // 启动时间戳(秒)
    int port = 3080;
    QString cmd;
};

// 文件位置(全部在 exe 所在目录, 绿色便携)
QString baseDir();
QString pidFilePath();
QString logFilePath();
QString errFilePath();

// 进程工具
bool processAlive(qint64 pid);
quint64 processCreationFt(qint64 pid);

// 状态记录(与 Python 版 dsh_ctl.py 的格式兼容: pid|created|started|port|cmd)
InstanceInfo managedInfo();
bool writePidFile(qint64 pid, int port, const QString& cmd);
void removePidFile();

// 端口检测
bool portListening(int port, int timeoutMs = 400);

// 隐藏窗口启动命令行(经 cmd.exe /c), stdout/stderr 重定向到日志文件
bool spawnHidden(const QString& cmd, const QString& cwd,
                 const QString& logPath, const QString& errPath,
                 qint64* pidOut);

// 终止进程及其整棵子进程树(先内核快照, 失败退回 taskkill /T /F)
bool killTree(qint64 pid);

// 扫描命令行包含 marker 的进程(自动排除本调用链祖先进程)
QList<qint64> scanMarkedPids(const QString& marker);
QString scanPsScript();

// 日志工具
QString tailOfFile(const QString& path, int maxLines);
bool clearFile(const QString& path);

QString formatUptime(qint64 seconds);

extern const QString kDefaultCmd;   // npx @deepseek-ai/dsh web
extern const int kDefaultPort;      // 3080
extern const QString kDefaultMatch; // @deepseek-ai/dsh

} // namespace dsh

#endif // DSHCTL_H
