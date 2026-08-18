#include "stdafx.h"
#include "./external/inih/INIReader.h"
#include "./helper.hpp"
#include <stdio.h>

bool Proxy_Attach();
void Proxy_Detach();

using namespace std;

HMODULE baseModule = GetModuleHandle(NULL);

// Ini variables
bool bRemoveResolutionLimit;
bool bFixUI;
bool bCrashFix;
int iFPSCap;
bool bFPSCap;
bool bMovieFix;
int iShadowQuality;
bool bShadowQuality;
bool bColourFilter;
bool bFOVAdjust;
bool bBorderlessWindowed;
float fFOVAdjust;
int iCustomResX;
int iCustomResY;

// Variables
float fDesktopRight;
float fDesktopBottom;
float fDesktopAspect;
float fNativeAspect = 1.777777791f;
float fCustomAspect;

DWORD FPSCapReturnJMP;
float FPSCapValue;
void __declspec(naked) FPSCap_CC()
{
	__asm
	{
		divss xmm1, [FPSCapValue]
		movaps xmm0, xmm1
		jmp [FPSCapReturnJMP]
	}
}

DWORD MovieFixReturnJMP;
float MovieFixValue1;
float MovieFixValue2;
void __declspec(naked) MovieFix_CC()
{
	__asm
	{
		subss xmm1, xmm2
		addss xmm0, xmm2
		movss xmm0, [MovieFixValue1]
		movss xmm3, [MovieFixValue2]
		jmp [MovieFixReturnJMP]
	}
}

DWORD ShadowQualityReturnJMP;
int ShadowQualityValue;
void __declspec(naked) ShadowQuality_CC()
{
	__asm
	{
		mov eax, [ShadowQualityValue]
		add eax, 15
		push esi
		mov esi, ecx
		and eax, -16
		jmp [ShadowQualityReturnJMP]
	}
}

DWORD FOV1ReturnJMP;
void __declspec(naked) FOV1_CC()
{
	__asm
	{
		movss xmm3, [ecx + 0x24]
		addss xmm3, [fFOVAdjust]
		mulss xmm3, xmm1
		movss xmm1, [edx + 0x24]
		addss xmm1, [fFOVAdjust]
		jmp [FOV1ReturnJMP]
	}
}

DWORD FOV2ReturnJMP;
void __declspec(naked) FOV2_CC()
{
	__asm
	{
		movss xmm3, [ecx + 0x24]
		addss xmm3, [fFOVAdjust]
		mulss xmm3, xmm1
		movss xmm1, [edx + 0x24]
		addss xmm1, [fFOVAdjust]
		jmp [FOV2ReturnJMP]
	}
}


DWORD FOV3ReturnJMP;
void __declspec(naked) FOV3_CC()
{
	__asm
	{
		fld dword ptr[ecx + 0x24]
		fadd [fFOVAdjust]
		mov ecx, [ebp + 0x14]
		fstp dword ptr[ecx]
		jmp [FOV3ReturnJMP]
	}
}


DWORD FOV4ReturnJMP;
void __declspec(naked) FOV4_CC()
{
	__asm
	{
		fstp dword ptr [edi + 0x08]
		fld dword ptr [eax + 0x24]
		fadd [fFOVAdjust]
		fstp dword ptr [edx]
		jmp [FOV4ReturnJMP]
	}
}

DWORD FOV5ReturnJMP;
void __declspec(naked) FOV5_CC()
{
	__asm
	{
		fstp dword ptr [edi + 0x08]
		fld dword ptr [edx + 0x24]
		fadd [fFOVAdjust]
		fstp dword ptr [eax]
		jmp [FOV5ReturnJMP]
	}
}

struct WindowSearchResult
{
	HWND hWnd;
	LONG area;
};

