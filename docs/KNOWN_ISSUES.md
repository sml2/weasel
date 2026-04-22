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

### 根本原因（最终确认，2025年）

**`weaselx64.dll` 所在目录缺少 `ALL APPLICATION PACKAGES` 读取权限，导致 AppContainer 进程无法加载 DLL。**

AppContainer 进程（UWP 应用）在加载 TSF DLL 时，操作系统以 AppContainer 安全令牌访问文件。若目标 DLL 所在路径没有 `ALL APPLICATION PACKAGES (S-1-15-2-1)` 的读取/执行 ACL，`LoadLibrary` 会静默失败，DLL 完全不被注入。

**`C:\Windows\System32`** 目录天生继承 `ALL APPLICATION PACKAGES:(OI)(CI)(RX)` 权限，搜狗等安装到 System32 的 IME 自动可用。**`D:\Rime\weasel-0.17.4\`** 等非系统路径默认无此权限，导致 Weasel 在 UWP 进程中不可见。

**验证：**

```powershell
# 修复前：D:\Rime 无 AppContainer 权限
icacls "D:\Rime\weasel-0.17.4\weaselx64.dll"
# → 只有 Administrators/SYSTEM/Users，无 APPLICATION PACKAGES

# 修复方法：添加权限
icacls "D:\Rime\weasel-0.17.4" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T

# 修复后：Microsoft.Notes.exe 进程模块列表出现 weaselx64.dll ✅
# TSF ActivateEx: flags=0x40000000 (TF_TMF_IMMERSIVEMODE), isImmersive=1, proc=Microsoft.Notes.exe ✅
# 标准 TSF OnKeyDown 正常触发，中文输入工作 ✅
```

**真正的架构（调试日志确认）：**

```
Microsoft.Notes.exe (AppContainer UWP 进程)
  └─ msctf.dll → InProc 注入 weaselx64.dll（权限修复后）
       └─ ActivateEx(flags=TF_TMF_IMMERSIVEMODE) → OnKeyDown → IPC → WeaselServer ✅
```

MSCTF 会直接将 TSF DLL 注入 UWP 进程本身（不是 ApplicationFrameHost），TSF 标准路径完全正常工作。之前调查中"DLL 未被加载"的现象，完全是 ACL 权限问题，与 AppContainer 沙箱设计、白名单机制无关。

**注：开发部署时 CLSID 注册表指向 `D:\Rime\...`，但正式安装后 `weaselx64.dll` 会被复制到 System32 并重命名为 `weasel.dll`（icacls 自动继承），所以正式安装用户不受此问题影响。仅影响直接覆盖部署的开发模式。**

**解决方案：** 在 `weaselsetup.exe` 的安装逻辑中，对安装目录执行一次 `icacls /grant *S-1-15-2-1:(OI)(CI)(RX) /T`，或确保 CLSID 注册表 InProcServer32 指向 System32 副本（`weasel.dll`）。

### 解决方案

在 `weaselsetup.exe` 安装逻辑中对安装目录添加 AppContainer 读取权限（见上文）。开发模式下手动执行：

```powershell
icacls "D:\Rime\weasel-0.17.4" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T
```

### 调查历史（附录）

以下为调查过程中产生的各种假设和逆向分析记录，已被最终结论取代，仅供参考。

#### 早期错误假设：MSCTF 白名单机制

早期调查认为"AppContainer 只信任 System32 的 TSF 组件"，并逆向了 `MSCTF.dll` 的硬编码 IME 白名单数组（offset `0x146324`）：

| CLSID | 名称 | 类型 |
|---|---|---|
| `{531FDEBF}` / `{B115690A}` | CImeServerCht | LocalServer32（繁中） |
| `{03B5835F}` | IMJPTIP | InProcServer32（日语） |
| `{A028AE76}` / `{A1E2B86B}` | IMKRTIP/IMKROTIP | InProcServer32（韩语） |
| `{81D4E9C9}` / `{6A498709}` | CImeServerChs | LocalServer32（简中） |

该白名单对 `TextInputHost.exe` 内部的 TSF3/GIP 路径（`ModernTsf`/`ClassicTsf`）有效，但与 UWP 进程直接 InProc 加载 TSF DLL 无关。**Weasel 不工作的真实原因是 ACL，不是白名单。**

#### TSF3/GIP/OOP 架构研究（已确认，对 UWP 直接注入路径无关）

Windows UWP 输入存在两条路径：

```
【InProc TSF — Weasel 实际走此路径】
UWP 进程 (Microsoft.Notes.exe)
  └─ msctf.dll → InProc 加载 weaselx64.dll（需 ACL 权限）
       └─ ActivateEx(TF_TMF_IMMERSIVEMODE) → OnKeyDown → IPC → WeaselServer ✅

