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

### 解决方案方向：Modern Input Stack（OOP TSF）

Windows 提供了 **Modern Input Stack** 机制，允许第三方 IME 通过 `TextInputHost.exe` 代理为 UWP 应用提供输入服务，无需将 DLL 注入到 AppContainer 进程。

#### 逆向工程发现（通过分析 imjptip.dll / TextInput.dll / windowsudk.shellcommon.dll）

**架构路径：**
```
UWP 应用 (AppContainer)
  └─ msctf.dll (TSF 客户端，不注入第三方 DLL)
       └─ windows.ui.core.textinput.dll
            └─ TextInputHost.exe (Modern Input Stack 宿主)
                  └─ TextInput.dll / InputApp.dll (WinRT 组件)
                       └─ Windows.UI.Internal.Text.Core.CoreTextSystemInputProcessor
                            └─ 各语言 SIPEndPoint (ChsInputProcessorForSIPEndPoint 等)
```

**核心接口（均为未文档化的内部 WinRT 接口）：**
- `Windows.UI.Internal.Text.Core.CoreTextSystemInputProcessor` — IME 注册到 TextInputHost 的核心接口
- `Windows.UI.Internal.Text.Core.CoreTextEditViewJunction` — 文本编辑视图桥接
- `Windows.UI.Internal.Text.Core.CoreKeyboardInputProfileManager` — 键盘输入配置管理
- `WindowsUdk.UI.Input.Text.KnownInputProcessorIds` — 已知输入处理器 ID 枚举
- `WindowsUdk.UI.Input.Text.TextInputSession` / `TextInputProfileManager` — 输入会话管理
- `WindowsInternal.ComposableShell.Experiences.TextInputUndocked.TextFramework.ISystemInputProcessor` — 系统输入处理器接口

**各语言内置 IME 的 SIPEndPoint 类（实现了上述接口）：**
- `ChsInputProcessorForSIPEndPoint` — 微软拼音（简体中文）
- `ChtInputProcessorForSIPEndPoint` — 微软注音（繁体中文）
- `JpnInputProcessorEndpointForSip` — 日文 IME

**关键辅助库：**
- `ime_textinputhelpers.dll` — 系统 IME 辅助库（字典加载、安装任务），9个无名导出函数（按序号调用），第三方无法使用

**重要结论：**
1. Modern Input Stack 完全基于**未公开的内部 WinRT 接口**，不是标准 COM/TSF 接口
2. `msctf.dll` 中没有任何 OOP TSF 桥接代码，两套架构完全独立
3. 系统 IME 通过 `CoreTextSystemInputProcessor` 接口向 `TextInputHost` 注册，这套接口对第三方完全封闭
4. `{3AF314A2-D79F-4B1B-9992-15086D339B05}` Category 是系统 IME 使用 Modern Input Stack 的标志，**不可随意注册**（已验证会导致系统输入法全部失效）

**实现难点：**
- 所有核心接口均为 `Windows.UI.Internal.*` 和 `WindowsInternal.*` 命名空间，微软明确标注为内部接口，无公开文档
- 需要完整逆向 `windows.ui.core.textinput.dll`（1.4MB）和 `windowsudk.shellcommon.dll`（6.2MB）的 WinRT vtable 布局
- 即使逆向成功，微软随时可能在系统更新中修改接口，维护成本极高
- 这是架构级改造，工作量巨大，暂不实现

#### 搜狗输入法逆向分析结论

通过逆向分析搜狗输入法（SogouTSF.dll / ImeFunc.dll / SGMyInput.exe / UIPlugin.dll）：

- 搜狗**不是 TSF IME**，而是 **IMM32 传统 IME + CUAS 兼容层**
- `SogouTSF.dll`：纯 COM 注册壳，只有 4 个标准导出，**无 msctf.dll 依赖**
- `SGMyInput.exe`：主程序，用 `ImmInstallIME()` 注册 `.ime` 文件，走传统 IMM32 路径
- 所有搜狗 DLL 均无 `msctf.dll`、`windows.ui.core.textinput.dll`、`WindowsUdk` 等 Modern Input Stack 依赖
- **CUAS**（CTF IME Compatibility Architecture）是 Windows 内置兼容层，负责把 IMM32 IME 包装成 TSF TIP，但在 AppContainer 沙箱中完全失效
- **结论：搜狗在 UWP/AppContainer 应用中同样无法使用**，这是行业普遍问题，不是 Weasel 特有缺陷

#### OOP TSF 真实机制（逆向 ChsIME.exe 发现）

**微软拼音的 OOP 实现方式已通过逆向确认：**

```
# 注册表：
HKLM\SOFTWARE\Classes\CLSID\{81d4e9c9}\LocalServer32
    = C:\Windows\System32\InputMethod\CHS\ChsIME.exe   ← 独立进程！

# 对比 Weasel：
HKLM\SOFTWARE\Classes\CLSID\{A3F4CDED}\InprocServer32
    = D:\Rime\weasel-0.17.4\weaselx64.dll              ← DLL 注入
```

**关键区别：**

| | 微软拼音/五笔 | 日文/韩文 IME | Weasel |
|---|---|---|---|
| COM 注册方式 | `LocalServer32`（独立进程） | `InProcServer32`（DLL） | `InProcServer32`（DLL） |
| UWP/AppContainer | ✅ 支持 | ❌ 不支持 | ❌ 不支持 |
| 注册 `{3AF314A2}` | ✅ 有 | ✅ 有 | ❌ 无（加了会崩溃） |

**上次加 `{3AF314A2}` 导致崩溃的真正原因：**
系统看到该 Category 后，会尝试以 OOP（`LocalServer32`）方式激活 Weasel，但 Weasel 没有注册 `LocalServer32`，COM 激活失败，导致所有进程的输入法激活链断裂。

**ChsIME.exe 的 OOP 工作流程：**
1. 用 `-Embedding` 参数启动（标准 OOP COM Server 模式）
2. 通过 `CoRegisterClassObject` 向 COM 注册自身
3. 接收来自 AppContainer 进程的跨进程 COM 调用（TSF 接口）
4. 输入处理结果通过 COM 回调传回 AppContainer 进程

**Weasel 实现 OOP TSF 的最小可行路径：**
1. 将 `WeaselServer.exe`（已有）注册为 `LocalServer32`
2. 在 `WeaselServer.exe` 中实现 `ITfTextInputProcessor` 等 TSF 接口（OOP 模式）
3. 注册 `{3AF314A2}` Category（中文系还需 `{74769EE9}`）
4. `weaselx64.dll` 的 `InProcServer32` 保留用于普通 Win32 应用

这比之前估计的**难度大幅降低**：不需要实现 WinRT 内部接口，只需在现有 `WeaselServer.exe` 上扩展标准 TSF COM 接口，走 `LocalServer32` OOP 路径即可。

### 状态

- **发现日期：** 2026年
- **Windows 版本：** Windows 11 25H2（Build 26220）及以上
- **状态：** 已知限制，暂不修复，待日后研究 OOP TSF 实现
