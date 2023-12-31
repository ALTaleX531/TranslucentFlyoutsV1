#include "pch.h"
#include "tflapi.h"
#include "DebugHelper.h"
#include "ThemeHelper.h"
#include "DetoursHelper.h"
#include "AcrylicHelper.h"
#include "Win32HookHelper.h"

using namespace TranslucentFlyoutsLib;
extern HMODULE g_hModule;

thread_local HWND g_hWnd = nullptr;
thread_local DWORD g_referenceCount = 0;
thread_local bool g_alphaFixedState = false;

// 透明化处理
DetoursHook TranslucentFlyoutsLib::DrawThemeBackgroundHook("Uxtheme", "DrawThemeBackground", MyDrawThemeBackground);
// 文字渲染
DetoursHook TranslucentFlyoutsLib::DrawThemeTextExHook("Uxtheme", "DrawThemeTextEx", MyDrawThemeTextEx);
DetoursHook TranslucentFlyoutsLib::DrawThemeTextHook("Uxtheme", "DrawThemeText", MyDrawThemeText);
DetoursHook TranslucentFlyoutsLib::DrawTextWHook("User32", "DrawTextW", MyDrawTextW);
DetoursHook TranslucentFlyoutsLib::DrawTextExWHook("User32", "DrawTextExW", MyDrawTextExW);
// 图标修复
DetoursHook TranslucentFlyoutsLib::SetMenuInfoHook("User32", "SetMenuInfo", MySetMenuInfo);
DetoursHook TranslucentFlyoutsLib::SetMenuItemBitmapsHook("User32", "SetMenuItemBitmaps", MySetMenuItemBitmaps);
DetoursHook TranslucentFlyoutsLib::InsertMenuItemWHook("User32", "InsertMenuItemW", MyInsertMenuItemW);
DetoursHook TranslucentFlyoutsLib::SetMenuItemInfoWHook("User32", "SetMenuItemInfoW", MySetMenuItemInfoW);
DetoursHook TranslucentFlyoutsLib::ModifyMenuWHook("User32", "ModifyMenuW", MyModifyMenuW);

void TranslucentFlyoutsLib::Win32HookStartup()
{
	Detours::Batch(
	    TRUE,
	    DrawThemeBackgroundHook,
	    //
	    DrawThemeTextExHook,
	    DrawThemeTextHook,
	    DrawTextWHook,
	    DrawTextExWHook,
	    //
	    SetMenuInfoHook,
	    SetMenuItemBitmapsHook,
	    InsertMenuItemWHook,
	    SetMenuItemInfoWHook,
	    ModifyMenuWHook
	);
}

void TranslucentFlyoutsLib::Win32HookShutdown()
{
	Detours::Batch(
	    FALSE,
	    DrawThemeBackgroundHook,
	    //
	    DrawThemeTextExHook,
	    DrawThemeTextHook,
	    DrawTextWHook,
	    DrawTextExWHook,
	    //
	    SetMenuInfoHook,
	    SetMenuItemBitmapsHook,
	    InsertMenuItemWHook,
	    SetMenuItemInfoWHook,
	    ModifyMenuWHook
	);
}