【TSF3/GIP — 微软自家 IME 专用】
UWP 进程
  └─ TextInputFramework.dll →[ALPC]→ TextInputHost.exe
       └─ tsf3gip.dll (mtfadapter)
            ├─ ClassicTsf：日韩 IME InProc 加载（TextInputHost 内）
            └─ ModernTsf：ChsIME.exe/ChtIME.exe（WinRT 工厂 + MTF 接口）
```

`ChsIME.exe` 的 ModernTsf OOP 机制（`RoRegisterActivationFactories` + WinRT 工厂 `Windows.Desktop.TextInput.ChsIme`）与 Weasel 无关，Weasel 走标准 InProc TSF 路径即可。

#### 搜狗 UWP 支持机制（逆向分析，已确认）

VM（WinDev2407Eval）搜狗拼音 12.2.0.6275 在便笺中可用的原因：
- `SogouTSF.ime` 安装在 `C:\Windows\System32`，天生有 AppContainer ACL
- MSCTF 将其注入 `Microsoft.Notes.exe`（UWP 进程本身），标准 TSF OnKeyDown 正常触发
- **与 AFH/CUAS/IMM32 无关**（早期逆向结论有误，已更正）

#### OOP TSF 实现（已实现，非 UWP 必需）

| 文件 | 改动 |
|---|---|
| `WeaselTSF/Register.cpp` | 添加 `LocalServer32` 注册；`{3AF314A2}` 预留注释 |
| `WeaselTSF/Globals.h/cpp` | 添加 `{3AF314A2}` / `{74769EE9}` GUID 定义 |
| `WeaselServer/WeaselServer.cpp` | `RunEmbedding()` 补全 OOP COM Server 生命周期 |

---

## WeaselIME IMM32 模块——恢复与 Win11 部署阻塞

### 背景

Weasel 历史上曾有 `WeaselIME` 模块，实现 IMM32（`.ime`）路径，与 TSF 路径并行，允许以传统输入法方式（类似 Windows 95/XP 时代）注册和激活。该模块在 commit `d636e0a`（PR #1775，标题"drop WeaselIME"）中被整体删除。

### WeaselIME 恢复过程

2025年，从 `d636e0a^`（删除前一个 commit）的 git 历史中完整恢复了 `WeaselIME` 16个源文件（commit `398a655`），并同步恢复了 `RimeWithWeasel` 中配套的 IMM32 支持逻辑（commit `e211a38`，反向了 `74eb272` 的部分删除）：

- `RimeWithWeasel.cpp/h`：恢复 `_IsSessionTSF()`、`client_type`（`ime`/`tsf`）解析、IMM32 路径下的 `_GetContext()` 调用、`inline_preedit` 判断、`Show()`/`Hide()` 控制逻辑

### 部署排查过程

#### 1. VERSION 资源错误（GetFileVersionInfoSize 返回 0，错误码 1813）

**原因：** `WeaselIME.rc` 中使用了 `VS_VERSION_INFO VERSIONINFO` 语法，但 `resource.h` 未 `#include <winresrc.h>`，导致 `VS_VERSION_INFO` 未被定义为整数 `1`，资源名被当作字符串处理，`GetFileVersionInfoSize` 按整数 ID `1` 查找失败。

**修复：** 将 `VS_VERSION_INFO VERSIONINFO` 改为 `1 VERSIONINFO`，版本号硬编码为 `0,17,4,0`，去掉宏依赖。

#### 2. 注册表字段名

