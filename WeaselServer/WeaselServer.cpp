// WeaselServer.cpp : main source file for WeaselServer.exe
//
//	WTL MessageLoop 封装了消息循环. 实现了 getmessage/dispatchmessage....

#include "stdafx.h"
#include "resource.h"
#include "WeaselService.h"
#include <WeaselIPC.h>
#include <WeaselUI.h>
#include <RimeWithWeasel.h>
#include <WeaselUtility.h>
#include <winsparkle.h>
#include <functional>
#include <ShellScalingApi.h>
#include <WinUser.h>
#include <memory>
#include <atlstr.h>
#pragma comment(lib, "Shcore.lib")
CAppModule _Module;

// OOP TSF: WeaselServer.exe 作为 LocalServer32 OOP COM Server 运行
// 当系统以 -Embedding 参数启动时进入此模式（AppContainer/UWP 支持）
static int RunEmbedding() {
  // 获取 WeaselServer.exe 所在目录，加载同目录的 weaselx64.dll
  WCHAR szPath[MAX_PATH];
  GetModuleFileNameW(NULL, szPath, MAX_PATH);
  WCHAR* pSlash = wcsrchr(szPath, L'\\');
  if (!pSlash)
    return -1;

#ifdef _M_ARM64
  wcscpy_s(pSlash + 1, MAX_PATH - (pSlash + 1 - szPath), L"weaselARM64.dll");
#else
  wcscpy_s(pSlash + 1, MAX_PATH - (pSlash + 1 - szPath), L"weaselx64.dll");
#endif

  HMODULE hDll = LoadLibraryW(szPath);
  if (!hDll)
    return -1;

  typedef HRESULT(STDAPICALLTYPE * FnDllGetClassObject)(REFCLSID, REFIID,
                                                        void**);
  auto fnGetClassObject =
      (FnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
  if (!fnGetClassObject) {
    FreeLibrary(hDll);
    return -1;
  }

  // Weasel TSF CLSID: {A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}
  static const CLSID clsidWeaselTSF = {
      0xa3f4cded,
      0xb1e9,
      0x41ee,
      {0x9c, 0xa6, 0x7b, 0x4d, 0x0d, 0xe6, 0xcb, 0x0a}};

  IClassFactory* pFactory = nullptr;
  HRESULT hr =
      fnGetClassObject(clsidWeaselTSF, IID_IClassFactory, (void**)&pFactory);
  if (FAILED(hr) || !pFactory) {
    FreeLibrary(hDll);
    return -1;
  }

  DWORD dwRegToken = 0;
  hr = CoRegisterClassObject(clsidWeaselTSF, pFactory,
                             CLSCTX_LOCAL_SERVER,
                             REGCLS_MULTIPLEUSE, &dwRegToken);
  pFactory->Release();

  if (FAILED(hr)) {
    FreeLibrary(hDll);
    return -1;
  }

  // 消息循环：等待 COM 调用完成后系统通知退出
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  CoRevokeClassObject(dwRegToken);
  FreeLibrary(hDll);
  return 0;
}

int WINAPI _tWinMain(HINSTANCE hInstance,
                     HINSTANCE /*hPrevInstance*/,
                     LPTSTR lpstrCmdLine,
                     int nCmdShow) {
  LANGID langId = get_language_id();
  SetThreadUILanguage(langId);
  SetThreadLocale(langId);

  if (!IsWindowsBlueOrLaterEx()) {
    CString info, cap;
    info.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING);
    cap.LoadStringW(IDS_STR_SYSTEM_VERSION_WARNING_CAPTION);
    MessageBoxExW(NULL, info, cap, MB_ICONERROR, langId);
    return 0;
  }
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  // 防止服务进程开启输入法
  ImmDisableIME(-1);

  WCHAR user_name[20] = {0};
  DWORD size = _countof(user_name);
  GetUserName(user_name, &size);
  if (!_wcsicmp(user_name, L"SYSTEM")) {
    return 1;
  }

  HRESULT hRes = ::CoInitialize(NULL);
  // If you are running on NT 4.0 or higher you can use the following call
  // instead to make the EXE free threaded. This means that calls come in on a
  // random RPC thread.
  // HRESULT hRes = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
  ATLASSERT(SUCCEEDED(hRes));

  // this resolves ATL window thunking problem when Microsoft Layer for Unicode
  // (MSLU) is used
  ::DefWindowProc(NULL, 0, 0, 0L);

  AtlInitCommonControls(
      ICC_BAR_CLASSES);  // add flags to support other controls

  hRes = _Module.Init(NULL, hInstance);
  ATLASSERT(SUCCEEDED(hRes));

  // OOP TSF: COM 以 -Embedding 参数启动时，作为 LocalServer32 OOP COM Server
  if (!wcscmp(L"-Embedding", lpstrCmdLine) ||
      !wcscmp(L"/Embedding", lpstrCmdLine)) {
    int nRet = RunEmbedding();
    _Module.Term();
    ::CoUninitialize();
    return nRet;
  }

  if (!wcscmp(L"/userdir", lpstrCmdLine)) {
    CreateDirectory(WeaselUserDataPath().c_str(), NULL);
    WeaselServerApp::explore(WeaselUserDataPath());
    return 0;
  }
  if (!wcscmp(L"/weaseldir", lpstrCmdLine)) {
    WeaselServerApp::explore(WeaselServerApp::install_dir());
    return 0;
  }
  if (!wcscmp(L"/ascii", lpstrCmdLine) || !wcscmp(L"/nascii", lpstrCmdLine)) {
    weasel::Client client;
    bool ascii = !wcscmp(L"/ascii", lpstrCmdLine);
    if (client.Connect())  // try to connect to running server
    {
      if (ascii)
        client.TrayCommand(ID_WEASELTRAY_ENABLE_ASCII);
      else
        client.TrayCommand(ID_WEASELTRAY_DISABLE_ASCII);
    }
    return 0;
  }

  // command line option /q stops the running server
  bool quit = !wcscmp(L"/q", lpstrCmdLine) || !wcscmp(L"/quit", lpstrCmdLine);
  // restart if already running
  {
    weasel::Client client;
    if (client.Connect())  // try to connect to running server
    {
      client.ShutdownServer();
      if (quit)
        return 0;
      int retry = 0;
      while (client.Connect() && retry < 10) {
        client.ShutdownServer();
        retry++;
        Sleep(50);
      }
      if (retry >= 10)
        return 0;
    } else if (quit)
      return 0;
  }

  bool check_updates = !wcscmp(L"/update", lpstrCmdLine);
  if (check_updates) {
    WeaselServerApp::check_update();
  }

  CreateDirectory(WeaselUserDataPath().c_str(), NULL);

  int nRet = 0;
  try {
    WeaselServerApp app;
    RegisterApplicationRestart(NULL, 0);
    nRet = app.Run();
  } catch (...) {
    // bad luck...
    nRet = -1;
  }

  _Module.Term();
  ::CoUninitialize();

  return nRet;
}