HRESULT WINAPI TranslucentFlyoutsLib::MyDrawThemeBackground(
    HTHEME  hTheme,
    HDC     hdc,
    int     iPartId,
    int     iStateId,
    LPCRECT pRect,
    LPCRECT pClipRect
)
{
	// 手动确认是否是透明的
	auto VerifyThemeBackgroundTransparency = [&](HTHEME hTheme, int iPartId, int iStateId)
	{
		RECT Rect = {0, 0, 1, 1};
		bool bResult = false;
		auto f = [&](HDC hMemDC, HPAINTBUFFER hPaintBuffer)
		{
			auto verify = [&](int y, int x, RGBQUAD * pRGBAInfo)
			{
				if (pRGBAInfo->rgbReserved != 0xFF)
				{
					bResult = true;
					return false;
				}
				return true;
			};

			HRESULT hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
			                 hTheme,
			                 hMemDC,
			                 iPartId,
			                 iStateId,
			                 &Rect,
			                 nullptr
			             );

			if (SUCCEEDED(hr))
			{
				BufferedPaintWalkBits(hPaintBuffer, verify);
			}
		};

		MARGINS mr = {};
		if (SUCCEEDED(GetThemeMargins(hTheme, hdc, iPartId, iStateId, TMT_SIZINGMARGINS, nullptr, &mr)))
		{
			Rect.right = max(mr.cxLeftWidth + mr.cxRightWidth, 1);
			Rect.bottom = max(mr.cyTopHeight + mr.cyBottomHeight, 1);
		}
		DoBufferedPaint(hdc, &Rect, f, 0xFF, BPPF_ERASE | BPPF_NOCLIP, FALSE, FALSE);

		return bResult;
	};

	HRESULT hr = S_OK;

	RECT Rect = *pRect;
	if (pClipRect)
	{
		::IntersectRect(&Rect, pRect, pClipRect);
	}

	// 通用绘制函数
	auto f = [&](HDC hMemDC, HPAINTBUFFER hPaintBuffer)
	{
		Clear(hdc, &Rect);

		hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
		         hTheme,
		         hMemDC,
		         iPartId,
		         iStateId,
		         pRect,
		         pClipRect
		     );

		// 为Windows 11准备的特化实现
		COLORREF Color = 0;
		if (
		    SUCCEEDED(hr) and
		    SUCCEEDED(GetThemeColor(hTheme, iPartId, iStateId, TMT_FILLCOLOR, &Color))
		)
		{
			// 修复Windows 11以来的FILLCOLOR的丑陋边框
			bool bHasOpaqueBorder = false;
			if (iPartId == MENU_POPUPITEM or iPartId == MENU_POPUPITEM_IMMERSIVE)
			{
				BYTE r = 0;
				BYTE g = 0;
				BYTE b = 0;
				BYTE a = 0;
				auto remove_opaque_border = [&](int y, int x, RGBQUAD * pRGBAInfo)
				{
					// 初始化要过滤的颜色
					if (x == 0 and y == 0)
					{
						RECT rect = {0, 0, 1, 1};
						auto f = [&](HDC hMemDC, HPAINTBUFFER hPaintBuffer)
						{
							HRESULT hr = S_OK;
							auto initialize = [&](int y, int x, RGBQUAD * pRGBAInfo)
							{
								r = pRGBAInfo->rgbRed;
								g = pRGBAInfo->rgbGreen;
								b = pRGBAInfo->rgbBlue;
								a = pRGBAInfo->rgbReserved;
								return false;
							};

							if (iPartId == MENU_POPUPITEM)
							{
								hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
								         hTheme,
								         hMemDC,
								         MENU_POPUPBACKGROUND,
								         0,
								         &rect,
								         nullptr
								     );
								hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
								         hTheme,
								         hMemDC,
								         MENU_POPUPITEM,
								         MPI_NORMAL,
								         &rect,
								         nullptr
								     );
							}
							if (iPartId == MENU_POPUPITEM_IMMERSIVE)
							{
								hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
								         hTheme,
								         hMemDC,
								         MENU_POPUPBACKGROUND_IMMERSIVE,
								         0,
								         &rect,
								         nullptr
								     );
								hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
								         hTheme,
								         hMemDC,
								         MENU_POPUPITEM_IMMERSIVE,
								         MPI_NORMAL,
								         &rect,
								         nullptr
								     );
							}
							if (SUCCEEDED(hr))
							{
								BufferedPaintWalkBits(hPaintBuffer, initialize);
							}
						};
						DoBufferedPaint(hdc, &rect, f, 0xFF, BPPF_ERASE | BPPF_NOCLIP, FALSE);

						// 没有边框，是完全透明的
						if (r == 0 and g == 0 and b == 0 and a == 0)
						{
							return false;
						}
						else
						{
							bHasOpaqueBorder = true;
						}
					}

					if (
					    pRGBAInfo->rgbRed == r and
					    pRGBAInfo->rgbGreen == g and
					    pRGBAInfo->rgbBlue == b and
					    pRGBAInfo->rgbReserved == a
					)
					{
						pRGBAInfo->rgbRed = 0;
						pRGBAInfo->rgbGreen = 0;
						pRGBAInfo->rgbBlue = 0;
						pRGBAInfo->rgbReserved = 0;
					}
					return true;
				};
				// Windows 11聚焦的菜单项会有纯色Alpha为255的边框，之前我们抠掉了这层边框，这里我们与不透明度做一次运算再画到HDC
				if (BufferedPaintWalkBits(hPaintBuffer, remove_opaque_border) and bHasOpaqueBorder)
				{
					auto partimage_background = [&](HDC hMemDC, HPAINTBUFFER hPaintBuffer)
					{
						HBRUSH hBrush = CreateSolidBrush(RGB(r, g, b));
						FillRect(hMemDC, &Rect, hBrush);
						DeleteObject(hBrush);
						BufferedPaintSetAlpha(hPaintBuffer, &Rect, a);
					};
					DoBufferedPaint(hdc, &Rect, partimage_background, (BYTE)GetCurrentFlyoutOpacity(), BPPF_ERASE, TRUE);
				}
			}
		}
	};

	// 工具提示
	if (
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    VerifyThemeData(hTheme, TEXT("Tooltip")) and
	    (GetCurrentFlyoutPolicy() & Tooltip) and
	    (
	        iPartId == TTP_STANDARD or
	        iPartId == TTP_BALLOON or
	        iPartId == TTP_BALLOONSTEM
	    )
	)
	{
		if (
		    !IsThemeBackgroundPartiallyTransparent(hTheme, iPartId, iStateId) or
		    !VerifyThemeBackgroundTransparency(hTheme, iPartId, iStateId)
		)
		{
			if (!DoBufferedPaint(hdc, &Rect, f, (BYTE)GetCurrentFlyoutOpacity()))
			{
				goto Default;
			}
		}
		else
		{
			goto Default;
		}
	}
	// 弹出菜单
	else if
	(
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    VerifyThemeData(hTheme, TEXT("Menu")) and
		(GetCurrentFlyoutPolicy() & PopupMenu)
	)
	{
		if (IsWindow(g_hWnd))
		{
			if (iPartId != MENU_POPUPBACKGROUND)
			{
				SetWindowEffect(
				    g_hWnd,
				    GetCurrentFlyoutEffect(),
				    GetCurrentFlyoutBorder()
				);
				g_hWnd = nullptr;
			}
		}

		// 在Windows 11 22H2中，Immersive弹出菜单使用的iPartId发生了改变
		// MENU_POPUPBACKGROUND -> 26 (MENU_POPUPBACKGROUND_IMMERSIVE)
		// MENU_POPUPITEM -> 27 (MENU_POPUPITEM_IMMERSIVE)
		if (
		    (
		        iPartId == MENU_POPUPBACKGROUND or
		        iPartId == MENU_POPUPBACKGROUND_IMMERSIVE
		    ) or
		    iPartId == MENU_POPUPGUTTER or
		    (
		        iPartId == MENU_POPUPITEM or
		        iPartId == MENU_POPUPITEM_IMMERSIVE
		    ) or
		    iPartId == MENU_POPUPBORDERS
		)
		{
			// 完全不透明的位图
			if (
			    (
			        iPartId == MENU_POPUPBACKGROUND or
			        iPartId == MENU_POPUPBACKGROUND_IMMERSIVE
			    ) or
			    (
			        !IsThemeBackgroundPartiallyTransparent(hTheme, iPartId, iStateId) or
			        !VerifyThemeBackgroundTransparency(hTheme, iPartId, iStateId)
			    )
			)
			{
				BYTE bOpacity = 255;
				switch (GetCurrentFlyoutColorizeOption())
				{
				case Opacity:
				{
					bOpacity = (BYTE)GetCurrentFlyoutOpacity();
					break;
				}
				case Opaque:
				{
					bOpacity = ((iPartId == MENU_POPUPITEM or iPartId == MENU_POPUPITEM_IMMERSIVE) and iStateId == MPI_HOT) ? 255 : (BYTE)GetCurrentFlyoutOpacity();
					break;
				}
				case Auto:
				{
					bOpacity = ((iPartId == MENU_POPUPITEM or iPartId == MENU_POPUPITEM_IMMERSIVE) and iStateId == MPI_HOT) ? BYTE(min(255 - ((BYTE)GetCurrentFlyoutOpacity() - 204), 255)) : (BYTE)GetCurrentFlyoutOpacity();
					break;
				}
				}
				if (!DoBufferedPaint(hdc, &Rect, f, bOpacity, BPPF_ERASE | (iPartId == MENU_POPUPBORDERS ? BPPF_NONCLIENT : 0UL)))
				{
					goto Default;
				}
			}
			else
				// 主题位图有透明部分
			{
				// 对Windows 11 22H2的支持
				// 先检查是否定义了MENU_POPUPITEM_IMMERSIVE和MENU_POPUPBACKGROUND_IMMERSIVE
				if (
				    IsThemePartDefined(hTheme, MENU_POPUPBACKGROUND_IMMERSIVE, 0) and
				    IsThemePartDefined(hTheme, MENU_POPUPITEM_IMMERSIVE, 0)
				)
				{
					if (iPartId != MENU_POPUPBACKGROUND_IMMERSIVE)
					{
						MyDrawThemeBackground(
						    hTheme,
						    hdc,
						    MENU_POPUPBACKGROUND_IMMERSIVE,
						    0,
						    pRect,
						    pClipRect
						);
					}
					if (!(iPartId == MENU_POPUPITEM_IMMERSIVE and iStateId == MPI_NORMAL))
					{
						MyDrawThemeBackground(
						    hTheme,
						    hdc,
						    MENU_POPUPITEM_IMMERSIVE,
						    MPI_NORMAL,
						    pRect,
						    pClipRect
						);
					}
				}

				else
				{
					if (iPartId != MENU_POPUPBACKGROUND)
					{
						MyDrawThemeBackground(
						    hTheme,
						    hdc,
						    MENU_POPUPBACKGROUND,
						    0,
						    pRect,
						    pClipRect
						);
					}
					if (!(iPartId == MENU_POPUPITEM and iStateId == MPI_NORMAL))
					{
						MyDrawThemeBackground(
						    hTheme,
						    hdc,
						    MENU_POPUPITEM,
						    MPI_NORMAL,
						    pRect,
						    pClipRect
						);
					}
				}
				goto Default;
			}
		}
		else
		{
			goto Default;
		}
	}
	else
	{
		goto Default;
	}
	return hr;
