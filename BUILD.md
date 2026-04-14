# Weasel 构建与部署指南

## 构建环境
- **IDE**: Visual Studio 2019 Community (v142 工具集)
- **解决方案**: `E:\CPP\weasel\weasel.sln`
- **平台**: x64 Release
- **MSBuild 路径**: `"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"`
- **Boost**: V:\boost\1_78_0

## 编译命令

### 编译 WeaselUI（静态库，UI 渲染相关改动）
```powershell
cd E:\CPP\weasel
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" weasel.sln /t:WeaselUI /p:Configuration=Release /p:Platform=x64 /v:m
```

### 编译 WeaselTSF（DLL，依赖 WeaselUI）
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /v:m
```

### 编译 WeaselServer（服务端，IPC 数据结构改动时需要）
```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" weasel.sln /t:WeaselServer /p:Configuration=Release /p:Platform=x64 /v:m
```

### 常见编译顺序
- **仅修改 UI 渲染代码**（WeaselPanel.cpp 等）：WeaselUI → WeaselTSF
- **修改 IPC 数据结构**（WeaselIPCData.h）：WeaselUI → WeaselTSF + WeaselServer
- **修改 Rime 集成代码**（RimeWithWeasel.cpp）：WeaselServer

## 输出文件
- DLL: `E:\CPP\weasel\output\weaselx64.dll`
- Server: `E:\CPP\weasel\output\WeaselServer.exe`

## 部署（安装目录）
目标路径: `D:\Rime\weasel-0.17.4\`

### 部署 DLL
```powershell
Stop-Process -Name WeaselServer -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
$target = "D:\Rime\weasel-0.17.4\weaselx64.dll"
$bak = $target + ".bak" + (Get-Date -Format "HHmmss")
if (Test-Path $target) { Move-Item $target $bak -Force }
Copy-Item ".\output\weaselx64.dll" $target -Force
Start-Process "D:\Rime\weasel-0.17.4\WeaselServer.exe"
```

### 部署 Server
```powershell
Stop-Process -Name WeaselServer -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
$target = "D:\Rime\weasel-0.17.4\WeaselServer.exe"
$bak = $target + ".bak" + (Get-Date -Format "HHmmss")
if (Test-Path $target) { Move-Item $target $bak -Force }
Copy-Item ".\output\WeaselServer.exe" $target -Force
Start-Process "D:\Rime\weasel-0.17.4\WeaselServer.exe"
```

## 注意事项

### 部署安全
- **绝对不要**强制杀死系统进程（explorer、安全软件等），只停止 WeaselServer
- 使用 Move + Copy 方式替换文件（先重命名旧文件再复制新文件）
- 部署后重启 WeaselServer，让用户自行重启测试应用

### IPC 兼容性
- 修改 `WeaselIPCData.h` 中的结构体字段时，**必须同时重新编译并部署** WeaselTSF (DLL) 和 WeaselServer (EXE)
- 如果只部署其中一个，会导致 IPC 数据结构不匹配，输入法无法正常工作
- 新增字段应添加到结构体末尾，避免破坏已有字段偏移

### 渲染管线
DoPaint 绘制顺序：
1. GDI+ 背景绘制（background, gradient, preedit_back_color）
2. `_DrawPreeditBack` — 高亮背景
3. `_DrawCandidates(back=true)` — 候选项背景
4. DirectWrite `BeginDraw` → `_DrawPreedit`（文本+翻页按钮）→ `_DrawCandidates`（文本）→ `EndDraw`
5. GDI+ 后处理（cursor line, separator line, vertical line）
6. `UpdateLayeredWindow`

### Rime 配置
- 用户配置目录: `C:\Users\Administrator\AppData\Roaming\Rime\`
- 当前方案: `luna_pinyin_simp`
- 主题: `sogou_light`（在 weasel.custom.yaml 中定义）
