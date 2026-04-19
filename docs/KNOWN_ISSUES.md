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

### 解决方案方向：TSF3 / GIP / OOP TSF 深度逆向研究

#### 一、整体架构（本次逆向确认）

Windows 对 UWP 输入的处理分为两条平行路径：

```
【旧路径 - Win32 InProc TSF】
Win32 应用
  └─ msctf.dll (usesLegacyImplementation=true)
       └─ InProcServer32 → weaselx64.dll (直接注入，正常工作)

【新路径 - TSF3 / GIP / OOP】
UWP 应用 (AppContainer)
  └─ msctf.dll (usesLegacyImplementation=false，不注入第三方 DLL)
       └─ TextInputFramework.dll (ALPC 客户端)
            └─[ALPC]→ TextInputHost.exe (非 AppContainer，ALPC 服务端)
                  └─ tsf3gip.dll (GIP = Global Input Processor，mtfadapter)
                       ├─ ClassicTsf 路径 → InProc 加载白名单内的 IME DLL
                       └─ ModernTsf 路径 → ChsIME.exe / ChtIME.exe (OOP + MTF 接口)
```

`MSCTF.dll` 内部有 `usesLegacyImplementation` 字段，控制走哪条路径：
- 无 `{3AF314A2}` Category → `usesLegacyImplementation=true` → InProc（Win32 正常）
- 有 `{3AF314A2}` Category → `usesLegacyImplementation=false` → OOP/TSF3

#### 二、MSCTF.dll 硬编码白名单（关键发现）

**`MSCTF.dll` 内部 offset `0x146324` 存在一个硬编码的 IME 结构体数组**，每项 48 字节（`CLSID` + `LangProfileGUID` + `Flags`），完整列表如下：

| CLSID | 名称 | 类型 | 说明 |
|---|---|---|---|
| `{531FDEBF}` | CImeServerCht | LocalServer32 | 繁中 ChtIME.exe |
| `{B115690A}` | CImeServerCht | LocalServer32 | 繁中 ChtIME.exe（备用） |
| `{03B5835F}` | IMJPTIP | InProcServer32 | 日语 imjptip.dll |
| `{A028AE76}` | IMKRTIP | InProcServer32 | 韩语 imkrtip.dll |
| `{A1E2B86B}` | IMKROTIP | InProcServer32 | 韩语旧版 imkrotip.dll |
| `{81D4E9C9}` | CImeServerChs | LocalServer32 | 简中 ChsIME.exe |
| `{6A498709}` | CImeServerChs | LocalServer32 | 简中 ChsIME.exe（备用） |

**这个白名单只包含微软自家 IME，对第三方完全封闭。** 通过注册表添加 Weasel CLSID 到 `Tsf3Override` 无效，MSCTF 不读取白名单之外的项目。

与该数组配套，`MSCTF.dll` 同一段还硬编码了这些 IME 的字符串形式 CLSID（用于注册表查询），并在运行时将匹配结果写入 `HKCU\Software\Microsoft\Input\TSF\Tsf3Override\{CLSID}` 作为系统预置缓存。

#### 三、TSF3 / GIP 架构详解（逆向 tsf3gip.dll）

`tsf3gip.dll`（GIP = Global Input Processor）是连接 `TextInputHost.exe` 和具体 IME 的桥接层，内部有 `mtfadapter`（MTF = Modern Text Framework）。

**tsf3gip.dll 内部 IME 类型枚举：**
- `Imm` — 旧版 IMM32 IME
- `System` — 系统内置 WinRT IME（BopomofoIme、JapaneseIme，完全 WinRT 路径）
- `ClassicTsf` — 传统 InProc TSF IME（日文/韩文走此路径，在 TextInputHost 内 InProc 加载）
- `ModernTsf` — 现代 OOP TSF（ChsIME.exe/ChtIME.exe，WinRT 激活工厂 + MTF 接口）
- `Unknown`

**注册表辅助控制：**
- `HKCU\Software\Microsoft\Input\TSF\Tsf3Override\{CLSID}` — 系统写入，标记白名单内 IME 已激活 TSF3
- `HKLM\Software\Microsoft\Input\TSF\Tsf3Override` — 无（系统级只在 HKCU）
- `noTsf3Override` / `NoTsf3Override1~5` — 内部调试/策略标志

