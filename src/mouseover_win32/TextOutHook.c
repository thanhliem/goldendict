/*
 * February 2, 2009: Konstantin Isakov <ikm@users.sf.net>
 *   Support for ETO_GLYPH_INDEX in ExtTextOutW() function added. This makes
 *   Firefox 3 work.
*/

#define _WIN32_WINNT 0x0500

#include "TextOutHook.h"
#include "GetWord.h"
#include "HookImportFunction.h"


typedef BOOL (WINAPI *TextOutANextHook_t)(HDC hdc, int nXStart, int nYStart, LPCSTR lpszString,int cbString);
TextOutANextHook_t TextOutANextHook = NULL;
typedef BOOL (WINAPI *TextOutWNextHook_t)(HDC hdc, int nXStart, int nYStart, LPCWSTR lpszString,int cbString);
TextOutWNextHook_t TextOutWNextHook = NULL; 
typedef BOOL (WINAPI *ExtTextOutANextHook_t)(HDC hdc, int nXStart, int nYStart, UINT fuOptions, CONST RECT *lprc, LPCSTR lpszString, UINT cbString, CONST INT *lpDx);
ExtTextOutANextHook_t ExtTextOutANextHook = NULL;
typedef BOOL (WINAPI *ExtTextOutWNextHook_t)(HDC hdc, int nXStart, int nYStart, UINT fuOptions, CONST RECT *lprc, LPCWSTR lpszString, UINT cbString, CONST INT *lpDx);
ExtTextOutWNextHook_t ExtTextOutWNextHook = NULL;

typedef struct TEverythingParams {
	HWND WND;
	POINT Pt;
	int Active;
	int WordLen;
	int Unicode;
	int BeginPos;
	char MatchedWordA[256];
	wchar_t MatchedWordW[256];
} TEverythingParams;

TEverythingParams *CurParams = NULL;

static void ConvertToMatchedWordA(TEverythingParams *TP)
{
	if (TP->WordLen>0) {
		int BeginPos;
		if (!TP->Unicode) {
			BeginPos = TP->BeginPos;
			if (BeginPos) {
				TP->BeginPos = MultiByteToWideChar(CP_ACP, 0, TP->MatchedWordA, BeginPos, TP->MatchedWordW, sizeof(TP->MatchedWordW)/sizeof(TP->MatchedWordW[0]) - 1);
				if (TP->BeginPos == 0) {
					TP->WordLen=0;
					TP->MatchedWordA[0] = '\0';
					return;
				}
			}
			TP->WordLen = MultiByteToWideChar(CP_ACP, 0, TP->MatchedWordA + BeginPos, TP->WordLen - BeginPos, TP->MatchedWordW + TP->BeginPos, sizeof(TP->MatchedWordW)/sizeof(TP->MatchedWordW[0]) - 1 - TP->BeginPos);
			if (TP->WordLen == 0) {
				TP->WordLen=TP->BeginPos;
				TP->MatchedWordA[TP->WordLen] = '\0';
				return;
			}
			TP->WordLen += TP->BeginPos;
		}
		BeginPos = TP->BeginPos;
		if (BeginPos) {
			wchar_t temp = TP->MatchedWordW[BeginPos];
			TP->MatchedWordW[BeginPos] = 0;
			TP->BeginPos = WideCharToMultiByte(CP_UTF8, 0, TP->MatchedWordW, BeginPos + 1, TP->MatchedWordA, sizeof(TP->MatchedWordA), NULL, NULL);
			TP->MatchedWordW[BeginPos] = temp;
			TP->BeginPos--;
			if (TP->BeginPos<=0) {
				TP->WordLen=0;
				TP->MatchedWordA[0] = '\0';
				return;
			} else if (TP->BeginPos == sizeof(TP->MatchedWordA)-1) {
				TP->WordLen=sizeof(TP->MatchedWordA)-1;
				TP->MatchedWordA[sizeof(TP->MatchedWordA)-1] = '\0';
				return;
			}
		}
		TP->MatchedWordW[TP->WordLen] = 0;
		TP->WordLen = WideCharToMultiByte(CP_UTF8, 0, TP->MatchedWordW + BeginPos, TP->WordLen - BeginPos + 1, TP->MatchedWordA + TP->BeginPos, sizeof(TP->MatchedWordA) - TP->BeginPos, NULL, NULL);
		TP->WordLen--;
		if (TP->WordLen<=0) {
			TP->WordLen=TP->BeginPos;
			TP->MatchedWordA[TP->WordLen] = '\0';
			return;
		}
		TP->WordLen += TP->BeginPos;
		TP->MatchedWordA[TP->WordLen] = '\0';
	} else {
		TP->MatchedWordA[0] = '\0';
	}
}