BOOL CALLBACK FindProcessWindow(HWND hWnd, LPARAM lParam)
{
	DWORD processID = 0;
	GetWindowThreadProcessId(hWnd, &processID);

	if (processID != GetCurrentProcessId())
	{
		return TRUE;
	}

	if (hWnd == GetConsoleWindow() || GetWindow(hWnd, GW_OWNER) != NULL || !IsWindowVisible(hWnd))
	{
		return TRUE;
	}

	RECT windowRect = {};
	if (!GetWindowRect(hWnd, &windowRect))
	{
		return TRUE;
	}

	LONG width = windowRect.right - windowRect.left;
	LONG height = windowRect.bottom - windowRect.top;

	if (width <= 0 || height <= 0)
	{
		return TRUE;
	}

	WindowSearchResult* result = reinterpret_cast<WindowSearchResult*>(lParam);
	LONG area = width * height;

	if (area > result->area)
	{
		result->hWnd = hWnd;
		result->area = area;
	}

	return TRUE;
}

HWND GetProcessWindow()
{
	WindowSearchResult result = {};

	for (int i = 0; i < 20 && result.hWnd == NULL; i++)
	{
		EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&result));

		if (result.hWnd == NULL)
		{
			Sleep(500);
		}
	}

	return result.hWnd;
}

void ReadConfig()
{
	INIReader config("RE5Fix.ini");

	bFixUI = config.GetBoolean("Fix UI Scaling", "Enabled", true);
	bCrashFix = config.GetBoolean("Fix Ultrawide Crash", "Enabled", true);
	bRemoveResolutionLimit = config.GetBoolean("Remove Resolution Limits", "Enabled", true);
	iFPSCap = config.GetInteger("FPS Cap", "Value", -1);
	bFPSCap = config.GetInteger("FPS Cap", "Enabled", true);
	bMovieFix = config.GetBoolean("Fix Movies", "Enabled", true);
	iShadowQuality = config.GetInteger("Shadow Quality", "Value", -1);
	bShadowQuality = config.GetBoolean("Shadow Quality", "Enabled", true);
	bColourFilter = config.GetBoolean("Remove Colour Filter", "Enabled", true);
	bFOVAdjust = config.GetBoolean("Increase FOV", "Enabled", true);
	bBorderlessWindowed = config.GetBoolean("Borderless Windowed", "Enabled", false);
	fFOVAdjust = config.GetFloat("Increase FOV", "Value", -1);
	iCustomResX = config.GetInteger("Custom Resolution", "Width", -1);
	iCustomResY = config.GetInteger("Custom Resolution", "Height", -1);

	RECT desktop;
	GetWindowRect(GetDesktopWindow(), &desktop);
	fDesktopRight = (float)desktop.right;
	fDesktopBottom = (float)desktop.bottom;
	fDesktopAspect = fDesktopRight / fDesktopBottom;

	fCustomAspect = (float)iCustomResX / iCustomResY;
}

void BorderlessWindowed()
{
	if (bBorderlessWindowed)
	{
		HWND hWnd = GetProcessWindow();

		if (hWnd)
		{
			LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
			LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

			if ((style & (WS_CAPTION | WS_THICKFRAME)) == 0)
			{
				#if _DEBUG
				std::cout << "Borderless windowed skipped. Game does not appear to be in windowed mode." << std::endl;
				#endif
				return;
			}

			HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFO);

			if (GetMonitorInfo(monitor, &monitorInfo))
			{
				RECT monitorRect = monitorInfo.rcMonitor;

				style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
				style |= WS_POPUP;
				exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

				SetWindowLongPtr(hWnd, GWL_STYLE, style);
				SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);
				SetWindowPos(
					hWnd,
					HWND_TOP,
					monitorRect.left,
					monitorRect.top,
					monitorRect.right - monitorRect.left,
					monitorRect.bottom - monitorRect.top,
					SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

				#if _DEBUG
				std::cout << "Borderless windowed enabled." << std::endl;
				#endif
			}
			else
			{
				#if _DEBUG
				std::cout << "Borderless windowed failed to get monitor info." << std::endl;
				#endif
				return;
			}
		}
		else
		{
			#if _DEBUG
			std::cout << "Borderless windowed failed to find game window." << std::endl;
			#endif
			return;
		}
	}
}