#### 四、ChsIME.exe 的 ModernTsf OOP 机制（逆向确认）

微软拼音（ChsIME.exe）不是简单的 `ITfTextInputProcessor` OOP，而是实现了完整的 WinRT 激活工厂 + MTF 接口：

**导入函数（dumpbin 验证）：**
- `RoRegisterActivationFactories` / `RoRevokeActivationFactories` — 注册 WinRT 工厂
- `CoAddRefServerProcess` / `CoReleaseServerProcess` / `CoResumeClassObjects` — 标准 OOP COM Server 生命周期
- `CoRegisterClassObject` / `CoRevokeClassObject` — COM 类注册
- `TF_GetShowFloatingStatus` / `TF_SetShowFloatingStatus` — 仅用这两个 MSCTF 函数，**不初始化 ThreadMgr**
- `RtlSubscribeWnfStateChangeNotification` — WNF（Windows Notification Facility）状态订阅

**关键字符串（strings 确认）：**
```
Windows.Desktop.TextInput.ChsIme   ← ChsIME 自己注册的 WinRT 激活工厂名
```

**工作流程：**
1. 系统以 `-Embedding` 参数启动 `ChsIME.exe`
2. `CoAddRefServerProcess()` → `CoRegisterClassObject()` → `CoResumeClassObjects()`
3. `RoRegisterActivationFactories("Windows.Desktop.TextInput.ChsIme", ...)` 注册 WinRT 工厂
4. `tsf3gip.dll` 的 `mtfadapter` 通过 WinRT 工厂激活 ChsIME，调用 MTF 接口
5. 输入结果通过 ALPC 回传给 UWP 应用

#### 五、第一版 OOP 实现失败原因（已排查）

| 失败点 | 原因 |
|---|---|
| Win32 记事本输入失效 | 注册 `{3AF314A2}` 后 MSCTF 全局切换到 OOP 路径，但我们的 `-Embedding` 进程未正确就绪 |
| `-Embedding` 进程无 MSCTF | `RunEmbedding` 只做了 `CoRegisterClassObject`，缺少 `CoAddRefServerProcess` + `CoResumeClassObjects` |
| UWP 仍无输入 | 即使 OOP Server 正确，Weasel CLSID 不在 MSCTF 硬编码白名单，TSF3 路径不会激活 Weasel |

**已修复：** `WeaselServer.cpp` 的 `RunEmbedding()` 已补全正确的 OOP COM Server 生命周期调用（`CoAddRefServerProcess` + `CoResumeClassObjects` + `CoReleaseServerProcess`）。`LocalServer32` 也已注册。`{3AF314A2}` Category 因白名单问题暂不启用。

#### 六、TextInputFramework.dll 的 ALPC 通信机制

`TextInputFramework.dll` 使用 ALPC（高级本地过程调用）端口在 UWP 进程和 `TextInputHost.exe` 之间传递输入事件：

- `TextInputServerCreate` / `TextInputServerCreateEx` — TextInputHost 侧（ALPC server）
- `TextInputClientCreate` / `TextInputClientCreate2` — UWP 应用侧（ALPC client）
- `TextInputHostGetForHwnd` — Win32 侧根据 HWND 获取 Host
- `TsfOneCreate` — TSF One 统一适配层

ALPC 端口名如 `Input\Core.AlpcPort\Server`、`Input\Public.AlpcPort\Server` 等。

#### 七、globinputhost.dll 的 IME 识别

`globinputhost.dll`（WGI = Windows Globalization Input）负责枚举和识别输入法，关键导出：
- `WGIIsImmersiveInputMethod(CLSID)` — 判断某个 IME 是否支持 UWP（Immersive）输入
- `WGIGetCompatibleInputMethodsForLanguage` — 获取语言可用的输入法列表
- `WGIGetDefaultInputMethodForLanguage` — 获取默认输入法

该函数的判断依据是 `CTF\TIP` 注册表中的 Language Profile 信息，**不单独检查 `{3AF314A2}`**，但最终激活时仍受 MSCTF 白名单约束。