Default:
	hr = DrawThemeBackgroundHook.OldFunction<decltype(MyDrawThemeBackground)>(
	         hTheme,
	         hdc,
	         iPartId,
	         iStateId,
	         pRect,
	         pClipRect
	     );
	return hr;
}

HRESULT WINAPI TranslucentFlyoutsLib::MyDrawThemeTextEx(
    HTHEME        hTheme,
    HDC           hdc,
    int           iPartId,
    int           iStateId,
    LPCTSTR       pszText,
    int           cchText,
    DWORD         dwTextFlags,
    LPRECT        pRect,
    const DTTOPTS *pOptions
)
{
	// pOptions可以为NULL，使用NULL时与DrawThemeText效果无异
	HRESULT hr = S_OK;

	if (
	    pOptions and
	    (
	        !pOptions or
	        (
	            !(pOptions->dwFlags & DTT_CALCRECT) and
	            !(pOptions->dwFlags & DTT_COMPOSITED)
	        )
	    ) and
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    (
	        (VerifyThemeData(hTheme, TEXT("Menu")) and GetCurrentFlyoutPolicy() & PopupMenu) or
	        (VerifyThemeData(hTheme, TEXT("Tooltip")) and GetCurrentFlyoutPolicy() & Tooltip)
	    )
	)
	{
		DTTOPTS Options = *pOptions;
		Options.dwFlags |= DTT_COMPOSITED;

		auto f = [&](HDC hMemDC, HPAINTBUFFER hPaintBuffer)
		{
			g_alphaFixedState = true;
			hr = DrawThemeTextExHook.OldFunction<decltype(MyDrawThemeTextEx)>(
			         hTheme,
			         hMemDC,
			         iPartId,
			         iStateId,
			         pszText,
			         cchText,
			         dwTextFlags,
			         pRect,
			         &Options
			     );
			g_alphaFixedState = false;
		};
		
		if (!DoBufferedPaint(hdc, pRect, f))
		{
			goto Default;
		}

		return hr;
	}
	else
	{
		goto Default;
	}
	return hr;
Default:
	g_alphaFixedState = true;
	hr = DrawThemeTextExHook.OldFunction<decltype(MyDrawThemeTextEx)>(
	         hTheme,
	         hdc,
	         iPartId,
	         iStateId,
	         pszText,
	         cchText,
	         dwTextFlags,
	         pRect,
	         pOptions
	     );
	g_alphaFixedState = false;
	return hr;
}

