#pragma once
#include "pch.h"

namespace TranslucentFlyoutsLib
{
	typedef UINT(WINAPI *pfnGetWindowDPI)(HWND hwnd);
	typedef HRESULT(WINAPI *pfnGetThemeClass)(HTHEME hTheme, LPCTSTR pszClassName, int cchClassName);
	typedef BOOL(WINAPI *pfnIsThemeClassDefined)(HTHEME hTheme, LPCTSTR pszAppName, LPCTSTR pszClassName, BOOL bMatchClass);
	typedef BOOL(WINAPI *pfnIsTopLevelWindow)(HWND hWnd);

	enum MENUPARTSEX
	{
		MENU_POPUPBACKGROUND_IMMERSIVE = 26,
		MENU_POPUPITEM_IMMERSIVE
	};

	static inline UINT GetWindowDPI(HWND hwnd)
	{
		static const auto &GetWindowDPI = (pfnGetWindowDPI)GetProcAddress(GetModuleHandle(TEXT("User32")), MAKEINTRESOURCEA(2707));
		if (GetWindowDPI)
		{
			return GetWindowDPI(hwnd);
		}
		else
		{
			return 96;
		}
	}

	// From UIRibbon.dll
	float inline MsoScaleForWindowDPI(HWND hwnd, float size)
	{
		return fmaxf(1.0, (float)GetWindowDPI(hwnd) / 96.f) * size;
	}

