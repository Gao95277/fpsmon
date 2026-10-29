# fpsmon

一个极简的游戏帧数监控工具。屏幕顶端一条横条，显示 FPS、1% low、CPU、GPU、内存占用。

不注入游戏进程、不读游戏内存、不 hook 图形接口。

![效果](docs/preview.png)

## 它和微星小飞机 / RTSS / FPS Monitor 的区别

那些工具都是在游戏进程内部跑一个 DLL，hook 图形接口往画面上画字。代价是：要适配 D3D11 / D3D12 / OpenGL / Vulkan 四套呈现路径、要处理 32/64 位、要装内核驱动读温度、要应付反作弊兼容性。界面上看到的复杂，是这些底层麻烦漏出来的部分。

这个工具在游戏外面。它读 Windows 自带的 ETW 事件流里 `Microsoft-Windows-DXGI` 的 Present 事件，只取其中的**进程 ID** 和**高精度时间戳**两个字段，用来算帧间隔。

游戏进程里没有任何属于它的模块。反作弊扫进程模块扫不到东西，风险等级等同于"开着任务管理器玩游戏"。

## 指标

| 指标 | 含义 |
|---|---|
| FPS | 最近 1 秒的平均帧率，由帧时间累加得出，不是"数帧数" |
| 1% low | 最近 30 秒内，最慢的 1% 帧的平均帧时间换算出的帧率 |
| CPU | 整机 CPU 占用，`GetSystemTimes` 差值 |
| GPU | 显卡 3D 引擎占用，PDH `\GPU Engine(*)\Utilization Percentage` |
| RAM | 物理内存占用，`GlobalMemoryStatusEx` |

1% low 的算法与 CapFrameX 等评测工具一致：窗口内**每一帧**都参与排序，取最慢 1% 的平均，不剔除任何样本。

## 工作原理

```
游戏 (IDXGISwapChain::Present)
      ↓  ETW 事件（系统内核产生，带 QPC 时间戳）
Microsoft-Windows-DXGI provider, EventID 42
      ↓  实时会话订阅，回调里只读 ProcessId + TimeStamp
本程序：按前台窗口进程过滤 → 帧时间序列 → 统计 → Direct2D 绘制到置顶窗口
```

关键技术点：

- **EventID 42** 是实际探测出来的，不是硬编码猜的。仓库里的 `tools/etwtest.cpp` 会同时订阅全部 DXGI 事件并自己跑一个 60fps 呈现循环，验证哪个 ID 与帧数严格 1:1 对应。
- **会话名固定**，启动时先清理同名残留。带进程号的会话名在异常退出后会永久残留，每个残留会话占一个 DXGI 订阅名额，累积几次就再也订阅不上事件源。
- **缓冲必须 64KB**。实测 8KB 会丢掉一半事件（60fps 被读成 30fps），这是 ETW 实时模式的固有行为。
- **显示层鼠标穿透**：`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，窗口过程里 `WM_NCHITTEST` 直接返回 `HTTRANSPARENT`，双保险。

## 直接使用（不用编译）

仓库的 `dist/` 目录里放了现成的构建产物：

- **`dist/fpsmon.exe`** — 单个可执行文件，双击就能跑
- **`dist/FpsMon-v1.0.0.zip`** — 绿色版压缩包，解压到任意位置即可，不写注册表、不留残留

也可以在 [Releases](https://github.com/Gao95277/fpsmon/releases) 页面下载。

## 构建

需要 Visual Studio 2019+ 和 Windows 10 SDK。

用 CMake：

```bash
cmake -B build -A x64
cmake --build build --config Release
```

或者直接用 MSVC 命令行：

```bash
cl /nologo /EHsc /std:c++20 /O2 /DUNICODE /D_UNICODE /utf-8 \
   /Fe:fpsmon.exe src\main.cpp src\overlay.cpp src\frame_source.cpp src\sysinfo.cpp
```

输出单个 exe，无运行时依赖。

## 使用

双击运行，横条出现在屏幕顶端。托盘图标右键可以：

- 显示 / 隐藏
- 位置：屏幕顶端 / 屏幕底端
- 开机自动启动
- 退出

横条完全不接收鼠标，点击、拖动、滚轮全部穿透到下层窗口。

## 已知限制

- **独占全屏看不见**。切分辨率那种真全屏不行，无边框全屏和窗口化正常。
- **数据约 1 秒刷新一次**。ETW 实时会话按秒批量投递事件（`FlushTimer` 最小 1 秒），这是系统侧的节奏。数值本身是逐帧精算的。
- **模拟器里读到的是模拟器输出帧率**，不是游戏内部帧率。模拟器锁 60 输出时，游戏内部掉帧看不出来。
- **GPU 占用走系统通用计数器**，与 Adrenalin / Afterburner 读驱动接口得到的值可能相差几个百分点。
- **多显示器未适配**，只跟随主屏工作区。
- 横条宽度固定 438 逻辑像素，在 1366 宽度的屏幕上占比会偏大。

## 项目结构

```
src/
  main.cpp          入口、托盘、定时刷新
  frame_source.h/cpp ETW 采集
  stats.h           帧时间统计（纯计算）
  sysinfo.h/cpp     CPU / GPU / 内存采样
  overlay.h/cpp     置顶透明窗口 + Direct2D 绘制
tools/
  probe.cpp         枚举 DXGI provider 的全部事件
  etwtest.cpp       验证事件 ID 与帧数的一一对应关系
  minitest.cpp      隔离测试采集层
  shot.cpp          屏幕截取（调试用）
  mkshortcut.cpp    创建快捷方式（安全策略禁用 COM 时的备选）
```

## 许可

MIT