HRESULT WINAPI TranslucentFlyoutsLib::MyDrawThemeText(
    HTHEME  hTheme,
    HDC     hdc,
    int     iPartId,
    int     iStateId,
    LPCTSTR pszText,
    int     cchText,
    DWORD   dwTextFlags,
    DWORD   dwTextFlags2,
    LPCRECT pRect
)
{
	// dwTextFlags 不支持DT_CALCRECT
	// dwTextFlags2 从未被使用，始终为0
	HRESULT hr = S_OK;

	if (
	    pRect and
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    (
	        (VerifyThemeData(hTheme, TEXT("Menu")) and GetCurrentFlyoutPolicy() & PopupMenu) or
	        (VerifyThemeData(hTheme, TEXT("Tooltip")) and GetCurrentFlyoutPolicy() & Tooltip)
	    )
	)
	{
		DTTOPTS Options = {sizeof(DTTOPTS)};
		RECT Rect = *pRect;
		hr = DrawThemeTextEx(
		         hTheme,
		         hdc,
		         iPartId,
		         iStateId,
		         pszText,
		         cchText,
		         dwTextFlags,
		         &Rect,
		         &Options
		     );
	}
	else
	{
		hr = DrawThemeTextHook.OldFunction<decltype(MyDrawThemeText)>(
		         hTheme,
		         hdc,
		         iPartId,
		         iStateId,
		         pszText,
		         cchText,
		         dwTextFlags,
		         dwTextFlags2,
		         pRect
		     );
	}

	return hr;
}