	static inline bool IsAllowTransparent()
	{
		DWORD dwResult = 0;
		DWORD dwSize = sizeof(DWORD);
		RegGetValue(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), TEXT("EnableTransparency"), RRF_RT_REG_DWORD, nullptr, &dwResult, &dwSize);
		return dwResult == 1;
	}

	static inline HRESULT WINAPI GetThemeClass(HTHEME hTheme, LPCTSTR pszClassName, const int cchClassName)
	{
		static const auto& GetThemeClass = (pfnGetThemeClass)GetProcAddress(GetModuleHandle(TEXT("Uxtheme")), MAKEINTRESOURCEA(74));
		if (GetThemeClass)
		{
			return GetThemeClass(hTheme, pszClassName, cchClassName);
		}
		else
		{
			return E_POINTER;
		}
	}

	static inline BOOL WINAPI IsThemeClassDefined(HTHEME hTheme, LPCTSTR pszAppName, LPCTSTR pszClassName, BOOL bMatchClass)
	{
		static const auto& IsThemeClassDefined = (pfnIsThemeClassDefined)GetProcAddress(GetModuleHandle(TEXT("Uxtheme")), MAKEINTRESOURCEA(50));
		if (IsThemeClassDefined)
		{
			return IsThemeClassDefined(hTheme, pszAppName, pszClassName, bMatchClass);
		}
		else
		{
			return FALSE;
		}
	}

	static inline BOOL IsThemeAvailable()
	{
		return
			IsAppThemed() and
			IsThemeActive() and
			(GetThemeAppProperties() & STAP_VALIDBITS);
	}

	static inline bool VerifyThemeData(HTHEME hTheme, LPCTSTR pszThemeClassName)
	{
		TCHAR pszClassName[MAX_PATH + 1];
		GetThemeClass(hTheme, pszClassName, MAX_PATH);
		return !_wcsicmp(pszClassName, pszThemeClassName);
	}

	static inline BOOL IsTopLevelWindow(HWND hWnd)
	{
		static const auto& IsTopLevelWindow = (pfnIsTopLevelWindow)GetProcAddress(GetModuleHandle(TEXT("User32")), "IsTopLevelWindow");
		if (IsTopLevelWindow)
		{
			return IsTopLevelWindow(hWnd);
		}
		return FALSE;
	}

	static inline bool VerifyWindowClass(HWND hWnd, LPCTSTR pszClassName, BOOL bRequireTopLevel = FALSE)
	{
		TCHAR pszClass[MAX_PATH + 1] = {};
		GetClassName(hWnd, pszClass, MAX_PATH);
		return (!_tcscmp(pszClass, pszClassName) and (bRequireTopLevel ? IsTopLevelWindow(hWnd) : !IsTopLevelWindow(hWnd)));
	}

	static inline bool IsPopupMenuFlyout(HWND hWnd)
	{
		TCHAR pszClass[MAX_PATH + 1] = {};
		// 微软内部判断窗口是否是弹出菜单的方法
		if (GetClassLong(hWnd, GCW_ATOM) == 32768)
		{
			return true;
		}
		GetClassName(hWnd, pszClass, MAX_PATH);
		return
		    (
		        IsTopLevelWindow(hWnd) and
		        (
		            !_tcscmp(pszClass, TEXT("#32768")) or
		            !_tcscmp(pszClass, TEXT("DropDown"))
		        )
		    );
	}

	static inline bool IsTooltipFlyout(HWND hWnd)
	{
		return VerifyWindowClass(hWnd, TOOLTIPS_CLASS, TRUE);
	}

	static inline void Clear(HDC hdc, LPCRECT lpRect)
	{
		PatBlt(
		    hdc,
		    lpRect->left,
		    lpRect->top,
		    lpRect->right - lpRect->left,
		    lpRect->bottom - lpRect->top,
		    BLACKNESS
		);
	}

	static inline HBITMAP CreateDIB(HDC hdc, LONG nWidth, LONG nHeight, PVOID* pvBits)
	{
		BITMAPINFO bitmapInfo = {};
		bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biWidth = nWidth;
		bitmapInfo.bmiHeader.biHeight = -nHeight;

		return CreateDIBSection(hdc, &bitmapInfo, DIB_RGB_COLORS, pvBits, nullptr, 0);
	}

	static inline BYTE PremultiplyColor(BYTE color, BYTE alpha = 255)
	{
		return (color * (alpha + 1) >> 8);
	}

	static void PrepareAlpha(HBITMAP hBitmap)
	{
		if (!hBitmap)
		{
			return;
		}

		if (GetObjectType(hBitmap) != OBJ_BITMAP)
		{
			return;
		}

		HDC hdc = GetDC(nullptr);
		BITMAPINFO BitmapInfo = {sizeof(BitmapInfo.bmiHeader)};

		if (hdc)
		{
			if (GetDIBits(hdc, hBitmap, 0, 0, nullptr, &BitmapInfo, DIB_RGB_COLORS) and BitmapInfo.bmiHeader.biBitCount == 32)
			{
				BitmapInfo.bmiHeader.biCompression = BI_RGB;

				BYTE *pvBits = new (std::nothrow) BYTE[BitmapInfo.bmiHeader.biSizeImage];

				if (pvBits)
				{
					if (GetDIBits(hdc, hBitmap, 0, BitmapInfo.bmiHeader.biHeight, (LPVOID)pvBits, &BitmapInfo, DIB_RGB_COLORS))
					{
						bool bHasAlpha = false;

						for (UINT i = 0; i < BitmapInfo.bmiHeader.biSizeImage; i += 4)
						{
							if (pvBits[i + 3] != 0)
							{
								bHasAlpha = true;
								break;
							}
						}

						if (!bHasAlpha)
						{
							for (UINT i = 0; i < BitmapInfo.bmiHeader.biSizeImage; i += 4)
							{
								pvBits[i] = PremultiplyColor(pvBits[0]);
								pvBits[i + 1] = PremultiplyColor(pvBits[i + 1]);
								pvBits[i + 2] = PremultiplyColor(pvBits[i + 2]);
								pvBits[i + 3] = 255;
							}
						}

						SetDIBits(hdc, hBitmap, 0, BitmapInfo.bmiHeader.biHeight, pvBits, &BitmapInfo, DIB_RGB_COLORS);
					}
					delete[] pvBits;
				}
			}
			ReleaseDC(nullptr, hdc);
		}
	}

	template <typename T>
	static BOOL DoBufferedPaint(
	    HDC hdc,
	    LPCRECT Rect,
	    T&& t,
	    BYTE dwOpacity = 255,
	    DWORD dwFlag = BPPF_ERASE,
	    BOOL bUpdateTarget = TRUE,
		BOOL bUseBlendFunction = TRUE
	)
	{
		HDC hMemDC = nullptr;
		BLENDFUNCTION BlendFunction = {AC_SRC_OVER, 0, dwOpacity, AC_SRC_ALPHA};
		BP_PAINTPARAMS PaintParams = {sizeof(BP_PAINTPARAMS), dwFlag, nullptr, bUseBlendFunction ? &BlendFunction : nullptr};
		HPAINTBUFFER hPaintBuffer = BeginBufferedPaint(hdc, Rect, BPBF_TOPDOWNDIB, &PaintParams, &hMemDC);
		if (hPaintBuffer and hMemDC)
		{
			SetLayout(hMemDC, GetLayout(hdc));
			SetMapMode(hMemDC, GetMapMode(hdc));
			SetGraphicsMode(hMemDC, GetGraphicsMode(hdc));
			SelectObject(hMemDC, GetCurrentObject(hdc, OBJ_FONT));
			SelectObject(hMemDC, GetCurrentObject(hdc, OBJ_BRUSH));
			SelectObject(hMemDC, GetCurrentObject(hdc, OBJ_PEN));
			SetTextAlign(hMemDC, GetTextAlign(hdc));
			SetTextCharacterExtra(hMemDC, GetTextCharacterExtra(hdc));

			t(hMemDC, hPaintBuffer);
			EndBufferedPaint(hPaintBuffer, bUpdateTarget);
		}
		else
		{
			return FALSE;
		}
		return TRUE;
	}

	// 此函数应该在EndBufferedPaint之前调用
	template <typename T>
	static BOOL BufferedPaintWalkBits(
	    HPAINTBUFFER hPaintBuffer,
	    T&& t
	)
	{
		int cxRow = 0;
		RGBQUAD* pbBuffer = nullptr;
		RECT targetRect = {};
		if (SUCCEEDED(GetBufferedPaintTargetRect(hPaintBuffer, &targetRect)))
		{
			int cx = targetRect.right - targetRect.left;
			int cy = targetRect.bottom - targetRect.top;
			if (SUCCEEDED(GetBufferedPaintBits(hPaintBuffer, &pbBuffer, &cxRow)))
			{
				for (int y = 0; y < cy; y++)
				{
					for (int x = 0; x < cx; x++)
					{
						RGBQUAD *pRGBAInfo = &pbBuffer[y * cxRow + x];
						if (!t(y, x, pRGBAInfo))
						{
							break;
						}
					}
				}
				return TRUE;
			}
		}
		return FALSE;
	}
};