static int MyCopyMemory(char *a, const char *b, int len)
{
	int count = 0;
	int i;
	for (i=0; i<len; i++) {
		if (*b != '&') {
			count++;
			*a = *b;
			a++;
		}
		b++; 
	}
	return count;
}

static void IterateThroughItems(HWND WND, HMENU menu, POINT *p)
{
	int count = GetMenuItemCount(menu);
	RECT rec;
	MENUITEMINFO info;
	int i;
	for (i=0; i<count; i++) {
		if (GetMenuItemRect(WND, menu, i, &rec) && (rec.left<=p->x) && (p->x<=rec.right) && (rec.top<=p->y) && (p->y<=rec.bottom)) {
			ZeroMemory(&info, sizeof(info));
			info.cbSize = sizeof(info);
			info.fMask = MIIM_TYPE | MIIM_SUBMENU;
			info.cch = 256;
			info.dwTypeData = malloc(256);
			GetMenuItemInfo(menu, i, TRUE, &info);
			if (info.cch>0) {
				if (info.cch > 255)
					CurParams->WordLen = 255;
				else
					CurParams->WordLen = info.cch;
				CurParams->Unicode = FALSE;
				CurParams->WordLen = MyCopyMemory(CurParams->MatchedWordA, info.dwTypeData, CurParams->WordLen);
				CurParams->BeginPos = 0;
			}
			free(info.dwTypeData);
			break;
		}
	}
}

static void GetWordTextOutHook (TEverythingParams *TP)
{
	CurParams = TP;
	ScreenToClient(TP->WND, &(TP->Pt));
	if (TP->Pt.y<0) {
		char buffer[256];
		HMENU menu;
		char buffer2[256];

		GetWindowText(TP->WND, buffer, sizeof(buffer)-1);
		SetWindowText(TP->WND, "");
		
		GetWindowText(TP->WND, buffer2, sizeof(buffer2)-1);
		if (buffer2[0]) { // MDI window.
			char *p = strstr(buffer, buffer2);
			if (p) {
				if (p == buffer) { // FWS_PREFIXTITLE
					strcpy(buffer, buffer+strlen(buffer2));
				} else {
					*p = '\0';
				}
			}
		}
		CurParams->Active = TRUE;
		SetWindowText(TP->WND, buffer);
		CurParams->Active = FALSE;
		menu = GetMenu(TP->WND);
		if (menu) {
			ClientToScreen(TP->WND, &(TP->Pt));
			IterateThroughItems(TP->WND, menu, &(TP->Pt));
		}
	}
	else {
		RECT UpdateRect;
		GetClientRect(TP->WND, &UpdateRect);
		UpdateRect.top = TP->Pt.y;
		UpdateRect.bottom = TP->Pt.y + 1;
		CurParams->Active = TRUE;
		InvalidateRect(TP->WND, &UpdateRect, FALSE);
		UpdateWindow(TP->WND);
		CurParams->Active = FALSE;
	}
	CurParams = NULL;
}

char* ExtractFromEverything(HWND WND, POINT Pt, int *BeginPos)
{
	TEverythingParams CParams;

	ZeroMemory(&CParams, sizeof(CParams));
	CParams.WND = WND;
	CParams.Pt = Pt;
	GetWordTextOutHook(&CParams);
	ConvertToMatchedWordA(&CParams);
	*BeginPos = CParams.BeginPos;
	return _strdup(CParams.MatchedWordA);
}

