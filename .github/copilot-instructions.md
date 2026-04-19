# Copilot Instructions

## 项目指南

### 编译
- 使用 VS2019 的 MSBuild，路径：`"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"`
- 解决方案：`E:\CPP\weasel\weasel.sln`，平台：`x64 Release`，工具集：`/p:PlatformToolset=v142`
- 编译命令模板：
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" "E:\CPP\weasel\weasel.sln" /t:<目标> /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142 /v:minimal
  ```
- UI 改动编译顺序：`/t:WeaselUI` → `/t:WeaselTSF`
- TSF 改动只需编译：`/t:WeaselTSF`
- IPC 改动需额外编译：`/t:WeaselServer`
- 编译输出目录：`E:\CPP\weasel\output\`

### 部署
- 部署目录：`D:\Rime\weasel-0.17.4\`
- 不要强制杀死系统进程（explorer、安全软件等）
- 部署步骤：
  1. 停止 WeaselServer：`Stop-Process -Name "WeaselServer" -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1`
  2. 用时间戳备份旧 DLL：`[System.IO.File]::Move("$dest\weaselx64.dll", "$dest\weaselx64.dll.bak_$(Get-Date -Format 'yyyyMMdd_HHmmss')")`
  3. 复制新 DLL：`Copy-Item "E:\CPP\weasel\output\weaselx64.dll" "$dest\weaselx64.dll" -Force`
  4. 重启 WeaselServer：`Start-Process "$dest\WeaselServer.exe"`
  5. 重启测试应用（如果不记事本或者便笺，可直接重启）