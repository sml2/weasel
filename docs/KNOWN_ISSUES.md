# 已知问题

## UWP / AppContainer 应用无法使用 Weasel 输入法

### 问题描述

在 Windows UWP 应用（AppContainer 沙箱进程）中，Weasel 输入法完全无效，无法输入中文。

**受影响的应用举例：**
- Windows 便笺（Microsoft.Notes.exe）
- 微软应用商店（WinStore.App.exe）
- 其他所有运行在 AppContainer 中的 UWP 应用

**不受影响的应用：**
- 记事本（notepad.exe，Win32）
- OneNote（ONENOTE.EXE，Win32）
- 其他普通 Win32 桌面应用

### 根本原因

**AppContainer 安全沙箱禁止加载第三方 TSF DLL。**

AppContainer 进程（UWP 应用）启动时，Windows 的 `msctf.dll`（TSF 客户端）不会向 AppContainer 进程注入第三方 IME DLL（如 `weaselx64.dll`）。这是 Windows 8 以来的安全设计，AppContainer 只信任来自 `System32` 的 TSF 组件。

**调查过程中的关键发现：**

1. 便笺（Microsoft.Notes.exe）和应用商店（WinStore.App.exe）均确认运行在 AppContainer 中（`TokenAppContainerSid = True`）
2. 两者进程内均加载了 `msctf.dll`（TSF 客户端），但**没有**加载 `weaselx64.dll`
3. `ApplicationFrameHost.exe`（UWP 宿主进程）加载了 `weaselx64.dll`，但其 `ActivateEx` 未被调用（MessageBox 调试确认）
4. `TextInputHost.exe`（Modern Input Stack 宿主）未加载任何第三方 IME DLL
5. Weasel 已注册 `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT`（`{13A016DF}`），但这不足以绕过 AppContainer 的 DLL 注入限制

### 解决方案方向：OOP TSF（Out-Of-Process TSF）

Windows 提供了 **Modern Input Stack / OOP TSF** 机制，允许第三方 IME 通过 `TextInputHost.exe` 代理为 UWP 应用提供输入服务，无需将 DLL 注入到 AppContainer 进程中。

**实现要求（架构级改造）：**
- 将输入逻辑迁移为进程外服务，由 `TextInputHost.exe` 调用
- 实现 `ITfFnGetPreferredTouchKeyboardLayout` 等 Modern Input Stack 接口
- 参考微软拼音、日文 IME 等系统内置 IME 的实现方式

**注意事项：**
- 微软拼音等系统 IME 注册了额外的 Category（如 `{3AF314A2-D79F-4B1B-9992-15086D339B05}`），该 GUID 不可随意注册到 Weasel，否则会导致系统所有新进程的输入法失效（已验证）
- 这是架构级改造，工作量较大，暂不实现

### 状态

- **发现日期：** 2025年
- **Windows 版本：** Windows 11 25H2（Build 26220）及以上
- **状态：** 已知限制，暂不修复，待日后研究 OOP TSF 实现