static void IsInsidePointA(const HDC DC, int X, int Y, LPCSTR Str, int Count)
{
	SIZE Size;
	if ((Count > 0) && GetTextExtentPoint32A(DC, Str, Count, &Size)) {
		DWORD Flags = GetTextAlign(DC);
		POINT Pt;
		RECT Rect;

		if (Flags & TA_UPDATECP) {
			GetCurrentPositionEx(DC, &Pt);
		} else {
			Pt.x = X;
			Pt.y = Y;
		}
		if (Flags & TA_CENTER) {
			Pt.x-=(Size.cx/2);
		} else if (Flags & TA_RIGHT) {
			Pt.x-=Size.cx;
		}
		if (Flags & TA_BASELINE) {
			TEXTMETRIC tm;
			GetTextMetricsA(DC, &tm);
			Pt.y-=tm.tmAscent;
		} else if (Flags & TA_BOTTOM) {
			Pt.y-=Size.cy;
		}
		LPtoDP(DC, &Pt, 1);
	
		Rect.left = Pt.x;
		Rect.right = Pt.x + Size.cx;
		Rect.top = Pt.y;
		Rect.bottom = Pt.y + Size.cy;
		if (((Rect.left <= Rect.right) && (CurParams->Pt.x >= Rect.left) && (CurParams->Pt.x <= Rect.right)) ||
			((Rect.left > Rect.right) && (CurParams->Pt.x <= Rect.left) && (CurParams->Pt.x >= Rect.right))) {
			int BegPos;

		//if (PtInRect(&Rect, CurParams->Pt)) {
			CurParams->Active = !PtInRect(&Rect, CurParams->Pt);
			//CurParams->Active = FALSE;
			BegPos = (int)((abs((CurParams->Pt.x - Rect.left) / (Rect.right - Rect.left)) * (Count - 1)) + 0.5);
			while ((BegPos < Count - 1) && GetTextExtentPoint32A(DC, Str, BegPos + 1, &Size) && (Size.cx < CurParams->Pt.x - Rect.left))
				BegPos++;
			while ((BegPos >= 0) && GetTextExtentPoint32A(DC, Str, BegPos + 1, &Size) && (Size.cx > CurParams->Pt.x - Rect.left))
				BegPos--;
			if (BegPos < Count - 1)
				BegPos++;
			CurParams->BeginPos = BegPos;
			if (Count > 255)
				CurParams->WordLen = 255;
			else
				CurParams->WordLen = Count;
			CurParams->Unicode = FALSE;
			CopyMemory(CurParams->MatchedWordA, Str, CurParams->WordLen);
		}
	}
}