int WINAPI TranslucentFlyoutsLib::MyDrawTextW(
    HDC     hdc,
    LPCWSTR lpchText,
    int     cchText,
    LPRECT  lprc,
    UINT    format
)
{
	int nResult = 0;
	thread_local int nLastResult = 0;

	if (
	    g_alphaFixedState == true or
	    (format & DT_CALCRECT) or
	    (format & DT_INTERNAL) or
	    (format & DT_NOCLIP) or
	    VerifyCaller(_T("Uxtheme")) or
	    GetBkMode(hdc) != TRANSPARENT or
	    !IsAllowTransparent() or
	    !IsThemeAvailable() or
	    !(GetCurrentFlyoutPolicy() != Null)
	)
	{
		goto Default;
	}
	else
	{
		DTTOPTS Options = {sizeof(DTTOPTS)};
		HTHEME hTheme = OpenThemeData(nullptr, _T("Menu"));
		Options.dwFlags = DTT_TEXTCOLOR;
		Options.crText = GetTextColor(hdc);

		if (hTheme)
		{
			DrawThemeTextEx(hTheme, hdc, 0, 0, lpchText, cchText, format, lprc, &Options);
			CloseThemeData(hTheme);
		}
		else
		{
			goto Default;
		}

		nResult = nLastResult;
	}
	return nResult;
Default:
	nResult =
	    DrawTextWHook.OldFunction<decltype(MyDrawTextW)>(
	        hdc,
	        lpchText,
	        cchText,
	        lprc,
	        format
	    );
	return nResult;
}