#### 八、搜狗等第三方 IME 的 UWP 支持分析

> ⚠️ **对先前文档中"搜狗走 IMM32 路径"的更正**
>
> 之前基于旧版搜狗逆向得出的结论（IMM32 + CUAS 兼容层）不适用于现代版本的搜狗 TSF。以下为更准确的分析。

**当前结论（基于 MSCTF 白名单机制的推断）：**

1. **搜狗 CLSID 不在 MSCTF.dll 硬编码白名单中**（已通过字节搜索验证）
2. 将 Weasel CLSID 加入 `Tsf3Override` 注册表后，TextInputHost 仍未加载 weaselx64.dll（已实验验证），说明注册表可扩展，但激活仍受白名单约束
3. 第三方 IME 若要真正支持 AppContainer（便笺等），理论上需要绕过白名单，可能的方式：
   - **Runtime Hook/Patch**：在运行时 Hook MSCTF 的白名单检查函数（高风险，反病毒可能误报）
   - **只支持"伪 UWP"**：Win11 新版记事本等 MSIX 打包应用并非完整 AppContainer，仍可 InProc 加载 TSF DLL，第三方 IME 在此场景下可正常工作
   - **用微软内部渠道申请白名单**：理论上可行，实际无此公开流程

**实验验证：** 把 Weasel 加入 `Tsf3Override` + 注册 `{3AF314A2}` 后，便笺仍无法使用 Weasel，且 Win32 记事本输入中断（MSCTF 全局切换 OOP 路径但白名单激活失败）。`{3AF314A2}` 已回滚。

**结论：** 搜狗若声称支持"便笺"等真正的 AppContainer 应用，其机制尚未完全明确；若只支持 Win11 新记事本，则与 Weasel 当前能力相同（Win32 InProc）。这不是 Weasel 特有缺陷，而是微软对第三方 IME 的架构性封锁。

#### 九、OOP TSF 实现的可行性评估（当前状态）

**已完成：**
- ✅ `WeaselServer.exe` 注册了 `LocalServer32`（`Register.cpp`）
- ✅ `RunEmbedding()` 实现了正确的 OOP COM Server 生命周期
- ✅ `GUID_TFCAT_TIPCAP_LOCALSERVER32`（`{3AF314A2}`）和 `GUID_TFCAT_TIPCAP_CHINESE`（`{74769EE9}`）的 GUID 定义已添加（`Globals.h/cpp`）
- ✅ `{3AF314A2}` Category 在 `Register.cpp` 中预留（注释状态，随时可启用）

**阻塞点：**
- ❌ MSCTF.dll 硬编码白名单无法通过注册表绕过，需要更深入的方案（Hook 或其他）
- ❌ 即使绕过白名单，ChsIME 走的是 MTF（Modern Text Framework）+ WinRT 激活工厂路径，而非标准 `ITfTextInputProcessor` OOP，Weasel 尚未实现 MTF 接口

**下一步研究方向：**
1. 研究 `tsf3gip.dll` 中 `ClassicTsf` 路径的完整 COM 接口协议（日韩 IME 走此路径，可能不需要 MTF）
2. 确认 `ClassicTsf` 路径是否也受白名单约束，或白名单仅对 `ModernTsf` 有效
3. 评估 Detours/MinHook 方案在 MSCTF 白名单函数上的可行性

### 当前代码状态

| 文件 | 改动 | 状态 |
|---|---|---|
| `WeaselTSF/Register.cpp` | 添加 `LocalServer32` 注册；`{3AF314A2}` 在注释中预留 | ✅ 已部署 |
| `WeaselTSF/Globals.h/cpp` | 添加 `{3AF314A2}` 和 `{74769EE9}` GUID 定义 | ✅ 已部署 |
| `WeaselServer/WeaselServer.cpp` | `RunEmbedding()` 补全 `CoAddRefServerProcess` + `CoResumeClassObjects` | ✅ 已部署 |

### 状态

- **首次发现日期：** 2024年
- **深度逆向研究：** 2025年（TSF3/GIP/MSCTF 白名单）
- **Windows 版本：** Windows 11 23H2+（Build 22631+）
- **状态：** 阻塞于 MSCTF 硬编码白名单，待研究绕过方案