void UIFix()
{
	if (bFixUI && fDesktopAspect > 1.8f or bFixUI && fCustomAspect > 1.8f)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x001DE90F (old: +0x001F43DF)
		// Address of signature = re5dx9.exe + 0x001DE90F
		// "\x8B\x83\x00\x00\x00\x00\x83\xE8\x00\x0F\x84\x00\x00\x00\x00\x83\xE8\x00\x74", "xx????xx?xx????xx?x"
		// "8B 83 ? ? ? ? 83 E8 ? 0F 84 ? ? ? ? 83 E8 ? 74"
		uint8_t* fixUIScanResult1 = Memory::PatternScan(baseModule, "8B ? E0 4E 00 00 ? ? ? ? ? ? ? ? ? 83 ? ? ?");

		if (fixUIScanResult1)
		{
			Memory::PatchBytes((intptr_t)fixUIScanResult1, "\xB8\x02\x00\x00\x00\x90", 6);
		}
		else
		{
			#if _DEBUG
			std::cout << "fixUI1 pattern scan failed." << std::endl;
			#endif	
			return;
		}

		// v1.0.0.129 runtime: re5dx9.exe + 0x001DD236 (old: +0x001F2D06)
		// Address of signature = re5dx9.exe + 0x001DD236
		// "\x8B\x81\x00\x00\x00\x00\x83\xEC\x00\x83\xF8", "xx????xx?xx"
		// "8B 81 ? ? ? ? 83 EC ? 83 F8"
		uint8_t* fixUIScanResult2 = Memory::PatternScan(baseModule, "8B 81 ? ? ? ? 83 EC ? 83 F8");

		if (fixUIScanResult2)
		{
			Memory::PatchBytes((intptr_t)fixUIScanResult2, "\xB8\x02\x00\x00\x00\x90", 6);

			#if _DEBUG
			std::cout << "UI scaling mode set to: " << "2" << std::endl;
			#endif	
		}
		else
		{
			#if _DEBUG
			std::cout << "fixUI2 pattern scan failed." << std::endl;
			#endif	
			return;
		}
	}
}

void ResolutionLimits()
{
	if (bRemoveResolutionLimit)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x00383B2E (old: +0x00387B7E)
		// Address of signature = re5dx9.exe + 0x00383B2E
		// "\x8B\x0D\x00\x00\x00\x00\x8A\x41", "xx????xx"
		// "8B 0D ? ? ? ? 8A 41"
		uint8_t* resLimitScanResult = (Memory::PatternScan(baseModule, "8B 0D ? ? ? ? 8A 41") + 2);

		if (resLimitScanResult)
		{
			DWORD resLimitAddress = *(DWORD*)*(DWORD*)resLimitScanResult;
			int resLimitValue1 = *(int*)(resLimitAddress + 0x50) = (int)131072;
			int resLimitValue2 = *(int*)(resLimitAddress + 0x54) = (int)131072;

			#if _DEBUG
			std::cout << "Resolution limit set to = " << resLimitValue1 << "x" << resLimitValue2 << std::endl;
			#endif	
		}
		else
		{
			#if _DEBUG
			std::cout << "resLimit pattern scan failed." << std::endl;
			#endif	
			return;
		}
	}
}

void UncapFPS()
{
	if (bFPSCap && iFPSCap == 0) // Don't leave it at 0, assume they want it "uncapped".
	{
		iFPSCap = 999;
	}

	if (bFPSCap && iFPSCap > 120)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x0002706C (old: +0x0004B45C)
		// Address of signature = re5dx9.exe + 0x0002706C
		// "\xF3\x0F\x00\x00\x00\x0F\x28\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\x00\x00\xD9\x54", "xx???xx?xx??xx????xx"
		// "F3 0F ? ? ? 0F 28 ? F3 0F ? ? F3 0F ? ? ? ? D9 54"
		uint8_t* FPSCapScanResult = Memory::PatternScan(baseModule, "F3 0F ? ? ? 0F 28 ? F3 0F ? ? F3 0F ? ? ? ? D9 54");

		if (FPSCapScanResult)
		{
			int FPSCapHookLength = 8;
			DWORD FPSCapAddress = (intptr_t)FPSCapScanResult;
			FPSCapValue = (float)iFPSCap;
			FPSCapReturnJMP = FPSCapAddress + FPSCapHookLength;
			Memory::Hook((void*)FPSCapAddress, FPSCap_CC, FPSCapHookLength);

			#if _DEBUG
			std::cout << "FPS Cap set to: " << (int)iFPSCap << std::endl;
			#endif	
		}
		else
		{
			#if _DEBUG
			std::cout << "FPSCap pattern scan failed." << std::endl;
			#endif	
			return;
		}
	}
}