static void IsInsidePointW(const HDC DC, int X, int Y, LPCWSTR Str, int Count)
{
	SIZE Size;
	if ((Count > 0) && GetTextExtentPoint32W(DC, Str, Count, &Size)) {
		DWORD Flags = GetTextAlign(DC);
		POINT Pt;
		RECT Rect;

		if (Flags & TA_UPDATECP) {
			GetCurrentPositionEx(DC, &Pt);
		} else {
			Pt.x = X;
			Pt.y = Y;
		}
		if (Flags & TA_CENTER) {
			Pt.x-=(Size.cx/2);
		} else if (Flags & TA_RIGHT) {
			Pt.x-=Size.cx;
		}
		if (Flags & TA_BASELINE) {
			TEXTMETRICW tm;
			GetTextMetricsW(DC, &tm);
			Pt.y-=tm.tmAscent;
		} else if (Flags & TA_BOTTOM) {
			Pt.y-=Size.cy;
		}
		LPtoDP(DC, &Pt, 1);

		Rect.left = Pt.x;
		Rect.right = Pt.x + Size.cx;
		Rect.top = Pt.y;
		Rect.bottom = Pt.y + Size.cy;
		// Bug: We don't check Pt.y here, as don't call PtInRect() directly, because 
		// in Title bar, Start Menu, IE, FireFox, Opera etc., the Rect.top and Rect.bottom will be wrong.
		// I try to use GetDCOrgEx(DC, &Pt), but they are not normal HDC that Pt.x and Pt.y will equal to 0 in these cases.
		// And use GetWindowRect() then get Rect.left and Rect.top is only useful on Title bar.
		if (((Rect.left <= Rect.right) && (CurParams->Pt.x >= Rect.left) && (CurParams->Pt.x <= Rect.right)) ||
			((Rect.left > Rect.right) && (CurParams->Pt.x <= Rect.left) && (CurParams->Pt.x >= Rect.right))) {
			int BegPos;

		//if (PtInRect(&Rect, CurParams->Pt)) {
			CurParams->Active = !PtInRect(&Rect, CurParams->Pt);
			//CurParams->Active = FALSE;
			BegPos = (int)((abs((CurParams->Pt.x - Rect.left) / (Rect.right - Rect.left)) * (Count - 1)) + 0.5);
			while ((BegPos < Count - 1) && GetTextExtentPoint32W(DC, Str, BegPos + 1, &Size) && (Size.cx < CurParams->Pt.x - Rect.left))
				BegPos++;
			while ((BegPos >= 0) && GetTextExtentPoint32W(DC, Str, BegPos + 1, &Size) && (Size.cx > CurParams->Pt.x - Rect.left))
				BegPos--;
			if (BegPos < Count - 1)
				BegPos++;
			CurParams->BeginPos = BegPos;
			if (Count > 255)
				CurParams->WordLen = 255;
			else
				CurParams->WordLen = Count;
			CurParams->Unicode = TRUE;
			CopyMemory(CurParams->MatchedWordW, Str, CurParams->WordLen * sizeof(wchar_t));
		}
	}
}

BOOL WINAPI TextOutACallbackProc(HDC hdc, int nXStart, int nYStart, LPCSTR lpszString, int cbString)
{
	if (CurParams && CurParams->Active)
		IsInsidePointA(hdc, nXStart, nYStart, lpszString, cbString);
	return TextOutANextHook(hdc, nXStart, nYStart, lpszString, cbString);
}

BOOL WINAPI TextOutWCallbackProc(HDC hdc, int nXStart, int nYStart, LPCWSTR lpszString, int cbString)
{
	if (CurParams && CurParams->Active)
		IsInsidePointW(hdc, nXStart, nYStart, lpszString, cbString);
	return TextOutWNextHook(hdc, nXStart, nYStart, lpszString, cbString);
}

BOOL WINAPI ExtTextOutACallbackProc(HDC hdc, int nXStart, int nYStart, UINT fuOptions, CONST RECT *lprc, LPCSTR lpszString, UINT cbString, CONST INT *lpDx)
{
	if (CurParams && CurParams->Active)
		IsInsidePointA(hdc, nXStart, nYStart, lpszString, cbString);
	return ExtTextOutANextHook(hdc, nXStart, nYStart, fuOptions, lprc, lpszString, cbString, lpDx);
}