IMM32 系统期望的注册表字段名是 `Ime File`（有空格），而非 `IME File`。KLID 范围从 `E0200000` 开始，格式为 `E02X0804`。`Layout Id` 字段需同步设置（4 位十六进制，如 `0E24`）。

#### 3. LoadKeyboardLayout 返回 1419（ERROR_INVALID_KEYBOARD_HANDLE）——核心阻塞点

将 `weasel.ime` 部署到 `System32`（64位）和 `SysWOW64`（32位），注册表项配置完整后，`LoadKeyboardLayout("E0240804", 0)` 在 Windows 10/11 上返回错误码 `1419`。

**根因：Windows 10/11 对第三方 `.ime` 文件要求 WHQL 代码签名。** 未签名的 `.ime` 在 `LoadKeyboardLayout` 阶段被系统拒绝，错误码 `1419`，无任何绕过方式。

测试验证：
- `GetFileVersionInfoSize` 修复后正常返回 `1292` ✅
- `FileVersionInfo.FileVersion = "0.17.4.0"` ✅
- `LoadLibrary("weasel.ime")` 成功 ✅
- `LoadKeyboardLayout("E0240804", 0)` → `1419` ❌（签名阻塞）

#### 4. RegisterUIClass ERROR_CLASS_ALREADY_EXISTS

`WeaselIME.cpp` 的 `RegisterUIClass()` 调用 `RegisterClassExW` 时，若窗口类已注册则返回 `ERROR_CLASS_ALREADY_EXISTS`（183），原代码将此视为致命错误。已修复为容忍此错误（非重复注册场景的正常情况）。

### WeaselIME 被删除的真实原因（git 历史分析）

通过分析 `d636e0a` 的 diff 和相关代码：

1. **Win10/11 签名要求**：`LoadKeyboardLayout` 对未签名第三方 `.ime` 返回 `1419`，安装程序自身有 `register_ime()` 函数但走的是注册表直接写入（依赖重启生效），无法实时激活，用户体验差。

2. **old_ime_support 实质废弃**：`WeaselSetup.cpp` 中命令行安装路径硬编码 `old_ime_support = false`，只有 GUI 安装才有 checkbox 可选，说明 IMM32 路径删除前已实质废弃。

3. **架构演进**：Weasel 主线已全面向 TSF 迁移；`imesetup.cpp` 删除注释写明 `// register_ime (IMM/.ime) support removed — TSF-only build`。

4. **ARM64 维护复杂度**：删除时同步移除了 `weaselARM64X.ime` 构建，ARM64X 包装层维护成本高。

### 代码修复汇总

| 文件 | 改动 | 状态 |
|---|---|---|
| `WeaselIME/WeaselIME.rc` | `1 VERSIONINFO`，硬编码版本 `0,17,4,0`，去宏依赖 | ✅ 已修复 |
| `WeaselIME/WeaselIME.cpp` | `RegisterUIClass` 容忍 `ERROR_CLASS_ALREADY_EXISTS` | ✅ 已修复 |
| `RimeWithWeasel/RimeWithWeasel.cpp` | 恢复 `_IsSessionTSF()`、`client_type`、IMM32 路径 UI 控制 | ✅ 已恢复（commit `e211a38`） |
| `include/RimeWithWeasel.h` | 恢复 `_IsSessionTSF()` 声明 | ✅ 已恢复 |

### 当前状态

- **代码**：`WeaselIME` 模块代码已完整恢复并修复编译问题，Win32 + x64 双平台编译通过
- **Win11 激活**：无 WHQL 签名无法通过 `LoadKeyboardLayout` 激活，阻塞于系统签名校验
- **Win32 应用**：TSF 路径（`weaselx64.dll`）在所有 Win32 应用中正常工作，无需 IMM32 路径
- **UWP 应用**：IMM32 路径对 UWP/AppContainer 无帮助（UWP 同样不会加载 `.ime`），根因仍是 MSCTF 白名单问题

### 状态

- **探究日期：** 2025年
- **Windows 版本：** Windows 11 23H2+（Build 22631+）
- **状态：** 代码已恢复，Win11 部署阻塞于 WHQL 代码签名要求，IMM32 路径对 UWP 问题无实质帮助