void CrashFix()
{
	if (bCrashFix && fDesktopAspect > 1.8f or bCrashFix && fCustomAspect > 1.8f)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x00CBCCDC -> 9/16 = 0.5625
		// Old signature "00 00 10 3F AC" no longer exists in v1.0.0.129.
		// This longer signature is unique in the runtime memory image.
		uint8_t* CrashFixScanResult = Memory::PatternScan(baseModule, "00 00 10 3F 00 60 EA 46 00 A0 0C 47 AC"); // Returns first match

		if (CrashFixScanResult)
		{
			float newAR = fNativeAspect;
			if (fCustomAspect > 1.8f)
			{
				newAR = (float)iCustomResY / iCustomResX; // Backwards!
			}
			else
			{
				newAR = fDesktopBottom / fDesktopRight; // Backwards, just the way it should be.
			}

			Memory::Write((intptr_t)CrashFixScanResult, newAR);

			#if _DEBUG
			std::cout << "Aspect ratio crash fix enabled. Cutscene AR set to: " << newAR << std::endl;
			#endif		
		}
		else
		{
			#if _DEBUG
			std::cout << "CrashFix pattern scan failed." << std::endl;
			#endif		
			return;
		}
	}
}

void MovieFix()
{
	if (bMovieFix && fDesktopAspect > 1.8f or bMovieFix && fCustomAspect > 1.8f)
	{
		// NOTE: v1.0.0.129 no longer contains the old MovieFix signature at
		// re5dx9.exe + 0x00255700. The new signature is still TODO.
		// The old pattern is kept so the scan fails gracefully instead of hooking
		// the wrong code.
		// Old pattern (pre-v1.0.0.129):
		// "F3 0F ? ? F3 0F ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? 75"

		uint8_t* MovieFixScanResult = Memory::PatternScan(baseModule, "F3 0F ? ? F3 0F ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? 75");

		if (MovieFixScanResult)
		{
			int MovieFixHookLength = 8;
			DWORD MovieFixAddress = (intptr_t)MovieFixScanResult;
			if (fCustomAspect > 1.8f)
			{
				MovieFixValue1 = (float)fNativeAspect / fCustomAspect;

			}
			else
			{
				MovieFixValue1 = (float)fNativeAspect / fDesktopAspect;
			}
			MovieFixValue2 = -MovieFixValue1;
			MovieFixReturnJMP = MovieFixAddress + MovieFixHookLength;
			Memory::Hook((void*)MovieFixAddress, MovieFix_CC, MovieFixHookLength);

			#if _DEBUG
			std::cout << "Pre-rendered movie playback fix enabled." << std::endl;
			#endif
		}
		else
		{
			#if _DEBUG
			std::cout << "MovieFix pattern scan failed." << std::endl;
			#endif
			return;
		}	
	}
}

void IncreaseQuality()
{
	if (bShadowQuality && iShadowQuality >= 1024)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x0023C8A5 (old: +0x00252805)
		// Shadow quality. Low=256, Med=512, High=1024.
		// Address of signature = re5dx9.exe + 0x0023C8A5
		// "\x83\xC0\x00\x56\x8B\xF1\x83\xE0", "xx?xxxxx"
		// "83 C0 ? 56 8B F1 83 E0"
		uint8_t* ShadowQualityScanResult = Memory::PatternScan(baseModule, "83 C0 ? 56 8B F1 83 E0");

		if (ShadowQualityScanResult)
		{
			int ShadowQualityHookLength = 9;
			DWORD ShadowQualityAddress = (intptr_t)ShadowQualityScanResult;
			ShadowQualityValue = iShadowQuality;
			ShadowQualityReturnJMP = ShadowQualityAddress + ShadowQualityHookLength;
			Memory::Hook((void*)ShadowQualityAddress, ShadowQuality_CC, ShadowQualityHookLength);

			#if _DEBUG
			std::cout << "Shadow quality set to " << iShadowQuality << std::endl;
			#endif
		}
		else
		{
			#if _DEBUG
			std::cout << "ShadowQuality pattern scan failed." << std::endl;
			#endif
			return;
		}
	}
}

