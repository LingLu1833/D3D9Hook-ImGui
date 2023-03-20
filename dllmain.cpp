#include <Windows.h>
#include <iostream>
#include <d3d9.h>

#include "detours.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "baidu_font.hpp"

LPDIRECT3D9				g_pD3D = NULL;
LPDIRECT3DDEVICE9       g_pd3dDevice = NULL;

typedef HRESULT(__stdcall* tReset)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);	// 16
typedef HRESULT(__stdcall* tEndScene)(LPDIRECT3DDEVICE9);						// 42
typedef LRESULT(__stdcall* tWndProc)(HWND, UINT, WPARAM, LPARAM);

tReset		g_oReset;
tEndScene	g_oEndScene;
tWndProc	g_oWndProc;

HWND g_hWnd = NULL;
DWORD* g_dDeviceVT = NULL;

DWORD WINAPI mainThread(PVOID);

bool InitD3D9Device();

void HookReset();
void HookEndScene();

HRESULT __stdcall HookResetCallback(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
HRESULT __stdcall HookEndSceneCallback(LPDIRECT3DDEVICE9);
LRESULT __stdcall HookWndProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

BOOL CALLBACK EnumWindowsCallback(HWND, LPARAM);
HWND GetProcessWindow();

void HookFunction(PVOID*, PVOID);
void UnHookFunction(PVOID*, PVOID);

void CleanupDevice();

void ImGuiInit(LPDIRECT3DDEVICE9);
void ImGuiShowMenu();

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)mainThread, NULL, NULL, NULL);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

DWORD WINAPI mainThread(PVOID base)
{
	if (InitD3D9Device())
	{
		HookReset();
		HookEndScene();
	}

	return 0;
}

bool InitD3D9Device()
{
	g_hWnd = GetProcessWindow();
	if (!g_hWnd)
	{
		return false;
	}

	g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!g_pD3D)
	{
		return false;
	}

	D3DPRESENT_PARAMETERS d3dpp = {};
	SecureZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	d3dpp.EnableAutoDepthStencil = TRUE;
	d3dpp.AutoDepthStencilFormat = D3DFMT_D16;

	HRESULT result = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);
	if (FAILED(result))
	{
		CleanupDevice();
		return false;
	}

	if (!g_pd3dDevice)
	{
		CleanupDevice();
		return false;
	}

	g_dDeviceVT = (DWORD*)*(DWORD*)g_pd3dDevice;
	if (!g_dDeviceVT)
	{
		CleanupDevice();
		return false;
	}

	g_pD3D->Release();

	return true;
}

void HookReset()
{
	if (!g_dDeviceVT)
	{
		return;
	}

	g_oReset = (tReset)g_dDeviceVT[16];
	HookFunction(&(PVOID&)g_oReset, HookResetCallback);
	return;
}

void HookEndScene()
{
	if (!g_dDeviceVT)
	{
		return;
	}

	g_oEndScene = (tEndScene)g_dDeviceVT[42];
	HookFunction(&(PVOID&)g_oEndScene, HookEndSceneCallback);
	return;
}

HRESULT __stdcall HookResetCallback(LPDIRECT3DDEVICE9 pd3dDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	UnHookFunction(&(PVOID&)g_oReset, HookResetCallback);
	ImGui_ImplDX9_InvalidateDeviceObjects();
	HRESULT result = pd3dDevice->Reset(pPresentationParameters);
	ImGui_ImplDX9_CreateDeviceObjects();
	HookFunction(&(PVOID&)g_oReset, HookResetCallback);
	return result;
}

HRESULT __stdcall HookEndSceneCallback(LPDIRECT3DDEVICE9 pd3dDevice)
{
	UnHookFunction(&(PVOID&)g_oEndScene, HookEndSceneCallback);

	static bool init_end_scene = false;
	if (!init_end_scene)
	{
		ImGuiInit(pd3dDevice);
		g_oWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWL_WNDPROC, (LONG_PTR)HookWndProcCallback);
		init_end_scene = true;
	}

	ImGuiShowMenu();

	HRESULT result = pd3dDevice->EndScene();
	HookFunction(&(PVOID&)g_oEndScene, HookEndSceneCallback);
	return result;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall HookWndProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}
	return CallWindowProc(g_oWndProc, hWnd, msg, wParam, lParam);
}

BOOL CALLBACK EnumWindowsCallback(HWND hWnd, LPARAM lParam)
{
	DWORD wndProcID;
	GetWindowThreadProcessId(hWnd, &wndProcID);

	if (GetCurrentProcessId() != wndProcID)
	{
		return TRUE;
	}

	g_hWnd = hWnd;
	return FALSE;
}

HWND GetProcessWindow()
{
	EnumWindows(EnumWindowsCallback, NULL);
	return g_hWnd;
}

void HookFunction(PVOID* oFunction, PVOID nFunction)
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(oFunction, nFunction);
	DetourTransactionCommit();
}

void UnHookFunction(PVOID* oFunction, PVOID nFunction)
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourDetach(oFunction, nFunction);
	DetourTransactionCommit();
}

void CleanupDevice()
{
	if (g_pD3D)
	{
		g_pD3D->Release();
		g_pD3D = NULL;
	}

	if (g_pd3dDevice)
	{
		g_pd3dDevice->Release();
		g_pd3dDevice = NULL;
	}
}

void ImGuiInit(LPDIRECT3DDEVICE9 pd3dDevice)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	io.WantSaveIniSettings = false;
	io.IniFilename = NULL;

	ImGui::StyleColorsDark();

	ImFontConfig f_cfg;
	f_cfg.FontDataOwnedByAtlas = false;
	const ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)baidu_font_data, baidu_font_size, 14.0f, &f_cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

	ImGui_ImplWin32_Init(g_hWnd);
	ImGui_ImplDX9_Init(pd3dDevice);
}

void ImGuiShowMenu()
{
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(200, 400), ImGuiCond_FirstUseEver);

	if (ImGui::Begin(u8"菜单"))
	{
		ImGui::Text("D3D9 Hook.");
	}
	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}
