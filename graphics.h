#pragma once
#include <d3d9.h>
class Graphics
{
public:
	Graphics(HWND, LPDIRECT3DDEVICE9);
	~Graphics();

	void SetShowMenu(bool);

	void Begin();
	void End();

	void ShowMenu();

private:
	void InitImGui();

private:
	HWND m_hWnd;
	LPDIRECT3DDEVICE9 m_pd3dDevice;
	bool m_showMenu = false;
};