void ColourFilter()
{
	if (bColourFilter)
	{
		// v1.0.0.129 runtime: re5dx9.exe + 0x0007BD08 (old: +0x000945D8)
		// Address of signature = re5dx9.exe + 0x0007BD08
		// "\x0F\x87\x00\x00\x00\x00\xFF\x24\x00\x00\x00\x00\x00\xD9\x05\x00\x00\x00\x00\x51", "xx????xx?????xx????x"
		// "0F 87 ? ? ? ? FF 24 ? ? ? ? D9 05 ? ? ? ? 51"
		//intptr_t ColourFilterScanResult = scanner.scan("0F 87 ? ? ? ? FF 24 ? ? ? ? ? D9 05 ? ? ? ? 51");

		uint8_t* ColourFilterScanResult = Memory::PatternScan(baseModule, "0F 87 ? ? ? ? FF 24 ? ? ? ? ? D9 05 ? ? ? ? 51");

		if (ColourFilterScanResult)
		{
			Memory::PatchBytes((intptr_t)ColourFilterScanResult, "\xE9\x88\x66\x00\x00\x90", 6); // Patch to JMJ
			std::cout << "Colour filter disabled." << std::endl;
		}
		else
		{
			#if _DEBUG
			std::cout << "ColourFilter pattern scan failed." << std::endl;
			#endif
			return;
		}
	}
}