int WINAPI TranslucentFlyoutsLib::MyDrawTextExW(
    HDC              hdc,
    LPWSTR           lpchText,
    int              cchText,
    LPRECT           lprc,
    UINT             format,
    LPDRAWTEXTPARAMS lpdtp
)
{
	int nResult = 0;
	thread_local int nLastResult = 0;

	if (
	    lpdtp or
	    g_alphaFixedState == true or
	    (format & DT_CALCRECT) or
	    (format & DT_INTERNAL) or
	    (format & DT_NOCLIP) or
	    VerifyCaller(_T("Uxtheme")) or
	    GetBkMode(hdc) != TRANSPARENT or
	    !IsAllowTransparent() or
	    !IsThemeAvailable() or
	    !(GetCurrentFlyoutPolicy() != Null)
	)
	{
		goto Default;
	}
	else
	{
		DTTOPTS Options = {sizeof(DTTOPTS)};
		HTHEME hTheme = OpenThemeData(nullptr, _T("Menu"));
		Options.dwFlags = DTT_TEXTCOLOR;
		Options.crText = GetTextColor(hdc);

		if (hTheme)
		{
			DrawThemeTextEx(hTheme, hdc, 0, 0, lpchText, cchText, format, lprc, &Options);
			CloseThemeData(hTheme);
		}
		else
		{
			goto Default;
		}

		nResult = nLastResult;
	}
	return nResult;
Default:
	nResult =
	    DrawTextExWHook.OldFunction<decltype(MyDrawTextExW)>(
	        hdc,
	        lpchText,
	        cchText,
	        lprc,
	        format,
	        lpdtp
	    );
	return nResult;
}

BOOL WINAPI TranslucentFlyoutsLib::MySetMenuInfo(
    HMENU hMenu,
    LPCMENUINFO lpMenuInfo
)
{
	BOOL bResult = TRUE;
	// 上次留下的画刷
	thread_local COLORREF dwLastColor = 0xFFFFFF;

	if (
	    (lpMenuInfo->fMask & MIM_BACKGROUND) and
	    lpMenuInfo->hbrBack and
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    (GetCurrentFlyoutPolicy() & PopupMenu)
	)
	{
		PBYTE pvBits = nullptr;
		MENUINFO MenuInfo = *lpMenuInfo;
		HBITMAP hBitmap = CreateDIB(nullptr, 1, 1, (PVOID *)&pvBits);

		if (hBitmap and pvBits)
		{
			BYTE bAlpha = (BYTE)GetCurrentFlyoutOpacity();
			RECT rcPaint = { 0, 0, 1, 1 };
			HDC hMemDC = CreateCompatibleDC(nullptr);
			if (hMemDC)
			{
				SelectObject(hMemDC, hBitmap);
				FillRect(hMemDC, &rcPaint, lpMenuInfo->hbrBack);
				pvBits[0] = PremultiplyColor(pvBits[0], bAlpha);
				pvBits[1] = PremultiplyColor(pvBits[1], bAlpha);
				pvBits[2] = PremultiplyColor(pvBits[2], bAlpha);
				pvBits[3] = bAlpha;

				DeleteDC(hMemDC);
			}
			else
			{
				DeleteObject(hBitmap);
				goto Default;
			}
			

			// 创建位图画刷
			// 只有位图画刷才有Alpha值
			HBRUSH hBrush = CreatePatternBrush(hBitmap);

			DeleteObject(hBitmap);
			if (hBrush)
			{
				// 此画刷会被内核自动释放
				MenuInfo.hbrBack = hBrush;
				// 我们替换了调用者提供的画刷，要帮它释放
				DeleteObject(lpMenuInfo->hbrBack);
			}
			else
			{
				goto Default;
			}

			bResult = SetMenuInfoHook.OldFunction<decltype(MySetMenuInfo)>(
			              hMenu,
			              &MenuInfo
			          );
		}
		else
		{
			goto Default;
		}
	}
	else
	{
		goto Default;
	}

	return bResult;
Default:
	bResult = SetMenuInfoHook.OldFunction<decltype(MySetMenuInfo)>(
	              hMenu,
	              lpMenuInfo
	          );
	return bResult;
}