BOOL WINAPI ExtTextOutWCallbackProc(HDC hdc, int nXStart, int nYStart, UINT fuOptions, CONST RECT *lprc, LPCWSTR lpszString, UINT cbString, CONST INT *lpDx)
{
	if (CurParams && CurParams->Active)
  {
    if ( fuOptions & ETO_GLYPH_INDEX )
    {
      LPGLYPHSET ranges;
      WCHAR * allChars, * ptr, * restoredString;
      WORD * allIndices;
      unsigned x;

      // Here we have to decode glyph indices back to chars. We do this
      // by tedious and ineffective iteration.
      //
      ranges = malloc( GetFontUnicodeRanges( hdc, 0 ) );
      GetFontUnicodeRanges( hdc, ranges );

      // Render up all available chars into one ridiculously big string

      allChars = malloc( ( ranges->cGlyphsSupported ) * sizeof( WCHAR ) );
      allIndices = malloc( ( ranges->cGlyphsSupported ) * sizeof( WORD ) );

      ptr = allChars;

      for( x = 0; x < ranges->cRanges; ++x )
      {
        WCHAR c = ranges->ranges[ x ].wcLow;
        unsigned y = ranges->ranges[ x ].cGlyphs;

        while( y-- )
          *ptr++ = c++;
      }

      // Amazing. Now get glyph indices for this one nice string.

      GetGlyphIndicesW( hdc, allChars, ranges->cGlyphsSupported, allIndices,
                        GGI_MARK_NONEXISTING_GLYPHS );

      // Fascinating. Now translate our original input string back into
      // its readable form.

      restoredString = malloc( cbString * sizeof( WCHAR ) );

      for( x = 0; x < cbString; ++x )
      {
        unsigned y;
        WORD idx = lpszString[ x ];

        for( y = 0; y < ranges->cGlyphsSupported; ++y )
          if ( allIndices[ y ] == idx )
          {
            restoredString[ x ] = allChars[ y ];
            break;
          }
        if ( y == ranges->cGlyphsSupported )
        {
          // Not found
          restoredString[ x ] = L'?';
        }
      }

      // And we're done.

      free( allIndices );
      free( allChars );
      free( ranges );

      IsInsidePointW( hdc, nXStart, nYStart, restoredString, cbString );

      free( restoredString );
    }
    else
      IsInsidePointW(hdc, nXStart, nYStart, lpszString, cbString);
  }
	return ExtTextOutWNextHook(hdc, nXStart, nYStart, fuOptions, lprc, lpszString, cbString, lpDx);
}

static void InstallTextOutHooks()
{
	HookAPI("gdi32.dll", "TextOutA", (PROC)TextOutACallbackProc, (PROC*)&TextOutANextHook);
	HookAPI("gdi32.dll", "TextOutW", (PROC)TextOutWCallbackProc, (PROC*)&TextOutWNextHook);
	HookAPI("gdi32.dll", "ExtTextOutA", (PROC)ExtTextOutACallbackProc, (PROC*)&ExtTextOutANextHook);
	HookAPI("gdi32.dll", "ExtTextOutW", (PROC)ExtTextOutWCallbackProc, (PROC*)&ExtTextOutWNextHook);
}

static void UninstallTextOutHooks()
{
	if (TextOutANextHook)
		HookAPI("gdi32.dll", "TextOutA", (PROC)TextOutANextHook, NULL);
	if (TextOutWNextHook)
		HookAPI("gdi32.dll", "TextOutW", (PROC)TextOutWNextHook, NULL);
	if (ExtTextOutANextHook)
		HookAPI("gdi32.dll", "ExtTextOutA", (PROC)ExtTextOutANextHook, NULL);
	if (ExtTextOutWNextHook)
		HookAPI("gdi32.dll", "ExtTextOutW", (PROC)ExtTextOutWNextHook, NULL);
}

DLLIMPORT void __gdGetWord (TCurrentMode *P)
{
	TCHAR wClassName[64];
	TKnownWndClass WndClass;
	char *p;

	if (GetClassName(P->WND, wClassName, sizeof(wClassName) / sizeof(TCHAR))==0)
		wClassName[0] = '\0';
	WndClass = GetWindowType(P->WND, wClassName);
	p = TryGetWordFromAnyWindow(WndClass, P->WND, P->Pt, &(P->BeginPos));
	if (p) {
	    P->WordLen = strlen(p);
		strcpy(P->MatchedWord, p);
		free(p);
	} else {
		P->WordLen = 0;
	}
}


BOOL APIENTRY DllMain (HINSTANCE hInst     /* Library instance handle. */ ,
                       DWORD reason        /* Reason this function is being called. */ ,
                       LPVOID reserved     /* Not used. */ )
{
    switch (reason)
    {
      case DLL_PROCESS_ATTACH:
			//ThTypes_Init();
			InstallTextOutHooks();
        break;

      case DLL_PROCESS_DETACH:
			UninstallTextOutHooks();
			//Thtypes_End();
        break;

      case DLL_THREAD_ATTACH:
        break;

      case DLL_THREAD_DETACH:
        break;
    }

    /* Returns TRUE on success, FALSE on failure */
    return TRUE;
}