void FOVAdjust()
{
	if (bFOVAdjust && fFOVAdjust > 0)
	{
		// FOV 1
		// v1.0.0.129 runtime: re5dx9.exe + 0x00446E6F (old: +0x0044B173)
		// Address of signature = re5dx9.exe + 0x00446E6F
		//  "\xF3\x0F\x00\x00\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\x00\x8B\x55\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\xF3\x0F", "xx????xx??xx????xx?xx??xx??xx"
		//  "F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? 8B 55 ? F3 0F ? ? F3 0F ? ? F3 0F"
		uint8_t* FOV1ScanResult = Memory::PatternScan(baseModule, "F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? 8B 55 ? F3 0F ? ? F3 0F ? ? F3 0F");

		if (FOV1ScanResult)
		{
			int FOV1HookLength = 14;
			DWORD FOV1Address = (intptr_t)FOV1ScanResult;
			FOV1ReturnJMP = FOV1Address + FOV1HookLength;
			Memory::Hook((void*)FOV1Address, FOV1_CC, FOV1HookLength);
		}
		else
		{
			#if _DEBUG
			std::cout << "FOV1 pattern scan failed." << std::endl;
			#endif
			return;
		}

		// FOV 2
		// v1.0.0.129 runtime: re5dx9.exe + 0x00446D84 (old: +0x0044B08B)
		// Address of signature = re5dx9.exe + 0x00446D84
		//	"\xF3\x0F\x00\x00\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\xF3\x0F\x00\x00\xE9", "xx???xx??xx???xx??xx??xx??x"
		//	"F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? E9"
		uint8_t* FOV2ScanResult = Memory::PatternScan(baseModule, "F3 0F ? ? ? F3 0F ? ? F3 0F ? ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? E9");

		if (FOV2ScanResult)
		{
			int FOV2HookLength = 14;
			DWORD FOV2Address = (intptr_t)FOV2ScanResult;
			FOV2ReturnJMP = FOV2Address + FOV2HookLength;
			Memory::Hook((void*)FOV2Address, FOV2_CC, FOV2HookLength);
		}
		else
		{
			#if _DEBUG
			std::cout << "FOV2 pattern scan failed." << std::endl;
			#endif
			return;
		}

		// FOV when looking up
		// v1.0.0.129 runtime: re5dx9.exe + 0x00446C63 (old: +0x0044AF6D)
		// Address of signature = re5dx9.exe + 0x00446C63
		//  "\xD9\x41\x00\x8B\x4D\x00\xD9\x19", "xx?xx?xx"
		//	"D9 41 ? 8B 4D ? D9 19"
		uint8_t* FOV3ScanResult = Memory::PatternScan(baseModule, "D9 41 ? 8B 4D ? D9 19");

		if (FOV3ScanResult)
		{
			int FOV3HookLength = 8;
			DWORD FOV3Address = (intptr_t)FOV3ScanResult;
			FOV3ReturnJMP = FOV3Address + FOV3HookLength;
			Memory::Hook((void*)FOV3Address, FOV3_CC, FOV3HookLength);
		}
		else
		{
			#if _DEBUG
			std::cout << "FOV3 pattern scan failed." << std::endl;
			#endif
			return;
		}

		// FOV when looking down
		// v1.0.0.129 runtime: re5dx9.exe + 0x00446CA4 (old: +0x0044AFAB)
		// Address of signature = re5dx9.exe + 0x00446CA4
		//	"\xD9\x5F\x00\xD9\x40\x00\xD9\x1A", "xx?xx?xx"
		//	"D9 5F ? D9 40 ? D9 1A"
		uint8_t* FOV4ScanResult = Memory::PatternScan(baseModule, "D9 5F ? D9 40 ? D9 1A");

		if (FOV4ScanResult)
		{
			int FOV4HookLength = 8;
			DWORD FOV4Address = (intptr_t)FOV4ScanResult;
			FOV4ReturnJMP = FOV4Address + FOV4HookLength;
			Memory::Hook((void*)FOV4Address, FOV4_CC, FOV4HookLength);
		}
		else
		{
			#if _DEBUG
			std::cout << "FOV4 pattern scan failed." << std::endl;
			#endif
			return;
		}

		// FOV when camera centred?
		// v1.0.0.129 runtime: re5dx9.exe + 0x00446C29 (old: +0x0044AF33)
		// Address of signature = re5dx9.exe + 0x00446C29
		// "\xD9\x5F\x00\xD9\x42\x00\xD9\x18", "xx?xx?xx"
		//	"D9 5F ? D9 42 ? D9 18"
		uint8_t* FOV5ScanResult = Memory::PatternScan(baseModule, "D9 5F ? D9 42 ? D9 18");

		if (FOV5ScanResult)
		{
			int FOV5HookLength = 8;
			DWORD FOV5Address = (intptr_t)FOV5ScanResult;
			FOV5ReturnJMP = FOV5Address + FOV5HookLength;
			Memory::Hook((void*)FOV5Address, FOV5_CC, FOV5HookLength);

			#if _DEBUG
			std::cout << "FOV increased by: " << fFOVAdjust << std::endl;
			#endif
		}
		else
		{
			#if _DEBUG
			std::cout << "FOV5 pattern scan failed." << std::endl;
			#endif
			return;
		}
	}
}

DWORD __stdcall Main(void*)
{
	Sleep(1000); // delay first
	
	#if _DEBUG
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
	std::cout << "Console initiated" << std::endl;
	#endif	
	ReadConfig();
	BorderlessWindowed();
	UIFix();
	ResolutionLimits();
	UncapFPS();
	CrashFix();
	MovieFix();
	IncreaseQuality();
	ColourFilter();
	FOVAdjust();

	return true; // end thread
}

HMODULE ourModule;

void Patch_Uninit()
{

}

BOOL APIENTRY DllMain(HMODULE hModule, int ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		ourModule = hModule;
		Proxy_Attach();

		CreateThread(NULL, 0, Main, 0, NULL, 0);
	}
	if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		Patch_Uninit();

		Proxy_Detach();
	}

	return TRUE;
}
