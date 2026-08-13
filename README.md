# DSHCtl — DSH 后台控制器 (Qt/C++)

控制 `npx @deepseek-ai/dsh web` 服务启动/停止的桌面程序，图形界面 + 系统托盘，可固定到任务栏。

## 结构

- `Src/MainWindow/DshCtl.h/.cpp` — 服务控制核心（隐藏启动、进程树终止、pid 记录、端口探测、旧实例扫描）
- `Src/MainWindow/MainWindow.h/.cpp` — 主窗口与托盘
- `Src/MainWindow/main.cpp` — 入口（GUI 模式 + CLI 子命令）
- `Src/MainWindow/app.rc` / `app.ico` — 应用图标与版本信息
- `build.bat` — 一键构建脚本（自动探测 VS2022 与 Qt6）

## 构建

依赖：VS2022、Qt 6.10.2 (msvc2022_64)、CMake。

```cmd
build.bat release deploy     :: 编译 Release 并把 Qt 运行库部署到 bin\
build.bat                    :: RelWithDebInfo, 不部署
build.bat release deploy run :: 编译部署后直接运行
```

## 使用

- 双击 `bin\DSHCtl.exe` 打开控制窗口；关闭窗口最小化到托盘。
- 支持命令行子命令：`start / stop / restart / status / logs`，参数 `--cmd --port --wait --scan --match --yes --lines --clear --cwd`。

## 发布

- 源码仓库：<https://github.com/Seudama/DSHCtl>
- Release 下载（exe + Qt 运行库，开箱即用）：<https://github.com/Seudama/DSHCtl/releases/latest>

详细说明见 `E:\WorkSpace\Python\dsh-boot\README.md`。
