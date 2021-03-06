#include "stdafx.h"

#include "MouseInput.h"

#include <Windows.h>

#include "AnvilRendering.h"
#include <chrono>

extern bool g_bBrowserShowing;

using clock_ = std::chrono::steady_clock;
static clock_::time_point select_timeout;
bool quick_selecting = false;

bool SendBeginQuickSelectTimeout(uint32_t timeout_ms);
void StartQuickSelectTimeout(uint32_t timeout_ms, bool from_remote)
{
	if (from_remote) {
		SendBeginQuickSelectTimeout(timeout_ms);
		return;
	}

	if (!GetHotKey(HOTKEY_Cancel))
		return;

	using namespace std;
	select_timeout = clock_::now() + chrono::milliseconds{ timeout_ms };
}

bool SendStopQuickSelect();
void StopQuickSelect(bool from_remote)
{
	if (from_remote) {
		SendStopQuickSelect();
		return;
	}

	select_timeout = {};
	quick_selecting = false;
}

bool QuickSelectTimeoutExpired()
{
	if (select_timeout >= clock_::now() || select_timeout == decltype(select_timeout){})
		return false;

	select_timeout = {};
	return true;
}

static bool StartQuickSelect()
{
	if (select_timeout < clock_::now())
		return false;

	quick_selecting = true;
	select_timeout = {};
	ForgeEvent::StartQuickSelect();
	return true;
}

bool UpdateMouseState(UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (!quick_selecting && msg == WM_MBUTTONDOWN && StartQuickSelect())
		return true;

	if (!g_bBrowserShowing && !quick_selecting)
		return false;

	if (quick_selecting && !g_bBrowserShowing) {
		POINT pt = { 0, 0 };
		ClientToScreen(g_Proc.m_Stats.m_hWndCap, &pt);
		lParam = POINTTOPOINTS(pt);
	}

	switch (msg)
	{
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
	case WM_MOUSEWHEEL:
		if (g_bBrowserShowing || quick_selecting) {
			ForgeEvent::MouseEvent(msg, wParam, lParam);
			return true;
		}
		break;

	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_XBUTTONDBLCLK:
		if (g_bBrowserShowing) {
			ForgeEvent::MouseEvent(msg, wParam, lParam);
			return true;
		}
	}

	return false;
}

void QueryRawInputDevices(std::vector<RAWINPUTDEVICE> &devices);
static std::vector<RAWINPUTDEVICE> devices;
void UpdateRawMouse(RAWMOUSE &event)
{
	auto middle_mouse_pressed = !!(event.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN);
	auto middle_mouse_released = !!(event.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP);

	if (!g_bBrowserShowing && !quick_selecting) {
		if (!(middle_mouse_pressed && StartQuickSelect()))
			return;
	}

	if (!g_bBrowserShowing && quick_selecting) {
		QueryRawInputDevices(devices);
		auto send_message = false;
		for (auto &dev : devices) {
			if (dev.usUsagePage == 1 && dev.usUsage == 2 && dev.dwFlags & RIDEV_NOLEGACY) {
				send_message = true;
				break;
			}
		}

		if (send_message) {
			if (middle_mouse_pressed || middle_mouse_released)
				UpdateMouseState(middle_mouse_pressed ? WM_MBUTTONDOWN : WM_MBUTTONUP, 0, 0);
			if (event.usButtonFlags & RI_MOUSE_WHEEL)
				UpdateMouseState(WM_MOUSEWHEEL, event.usButtonData << 16, 0);
		}

		event.usButtonFlags &= ~(RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_WHEEL);
		event.usButtonData = 0;
		return;
	}

	ZeroMemory(&event, sizeof(event));
}
