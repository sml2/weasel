# Copilot Instructions

## 项目指南
- 部署 Weasel DLL 时不要强制杀死系统进程（explorer、安全软件等）。应该只停止 WeaselServer，然后用 Move+Copy 方式替换 DLL（先重命名旧文件为 .bak 再复制新文件），最后重启 WeaselServer，让用户自行重启测试应用。
- Weasel 项目编译方式：使用 VS2019 的 MSBuild（路径 "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"），解决方案 weasel.sln，平台 x64 Release。
- UI 改动编译顺序：/t:WeaselUI → /t:WeaselTSF。
- IPC 改动需额外编译 /t:WeaselServer。
- 部署目录 D:\Rime\weasel-0.17.4\。