BOOL WINAPI TranslucentFlyoutsLib::MySetMenuItemBitmaps(
    HMENU   hMenu,
    UINT    uPosition,
    UINT    uFlags,
    HBITMAP hBitmapUnchecked,
    HBITMAP hBitmapChecked
)
{
	BOOL bResult = FALSE;

	if (
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    GetCurrentFlyoutPolicy() & PopupMenu
	)
	{
		if (hBitmapUnchecked != hBitmapChecked)
		{
			PrepareAlpha(hBitmapUnchecked);
			PrepareAlpha(hBitmapChecked);
		}
		else
		{
			PrepareAlpha(hBitmapChecked);
		}
	}

	goto Default;
	return bResult;
Default:
	bResult = SetMenuItemBitmapsHook.OldFunction<decltype(MySetMenuItemBitmaps)>(
	              hMenu,
	              uPosition,
	              uFlags,
	              hBitmapUnchecked,
	              hBitmapChecked
	          );
	return bResult;
}

BOOL WINAPI TranslucentFlyoutsLib::MyInsertMenuItemW(
    HMENU            hMenu,
    UINT             item,
    BOOL             fByPosition,
    LPCMENUITEMINFOW lpmii
)
{
	BOOL bResult = FALSE;

	if (
	    lpmii and
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    GetCurrentFlyoutPolicy() & PopupMenu)
	{
		if (lpmii->fMask & MIIM_BITMAP)
		{
			PrepareAlpha(lpmii->hbmpItem);
		}
		if (lpmii->fMask & MIIM_CHECKMARKS)
		{
			if (lpmii->hbmpUnchecked != lpmii->hbmpChecked)
			{
				PrepareAlpha(lpmii->hbmpUnchecked);
				PrepareAlpha(lpmii->hbmpChecked);
			}
			else
			{
				PrepareAlpha(lpmii->hbmpChecked);
			}
		}
	}

	goto Default;
	return bResult;
Default:
	bResult = InsertMenuItemWHook.OldFunction<decltype(MyInsertMenuItemW)>(
	              hMenu,
	              item,
	              fByPosition,
	              lpmii
	          );
	return bResult;
}

BOOL WINAPI TranslucentFlyoutsLib::MySetMenuItemInfoW(
    HMENU            hMenu,
    UINT             item,
    BOOL             fByPositon,
    LPCMENUITEMINFOW lpmii
)
{
	BOOL bResult = FALSE;

	if (
	    lpmii and
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    GetCurrentFlyoutPolicy() & PopupMenu
	)
	{
		if (lpmii->fMask & MIIM_BITMAP)
		{
			PrepareAlpha(lpmii->hbmpItem);
		}
		if (lpmii->fMask & MIIM_CHECKMARKS)
		{
			if (lpmii->hbmpUnchecked != lpmii->hbmpChecked)
			{
				PrepareAlpha(lpmii->hbmpUnchecked);
				PrepareAlpha(lpmii->hbmpChecked);
			}
			else
			{
				PrepareAlpha(lpmii->hbmpChecked);
			}
		}
	}

	goto Default;
	return bResult;
Default:
	bResult = SetMenuItemInfoWHook.OldFunction<decltype(MySetMenuItemInfoW)>(
	              hMenu,
	              item,
	              fByPositon,
	              lpmii
	          );

	return bResult;
}

BOOL WINAPI TranslucentFlyoutsLib::MyModifyMenuW(
    HMENU    hMnu,
    UINT     uPosition,
    UINT     uFlags,
    UINT_PTR uIDNewItem,
    LPCWSTR  lpNewItem
)
{
	BOOL bResult = FALSE;

	if (
	    IsAllowTransparent() and
	    IsThemeAvailable() and
	    GetCurrentFlyoutPolicy() & PopupMenu
	)
	{
		if (uFlags & MF_BITMAP)
		{
			PrepareAlpha((HBITMAP)lpNewItem);
		}
	}

	goto Default;
	return bResult;
Default:
	bResult = ModifyMenuWHook.OldFunction<decltype(MyModifyMenuW)>(
	              hMnu,
	              uPosition,
	              uFlags,
	              uIDNewItem,
	              lpNewItem
	          );
	return bResult;
}