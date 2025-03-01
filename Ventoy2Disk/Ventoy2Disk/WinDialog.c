/******************************************************************************
 * WinDialog.c
 *
 * Copyright © 2017-2019 Pete Batard <pete@akeo.ie>
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <Windows.h>
#include <Shlobj.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <commctrl.h>
#include "resource.h"
#include "Language.h"
#include "Ventoy2Disk.h"
#include "DiskService.h"
#include "VentoyJson.h"

HINSTANCE g_hInst;

BOOL g_InvalidClusterSize = FALSE;
BOOL g_SecureBoot = TRUE;
HWND g_DialogHwnd;
HWND g_ComboxHwnd;
HWND g_LocalClusterTipHwnd;
HWND g_DiskClusterTipHwnd;
HWND g_StaticLocalVerHwnd;
HWND g_StaticDiskVerHwnd;
HWND g_StaticLocalStyleHwnd;
HWND g_StaticDiskStyleHwnd;
HWND g_StaticLocalFsHwnd;
HWND g_StaticDevFsHwnd;
HWND g_BtnInstallHwnd;
HWND g_StaticDevHwnd;
HWND g_StaticLocalHwnd;
HWND g_StaticDiskHwnd;
HWND g_BtnUpdateHwnd;
HWND g_ProgressBarHwnd;
HWND g_StaticStatusHwnd;
HWND g_LocalIconSecureHwnd;
HWND g_DiskIconSecureHwnd;
HANDLE g_ThreadHandle = NULL;

HFONT g_language_normal_font = NULL;
HFONT g_language_bold_font = NULL;

int g_cur_part_style = 0; // 0:MBR  1:GPT
int g_language_count = 0;
int g_cur_lang_id = 0;
VENTOY_LANGUAGE *g_language_data = NULL;
VENTOY_LANGUAGE *g_cur_lang_data = NULL;

static const char* current_arch_string(void)
{
#if (defined VTARCH_X86)
    return "X86";
#elif (defined VTARCH_X64)
    return "X64"; 
#elif (defined VTARCH_ARM)
    return "ARM";
#elif (defined VTARCH_ARM64)
    return "ARM64"; 
#else
    return "XXX";
#endif
}

static int LoadCfgIni(void)
{
	int value;

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("PartStyle"), 0, VENTOY_CFG_INI);
    if (value == 1)
    {
        g_cur_part_style = 1;
    }

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("ShowAllDevice"), 0, VENTOY_CFG_INI);
    if (value == 1)
    {
        g_FilterUSB = 0;
    }

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("MainPartFs"), 0, VENTOY_CFG_INI);
    if (value == 1 || value == 2 || value == 3)
    {
        SetVentoyFsType(value);
        Log("Set Ventoy FS Type %d", value);
    }

    value = GetPrivateProfileInt(TEXT("Ventoy"), TEXT("ClusterSize"), -1, VENTOY_CFG_INI);
    if (value >= 0)
    {
        SetClusterSize(value);        
        Log("Set Ventoy FS ClusterSize %d", value);
    }

	return 0;
}

static int WriteCfgIni(void)
{
    WCHAR *CfgBuf = NULL;
    WORD UTFHdr = 0xFEFF;
    int charcount = 0;
    FILE *fp = NULL;


    fopen_s(&fp, VENTOY_CFG_INI_A, "wb+");
    if (fp == NULL)
    {
        return 1;
    }

    CfgBuf = (WCHAR *)malloc(1024 * 64);
    if (CfgBuf == NULL)
    {
        fclose(fp);
        return 1;
    }

    charcount = swprintf_s(CfgBuf, 1024 * 64 / sizeof(WCHAR),
        L"[Ventoy]\r\n"
        L"Language=%s\r\n"
        L"PartStyle=%d\r\n"
        L"ShowAllDevice=%d\r\n"
        L"MainPartFs=%d\r\n"
        L"ClusterSize=%d\r\n"
        ,
        g_language_data[g_cur_lang_id].Name,
        g_cur_part_style,
        1 - g_FilterUSB,
        GetVentoyFsType(),
        GetClusterSize());

    fwrite(&UTFHdr, 1, sizeof(UTFHdr), fp);
    fwrite(CfgBuf, 1, charcount * sizeof(WCHAR), fp);
    fclose(fp);

    free(CfgBuf);

//	WritePrivateProfileString(TEXT("Ventoy"), TEXT("Language"), g_language_data[g_cur_lang_id].Name, VENTOY_CFG_INI);

//  swprintf_s(TmpBuf, 128, TEXT("%d"), g_SecureBoot);
//  WritePrivateProfileString(TEXT("Ventoy"), TEXT("SecureBoot"), TmpBuf, VENTOY_CFG_INI);

	return 0;
}

void SetProgressBarPos(int Pos)
{
    int i = 0;
    int flag = 0;
    int index = 0;
    const TCHAR* StrPos = NULL;
    const TCHAR* StrStatus = NULL;
    CHAR Ratio[64] = { 0 };
    WCHAR wRatio[256] = { 0 };

    if (g_CLI_Mode)
    {
        CLI_UpdatePercent(Pos);
        return;
    }

    if (Pos >= PT_FINISH)
    {
        Pos = PT_FINISH;
    }

    SendMessage(g_ProgressBarHwnd, PBM_SETPOS, Pos, 0);

    StrStatus = _G(STR_STATUS);
    if (StrStatus)
    {
        for (StrPos = StrStatus; *StrPos; StrPos++)
        {
            if (index < 200)
            {
                wRatio[index] = *StrPos;
            }
            
            index++;            
            if (*StrPos == L'-')
            {
                flag = 1;
                break;
            }
        }
    }
    
    if (flag && index < 200)
    {        
        safe_sprintf(Ratio, " %.0lf%%", Pos * 100.0 / PT_FINISH);
        for (i = 0; index < 200 && Ratio[i]; i++, index++)
        {
            wRatio[index] = Ratio[i];
        }

        SetWindowTextW(g_StaticStatusHwnd, wRatio);
    }
    else
    {
        safe_sprintf(Ratio, "Status - %.0lf%%", Pos * 100.0 / PT_FINISH);
        SetWindowTextA(g_StaticStatusHwnd, Ratio);
    }
}

static void UpdateLocalVentoyVersion()
{
    CHAR Ver[128];

	safe_sprintf(Ver, "%s", GetLocalVentoyVersion());
	SetWindowTextA(g_StaticLocalVerHwnd, Ver);

	SetWindowTextA(g_StaticLocalStyleHwnd, g_cur_part_style ? "GPT" : "MBR");
}

static int UpdateClusterTipMsg(int toolID, HWND hDlg, HWND hWndTip, WCHAR* Msg)
{
    HWND hwndTool = GetDlgItem(hDlg, toolID);

    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = hDlg;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR)hwndTool;
    toolInfo.lpszText = Msg;

    SendMessage(hWndTip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
    return 0;
}

static void OnComboxSelChange(HWND hCombox)
{
    int nCurSelected;
    PHY_DRIVE_INFO *CurDrive = NULL;
    HMENU SubMenu;    
    HMENU hMenu = GetMenu(g_DialogHwnd);
    WCHAR Tip[256];

    UpdateLocalVentoyVersion();
    SetWindowTextA(g_StaticDiskVerHwnd, ""); g_InvalidClusterSize = FALSE;
	SetWindowTextA(g_StaticDiskStyleHwnd, "");
    SetWindowTextA(g_StaticDevFsHwnd, "");    
    ShowWindow(g_DiskIconSecureHwnd, SW_HIDE);
    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);
    UpdateClusterTipMsg(IDC_STATIC_DEV_FS, g_DialogHwnd, g_DiskClusterTipHwnd, L"");

    SubMenu = GetSubMenu(hMenu, 0);
    ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_BYPOSITION | MF_STRING | MF_DISABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));    
    ModifyMenu(SubMenu, OPT_SUBMENU_PART_RESIZE, MF_BYPOSITION | MF_STRING | MF_DISABLED, VTOY_MENU_PART_RESIZE, _G(STR_MENU_PART_RESIZE));

    if (g_PhyDriveCount > 0)
    {
        nCurSelected = (int)SendMessage(hCombox, CB_GETCURSEL, 0, 0);
        if (CB_ERR != nCurSelected)
        {
            CurDrive = GetPhyDriveInfoById(nCurSelected);
        }
    }
    
    if (CurDrive)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_BYPOSITION | MF_STRING | MF_ENABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
        SetWindowTextA(g_StaticDiskVerHwnd, CurDrive->VentoyVersion); g_InvalidClusterSize = FALSE;
        SetWindowTextA(g_StaticDevFsHwnd, CurDrive->VentoyFsType);

        FormatClusterSizeTip(CurDrive->VentoyFsClusterSize, Tip, 256);
        UpdateClusterTipMsg(IDC_STATIC_DEV_FS, g_DialogHwnd, g_DiskClusterTipHwnd, Tip);

		if (CurDrive->VentoyVersion[0])
		{
            if (_stricmp(CurDrive->VentoyFsType, "EXFAT") == 0 ||
                _stricmp(CurDrive->VentoyFsType, "FAT32") == 0 ||
                _stricmp(CurDrive->VentoyFsType, "NTFS") == 0)
            {
                if (CurDrive->VentoyFsClusterSize < 2048)
                {
                    g_InvalidClusterSize = TRUE;
                }
            }

			SetWindowTextA(g_StaticDiskStyleHwnd, CurDrive->PartStyle ? "GPT" : "MBR");

            Log("Combox select change, update secure boot option: %u %u", g_SecureBoot, CurDrive->SecureBootSupport);
            g_SecureBoot = CurDrive->SecureBootSupport;

            if (g_SecureBoot)
            {
                ShowWindow(g_DiskIconSecureHwnd, SW_NORMAL);
                ShowWindow(g_LocalIconSecureHwnd, SW_NORMAL);

                CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
            }
            else
            {
                ShowWindow(g_DiskIconSecureHwnd, SW_HIDE);
                ShowWindow(g_LocalIconSecureHwnd, SW_HIDE);

                CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
            }
		}
		else
		{
            ModifyMenu(SubMenu, OPT_SUBMENU_PART_RESIZE, MF_BYPOSITION | MF_STRING | MF_ENABLED, VTOY_MENU_PART_RESIZE, _G(STR_MENU_PART_RESIZE));
			
            SetWindowTextA(g_StaticDiskStyleHwnd, "");
            SetWindowTextA(g_StaticDevFsHwnd, "");

            Log("Not ventoy disk, set secure boot option");
            g_SecureBoot = TRUE;            
            ShowWindow(g_DiskIconSecureHwnd, SW_HIDE);
            ShowWindow(g_LocalIconSecureHwnd, SW_NORMAL);
            CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
		}
		
		
        if (g_ForceOperation == 0)
        {
            if (CurDrive->VentoyVersion[0])
            {
                //only can update
                EnableWindow(g_BtnInstallHwnd, FALSE);
                EnableWindow(g_BtnUpdateHwnd, TRUE);
            }
            else
            {
                //only can install
                EnableWindow(g_BtnInstallHwnd, TRUE);
                EnableWindow(g_BtnUpdateHwnd, FALSE);
            }
        }
        else
        {
            EnableWindow(g_BtnInstallHwnd, TRUE);
			if (CurDrive->VentoyVersion[0])
			{
				EnableWindow(g_BtnUpdateHwnd, TRUE);
			}
        }
    }
    
    InvalidateRect(g_DialogHwnd, NULL, TRUE);
    UpdateWindow(g_DialogHwnd);
}

static void UpdateReservedPostfix(void)
{
	int Space = 0;
	WCHAR Buf[128] = { 0 };
	
	Space = GetReservedSpaceInMB();

	if (Space <= 0)
	{
		SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DEV), _G(STR_DEVICE));
	}
	else
	{
		if (Space % 1024 == 0)
		{
			wsprintf(Buf, L"%s  [ -%dGB ]", _G(STR_DEVICE), Space / 1024);
		}
		else
		{
			wsprintf(Buf, L"%s  [ -%dMB ]", _G(STR_DEVICE), Space);
		}
		SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DEV), Buf);
	}
}

static void UpdateItemString(int defaultLangId)
{
	int i;
    UINT State;
	HMENU SubMenu;
	HFONT hLangFont, hBoldFont;
    WCHAR Str[256];
	HMENU hMenu = GetMenu(g_DialogHwnd);

	g_cur_lang_id = defaultLangId;
	g_cur_lang_data = g_language_data + defaultLangId;

	hBoldFont = hLangFont = CreateFont(g_language_data[defaultLangId].FontSize, 0, 0, 0, 700, FALSE, FALSE, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH, g_language_data[defaultLangId].FontFamily);

	hLangFont = CreateFont(g_language_data[defaultLangId].FontSize, 0, 0, 0, 400, FALSE, FALSE, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH, g_language_data[defaultLangId].FontFamily);

	SendMessage(g_BtnInstallHwnd, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
	SendMessage(g_BtnUpdateHwnd, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

	SendMessage(g_StaticStatusHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticLocalHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticDiskHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_StaticDevHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);
	SendMessage(g_DialogHwnd, WM_SETFONT, (WPARAM)hLangFont, TRUE);

    SendMessage(GetDlgItem(g_DialogHwnd, IDC_SYSLINK2), WM_SETFONT, (WPARAM)hLangFont, TRUE);

    g_language_normal_font = hLangFont;
    g_language_bold_font = hBoldFont;

	ModifyMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, 0, _G(STR_MENU_OPTION));

	UpdateReservedPostfix();

	SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_LOCAL), _G(STR_LOCAL_VER));
	SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DISK), _G(STR_DISK_VER));
	SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));

	SetWindowText(g_BtnInstallHwnd, _G(STR_INSTALL));
	SetWindowText(g_BtnUpdateHwnd, _G(STR_UPDATE));

    swprintf_s(Str, 200, L"<a>%ls</a>", _G(STR_DONATE));
    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_SYSLINK2), Str);

	SubMenu = GetSubMenu(hMenu, 0);
	if (g_SecureBoot)
	{
        ShowWindow(g_LocalIconSecureHwnd, SW_NORMAL);
		ModifyMenu(SubMenu, OPT_SUBMENU_SECURE_BOOT, MF_BYPOSITION | MF_STRING | MF_CHECKED, 0, _G(STR_MENU_SECURE_BOOT));
	}
	else
	{
        ShowWindow(g_LocalIconSecureHwnd, SW_HIDE);
        ModifyMenu(SubMenu, OPT_SUBMENU_SECURE_BOOT, MF_BYPOSITION | MF_STRING | MF_UNCHECKED, 0, _G(STR_MENU_SECURE_BOOT));
	}
    
    ModifyMenu(SubMenu, OPT_SUBMENU_PART_STYLE, MF_STRING | MF_BYPOSITION, VTOY_MENU_PART_STYLE, _G(STR_MENU_PART_STYLE));
    ModifyMenu(SubMenu, OPT_SUBMENU_PART_CFG, MF_STRING | MF_BYPOSITION, VTOY_MENU_PART_CFG, _G(STR_MENU_PART_CFG));

    State = GetMenuState(SubMenu, VTOY_MENU_CLEAN, MF_BYCOMMAND);
    if (State & MF_DISABLED)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_STRING | MF_BYPOSITION | MF_DISABLED, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_CLEAR, MF_STRING | MF_BYPOSITION, VTOY_MENU_CLEAN, _G(STR_MENU_CLEAR));
    }

    State = GetMenuState(SubMenu, VTOY_MENU_PART_RESIZE, MF_BYCOMMAND);
    if (State & MF_DISABLED)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_PART_RESIZE, MF_STRING | MF_BYPOSITION | MF_DISABLED, VTOY_MENU_PART_RESIZE, _G(STR_MENU_PART_RESIZE));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_PART_RESIZE, MF_STRING | MF_BYPOSITION, VTOY_MENU_PART_RESIZE, _G(STR_MENU_PART_RESIZE));
    }

    if (g_FilterUSB == 0)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
    }

#if VTSI_SUPPORT
    if (g_WriteImage == 1)
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
    }
    else
    {
        ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
    }
#endif

	ShowWindow(g_DialogHwnd, SW_HIDE);
	ShowWindow(g_DialogHwnd, SW_NORMAL);

	//Update check
	for (i = 0; i < g_language_count; i++)
	{
		CheckMenuItem(hMenu, VTOY_MENU_LANGUAGE_BEGIN | i, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
	}
	CheckMenuItem(hMenu, VTOY_MENU_LANGUAGE_BEGIN | defaultLangId, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
}

static int ventoy_compare_language(VENTOY_LANGUAGE *lang1, VENTOY_LANGUAGE *lang2)
{
	if (lstrcmp(lang1->Name, TEXT("Chinese Simplified (简体中文)")) == 0)
	{
		return -1;
	}
	else if (lstrcmp(lang2->Name, TEXT("Chinese Simplified (简体中文)")) == 0)
	{
		return 1;
	}

	return lstrcmp(lang1->Name, lang2->Name);
}

static void ventoy_sort_language(VENTOY_LANGUAGE *LangData, int LangCount)
{
	int i, j;
	VENTOY_LANGUAGE *tmpdata = NULL;

	tmpdata = (VENTOY_LANGUAGE *)malloc(sizeof(VENTOY_LANGUAGE));
    if (tmpdata == NULL)
    {
        return;
    }

	for (i = 0; i < LangCount; i++)
	{
		for (j = i + 1; j < LangCount; j++)
		{
			if (ventoy_compare_language(LangData + j, LangData + i) < 0)
			{
				memcpy(tmpdata, LangData + i, sizeof(VENTOY_LANGUAGE));
				memcpy(LangData + i, LangData + j, sizeof(VENTOY_LANGUAGE));
				memcpy(LangData + j, tmpdata, sizeof(VENTOY_LANGUAGE));
			}
		}
	}

	free(tmpdata);
}

static void LoadLanguageFromIni(void)
{
    int i, j, k;
    WCHAR *SectionName = NULL;
    WCHAR *SectionNameBuf = NULL;
    VENTOY_LANGUAGE *cur_lang = NULL;
    WCHAR Language[64];
    WCHAR TmpBuf[64];

    swprintf_s(Language, 64, L"StringDefine");
    for (i = 0; i < STR_ID_MAX; i++)
    {
        swprintf_s(TmpBuf, 64, L"%d", i);
        GET_INI_STRING(Language, TmpBuf, g_language_data[0].StrId[i]);
    }

    SectionNameBuf = (WCHAR *)malloc(SIZE_1MB);
    if (SectionNameBuf == NULL)
    {
        return;
    }

    GetPrivateProfileSectionNames(SectionNameBuf, SIZE_1MB / sizeof(WCHAR), VENTOY_LANGUAGE_INI);

    cur_lang = g_language_data;
    for (SectionName = SectionNameBuf; *SectionName && g_language_count < VENTOY_MAX_LANGUAGE; SectionName += (lstrlen(SectionName) + 1))
    {
        if (lstrlen(SectionName) < 9 || memcmp(L"Language-", SectionName, 9 * sizeof(WCHAR)))
        {
            continue;
        }

        // "Language-"
        lstrcpy(cur_lang->Name, SectionName + 9);

        GET_INI_STRING(SectionName, TEXT("FontFamily"), cur_lang->FontFamily);
        cur_lang->FontSize = GetPrivateProfileInt(SectionName, TEXT("FontSize"), 10, VENTOY_LANGUAGE_INI);

        for (j = 0; j < STR_ID_MAX; j++)
        {
            GET_INI_STRING(SectionName, g_language_data[0].StrId[j], cur_lang->MsgString[j]);

            for (k = 0; cur_lang->MsgString[j][k] && cur_lang->MsgString[j][k + 1]; k++)
            {
                if (cur_lang->MsgString[j][k] == '#' && cur_lang->MsgString[j][k + 1] == '@')
                {
                    cur_lang->MsgString[j][k] = '\r';
                    cur_lang->MsgString[j][k + 1] = '\n';
                }
            }
        }

        g_language_count++;
        cur_lang++;
    }
    free(SectionNameBuf);

    Log("Total %d languages ...", g_language_count);
}

static void UTF8ToWString(const char *str, WCHAR *buf)
{
    int wcsLen;
    int len = (int)strlen(str);

    wcsLen = MultiByteToWideChar(CP_UTF8, 0, str, len, NULL, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, len, buf, wcsLen);
}

static void LoadLanguageFromJson(void)
{
    int k;
    int ret;
    int index = 0;
    int len = 0;
    char *buf = NULL;
    VTOY_JSON *json = NULL;
    VTOY_JSON *node = NULL;
    VTOY_JSON *cur = NULL;
    VENTOY_LANGUAGE *cur_lang = NULL;

    ReadWholeFileToBuf(VENTOY_LANGUAGE_JSON_A, 4, &buf, &len);
    buf[len] = 0;

    json = vtoy_json_create();

    ret = vtoy_json_parse(json, buf);

    Log("language json file len:%d json parse:%d", len, ret);

    cur_lang = g_language_data;
    for (node = json->pstChild; node; node = node->pstNext)
    {
        cur = node->pstChild;
        index = 0;
        while (cur)
        {
            if (strncmp(cur->pcName, "name", 4) == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->Name);
            }
            else if (strcmp(cur->pcName, "FontFamily") == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->FontFamily);
            }
            else if (strcmp(cur->pcName, "FontSize") == 0)
            {
                cur_lang->FontSize = (int)cur->unData.lValue;
            }
            else if (strncmp(cur->pcName, "STR_", 4) == 0)
            {
                UTF8ToWString(cur->unData.pcStrVal, cur_lang->MsgString[index]);

                for (k = 0; cur_lang->MsgString[index][k] && cur_lang->MsgString[index][k + 1]; k++)
                {
                    if (cur_lang->MsgString[index][k] == '#' && cur_lang->MsgString[index][k + 1] == '@')
                    {
                        cur_lang->MsgString[index][k] = '\r';
                        cur_lang->MsgString[index][k + 1] = '\n';
                    }
                }

                index++;
            }
            cur = cur->pstNext;
        }

        cur_lang++;
        g_language_count++;
    }

    vtoy_json_destroy(json);
    free(buf);

    Log("Total %d languages ...", g_language_count);
}


static void LanguageInit(void)
{
    int i;
	int id = -1, DefaultId = -1;
	WCHAR TmpBuf[256];
	LANGID LangId = GetSystemDefaultUILanguage();
	HMENU SubMenu;
	HMENU hMenu = GetMenu(g_DialogHwnd);

    SubMenu = GetSubMenu(hMenu, 0);
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_PART_CFG, TEXT("yyy"));
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_ALL_DEV, TEXT("USB Device Only")); 

#if VTSI_SUPPORT    
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_VTSI, TEXT("Generate VTSI File"));    
#endif

    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_CLEAN, TEXT("yyy"));       
    AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_PART_RESIZE, TEXT("yyy"));
    
    if (g_cur_part_style)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_CHECKED);
    }
    else
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_UNCHECKED);
    }

	SubMenu = GetSubMenu(hMenu, 1);
	DeleteMenu(SubMenu, 0, MF_BYPOSITION);

	g_language_data = (VENTOY_LANGUAGE *)malloc(sizeof(VENTOY_LANGUAGE)* VENTOY_MAX_LANGUAGE);
    if (g_language_data == NULL)
    {
        return;
    }

	memset(g_language_data, 0, sizeof(VENTOY_LANGUAGE)* VENTOY_MAX_LANGUAGE);

    if (IsFileExist(VENTOY_LANGUAGE_JSON_A))
    {
        Log("Load languages from json file ...");
        LoadLanguageFromJson(); 
    }
    else
    {
        Log("Load languages from ini file ...");
        LoadLanguageFromIni();
    }

	ventoy_sort_language(g_language_data, g_language_count);

	if (MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED) == LangId)
	{
		DefaultId = 0;
	}

	memset(TmpBuf, 0, sizeof(TmpBuf));
	GetPrivateProfileString(TEXT("Ventoy"), TEXT("Language"), TEXT("#"), TmpBuf, 256, VENTOY_CFG_INI);

	for (i = 0; i < g_language_count; i++)
	{
		AppendMenu(SubMenu, MF_STRING | MF_BYCOMMAND, VTOY_MENU_LANGUAGE_BEGIN | i, g_language_data[i].Name);
		
		if (id < 0 && lstrcmp(g_language_data[i].Name, TmpBuf) == 0)
		{
			id = i;
		}

		if (DefaultId < 0 && lstrcmp(g_language_data[i].Name, TEXT("English (English)")) == 0)
		{
			DefaultId = i;
		}
	}

	if (id < 0)
	{
		id = DefaultId;
	}



	UpdateItemString(id);
}

void InitComboxCtrl(HWND hWnd, int PhyDrive)
{
    int SizeGB = 0;
	WPARAM n = 0;
	WPARAM nIndex = 0;
    DWORD i, j;
    HANDLE hCombox;
    CHAR Drive[16];
    CHAR Letter[128];
    CHAR DeviceName[256];

    hCombox = GetDlgItem(hWnd, IDC_COMBO1);

    // delete all items
    SendMessage(hCombox, CB_RESETCONTENT, 0, 0);
    
    //Fill device combox
    for (i = 0; i < g_PhyDriveCount; i++)
    {
        if (g_PhyDriveList[i].Id < 0)
        {
            continue;
        }

        if (g_PhyDriveList[i].DriveLetters[0])
        {
            safe_sprintf(Letter, "%C: ", g_PhyDriveList[i].DriveLetters[0]);
            for (j = 1; j < sizeof(g_PhyDriveList[i].DriveLetters) / sizeof(CHAR); j++)
            {
                if (g_PhyDriveList[i].DriveLetters[j] == 0)
                {
                    break;
                }
                safe_sprintf(Drive, "%C: ", g_PhyDriveList[i].DriveLetters[j]);
                strcat_s(Letter, sizeof(Letter), Drive);
            }
        }
        else
        {
            Letter[0] = 0;
        }

        SizeGB = GetHumanReadableGBSize(g_PhyDriveList[i].SizeInBytes);

        if ((SizeGB % 1024) == 0)
        {
            safe_sprintf(DeviceName, "%s[%dTB] %s %s",
                Letter,
                SizeGB / 1024,
                g_PhyDriveList[i].VendorId,
                g_PhyDriveList[i].ProductId
            );
        }
        else
        {
            safe_sprintf(DeviceName, "%s[%dGB] %s %s",
                Letter,
                SizeGB,
                g_PhyDriveList[i].VendorId,
                g_PhyDriveList[i].ProductId
            );
        }
        
        SendMessageA(hCombox, CB_ADDSTRING, 0, (LPARAM)DeviceName);

		if (g_PhyDriveList[i].PhyDrive == PhyDrive)
		{
			nIndex = n;
		}
		n++;
    }

	SendMessage(hCombox, CB_SETCURSEL, nIndex, 0);
    OnComboxSelChange(g_ComboxHwnd);
}

HWND CreateClusterToolTip(int toolID, HWND hDlg, PTSTR pszText)
{
    if (!toolID || !hDlg || !pszText)
    {
        return NULL;
    }

    // Get the window of the tool.
    HWND hwndTool = GetDlgItem(hDlg, toolID);

    // Create the tooltip. g_hInst is the global instance handle.
    HWND hwndTip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        hDlg, NULL,
        g_hInst, NULL);

    if (!hwndTool || !hwndTip)
    {
        return (HWND)NULL;
    }

    // Associate the tooltip with the tool.
    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = hDlg;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR)hwndTool;
    toolInfo.lpszText = pszText;

    SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
    //SendMessage(hwndTip, TTM_ACTIVATE, TRUE, NULL);

    return hwndTip;
}

static BOOL InitDialog(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	//HFONT hStyleFont;
    HFONT hDlgFont;
    HFONT hDlgBoldFont;
    HFONT hStaticFont;	
    HICON hIcon;
    HICON hSecureIcon;
    CHAR WinText[128];
    CHAR WinPath[MAX_PATH] = { 0 };

    g_DialogHwnd = hWnd;
    g_ComboxHwnd = GetDlgItem(hWnd, IDC_COMBO1);
    g_StaticLocalVerHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_VER);
    g_StaticDiskVerHwnd = GetDlgItem(hWnd, IDC_STATIC_DISK_VER);
	g_StaticLocalStyleHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_STYLE);
	g_StaticDiskStyleHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV_STYLE);
    g_StaticLocalFsHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL_FS);
    g_StaticDevFsHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV_FS);

    g_BtnInstallHwnd = GetDlgItem(hWnd, IDC_BUTTON4);

	g_StaticDevHwnd = GetDlgItem(hWnd, IDC_STATIC_DEV);
	g_StaticLocalHwnd = GetDlgItem(hWnd, IDC_STATIC_LOCAL);
	g_StaticDiskHwnd = GetDlgItem(hWnd, IDC_STATIC_DISK);

    g_LocalClusterTipHwnd = CreateClusterToolTip(IDC_STATIC_LOCAL_FS, hWnd, L"");
    g_DiskClusterTipHwnd = CreateClusterToolTip(IDC_STATIC_DEV_FS, hWnd, L"");

    g_LocalIconSecureHwnd = GetDlgItem(hWnd, IDC_ICON_LOCAL_SECURE);
    g_DiskIconSecureHwnd = GetDlgItem(hWnd, IDC_ICON_DISK_SECURE);

    hSecureIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_ICON4), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    SendMessage(g_LocalIconSecureHwnd, STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hSecureIcon);
    SendMessage(g_DiskIconSecureHwnd, STM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hSecureIcon);
    ShowWindow(g_LocalIconSecureHwnd, SW_HIDE);
    ShowWindow(g_DiskIconSecureHwnd, SW_HIDE);


    g_BtnUpdateHwnd = GetDlgItem(hWnd, IDC_BUTTON3);
    g_ProgressBarHwnd = GetDlgItem(hWnd, IDC_PROGRESS1);
    g_StaticStatusHwnd = GetDlgItem(hWnd, IDC_STATIC_STATUS);

    hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON1));
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);


    SendDlgItemMessage(hWnd, IDC_COMMAND1, BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON2)));

    SendMessage(g_ProgressBarHwnd, PBM_SETRANGE, (WPARAM)0, (LPARAM)(MAKELPARAM(0, PT_FINISH)));
    PROGRESS_BAR_SET_POS(PT_START);

	SetMenu(hWnd, LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU1)));

    LanguageInit();

    sprintf_s(WinText, sizeof(WinText), "Ventoy2Disk  %s", current_arch_string());
    SetWindowTextA(hWnd, WinText);

    // Set static text & font 
    hStaticFont = CreateFont(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, 0,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH&FF_SWISS, TEXT("Courier New"));
	SendMessage(g_StaticLocalVerHwnd, WM_SETFONT, (WPARAM)hStaticFont, TRUE);
	SendMessage(g_StaticDiskVerHwnd, WM_SETFONT, (WPARAM)hStaticFont, TRUE);

#if 0
	hStyleFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
		ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH&FF_SWISS, TEXT("Courier New"));
	SendMessage(g_StaticLocalStyleHwnd, WM_SETFONT, (WPARAM)hStyleFont, TRUE);
	SendMessage(g_StaticDiskStyleHwnd, WM_SETFONT, (WPARAM)hStyleFont, TRUE);
#endif

    SetWindowTextA(g_StaticLocalFsHwnd, GetVentoyFsName());
    UpdateClusterTipMsg(IDC_STATIC_LOCAL_FS, hWnd, g_LocalClusterTipHwnd, GetClusterSizeTip());

    InitComboxCtrl(hWnd, -1);

    SetFocus(g_ProgressBarHwnd);


    GetEnvironmentVariableA("SystemRoot", WinPath, MAX_PATH);
    strcat_s(WinPath, MAX_PATH, "\\Fonts\\couri.ttf");

    if (IsFileExist(WinPath))
    {
        Log("Courier New font <%s> exist OK.", WinPath);
    }
    else
    {
        Log("Courier New font <%s> does NOT exist.", WinPath);

        hDlgFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH & FF_SWISS, TEXT("Microsoft Yahe"));

        hDlgBoldFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, 0,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH & FF_SWISS, TEXT("Microsoft Yahe"));

        SendMessage(hWnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(g_StaticLocalStyleHwnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(g_StaticDiskStyleHwnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(g_StaticLocalFsHwnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(g_StaticDevFsHwnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(g_ComboxHwnd, WM_SETFONT, (WPARAM)hDlgFont, TRUE);

        SendMessage(g_BtnInstallHwnd, WM_SETFONT, (WPARAM)hDlgBoldFont, TRUE);
        SendMessage(g_BtnUpdateHwnd, WM_SETFONT, (WPARAM)hDlgBoldFont, TRUE);

        SendMessage(GetDlgItem(hWnd, IDC_SYSLINK1), WM_SETFONT, (WPARAM)hDlgFont, TRUE);
        SendMessage(GetDlgItem(hWnd, IDC_SYSLINK2), WM_SETFONT, (WPARAM)hDlgFont, TRUE);
    }

    AlertSuppressInit();

    return TRUE;
}

static DWORD WINAPI InstallVentoyThread(void* Param)
{
    int rc;
    int TryId = 1;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    SetAlertPromptHookEnable(TRUE);

    if (g_WriteImage)
    {
        rc = InstallVentoy2FileImage(pPhyDrive, g_cur_part_style);
    }
    else
    {
        rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);
        if (rc)
        {
            Log("This time install failed, clean disk by disk, wait 5s and retry...");
			DISK_CleanDisk(pPhyDrive->PhyDrive);

            Sleep(5000);

            Log("Now retry to install...");
            rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);

            if (rc)
            {
                Log("This time install failed, clean disk by diskpart, wait 10s and retry...");
                DSPT_CleanDisk(pPhyDrive->PhyDrive);

                Sleep(10000);

                Log("Now retry to install...");
                rc = InstallVentoy2PhyDrive(pPhyDrive, g_cur_part_style, TryId++);
            }
        }
    }

    if (rc == 0)
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, g_WriteImage ? _G(STR_VTSI_CREATE_SUCCESS) : _G(STR_INSTALL_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);

        if (g_WriteImage == 0)
        {
            safe_strcpy(pPhyDrive->VentoyVersion, GetLocalVentoyVersion());
            safe_strcpy(pPhyDrive->VentoyFsType, GetVentoyFsName());
            pPhyDrive->PartStyle = g_cur_part_style;
            pPhyDrive->SecureBootSupport = g_SecureBoot;
        }
    }
    else
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, g_WriteImage ? _G(STR_VTSI_CREATE_FAILED) : _G(STR_INSTALL_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }
    
	PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_LOCAL), _G(STR_LOCAL_VER));
    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DISK), _G(STR_DISK_VER));

    SetAlertPromptHookEnable(FALSE);

    return 0;
}

static DWORD WINAPI ClearVentoyThread(void* Param)
{
    int rc;
    UINT Drive = 0;
    CHAR DrvLetter = 0;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    rc = ClearVentoyFromPhyDrive(g_DialogHwnd, pPhyDrive, &DrvLetter);
    if (rc)
    {
        Log("This time clear failed, now wait and retry...");
        Sleep(10000);

        Log("Now retry to clear...");

        rc = ClearVentoyFromPhyDrive(g_DialogHwnd, pPhyDrive, &DrvLetter);
    }

    if (rc == 0)
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_CLEAR_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
        safe_strcpy(pPhyDrive->VentoyVersion, "");
        safe_strcpy(pPhyDrive->VentoyFsType, "");
        pPhyDrive->VentoyFsClusterSize = 0;
    }
    else
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_CLEAR_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }

    PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_LOCAL), _G(STR_LOCAL_VER));
    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DISK), _G(STR_DISK_VER));

    if (rc == 0 && DrvLetter > 0)
    {
        if (DrvLetter >= 'A' && DrvLetter <= 'Z')
        {
            Drive = DrvLetter - 'A';
        }
        else if (DrvLetter >= 'a' && DrvLetter <= 'z')
        {
            Drive = DrvLetter - 'a';
        }

        if (Drive > 0)
        {
            //SHFormatDrive(g_DialogHwnd, Drive, SHFMT_ID_DEFAULT, SHFMT_OPT_FULL);
        }
    }

    return 0;
}


static DWORD WINAPI UpdateVentoyThread(void* Param)
{
    int rc;
    int TryId = 1;
    PHY_DRIVE_INFO *pPhyDrive = (PHY_DRIVE_INFO *)Param;

    rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
	if (rc)
	{
		Log("This time update failed, now wait and retry...");
		Sleep(4000);

		//Try2
		Log("Now retry to update...");
        rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
		if (rc)
		{
			//Try3
			Sleep(1000);
			Log("Now retry to update...");
			rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
			if (rc)
			{
				//Try4 is dangerous ...
				Sleep(3000);
				Log("Now retry to update...");
				rc = UpdateVentoy2PhyDrive(pPhyDrive, TryId++);
			}
		}
	}

    if (rc == 0)
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_UPDATE_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
        safe_strcpy(pPhyDrive->VentoyVersion, GetLocalVentoyVersion());
        pPhyDrive->SecureBootSupport = g_SecureBoot;
    }
    else
    {
		PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_UPDATE_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }
    
	PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));
    OnComboxSelChange(g_ComboxHwnd);

    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_LOCAL), _G(STR_LOCAL_VER));
    SetWindowText(GetDlgItem(g_DialogHwnd, IDC_STATIC_DISK), _G(STR_DISK_VER));

    return 0;
}


static DWORD WINAPI PartResizeThread(void* Param)
{
	int rc;
    PHY_DRIVE_INFO* pPhyDrive = (PHY_DRIVE_INFO*)Param;

	rc = PartitionResizeForVentoy(pPhyDrive);
	if (rc == 0)
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_PART_RESIZE_SUCCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        PROGRESS_BAR_SET_POS(PT_FINISH);
        MessageBox(g_DialogHwnd, _G(STR_PART_RESIZE_FAILED), _G(STR_ERROR), MB_OK | MB_ICONERROR);
    }

    PROGRESS_BAR_SET_POS(PT_START);
    g_ThreadHandle = NULL;
    SetWindowText(g_StaticStatusHwnd, _G(STR_STATUS));

    OnComboxSelChange(g_ComboxHwnd);

    return 0;
}

static void OnInstallBtnClick(void)
{
    int nCurSel;
	int SpaceMB = 0;
	int SizeInMB = 0;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (g_WriteImage)
    {
        if (MessageBox(g_DialogHwnd, _G(STR_VTSI_CREATE_TIP), _G(STR_INFO), MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
    }
    else
    {
        if ((g_NoNeedInputYes == 0) && IsWindowEnabled(g_BtnUpdateHwnd))
        {
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG3), NULL, YesDialogProc);
            if (!g_InputYes)
            {
                return;
            }
        }

        if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }

        if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP2), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

	if (g_cur_part_style == 0 && pPhyDrive->SizeInBytes > 2199023255552ULL)
	{
		MessageBox(g_DialogHwnd, _G(STR_DISK_2TB_MBR_ERROR), _G(STR_ERROR), MB_OK | MB_ICONERROR);
		return;
	}

	SpaceMB = GetReservedSpaceInMB();
	SizeInMB = (int)(pPhyDrive->SizeInBytes / 1024 / 1024);
	Log("SpaceMB:%d SizeInMB:%d", SpaceMB, SizeInMB);

	if (SizeInMB <= SpaceMB || (SizeInMB - SpaceMB) <= (VENTOY_EFI_PART_SIZE / SIZE_1MB))
	{
		MessageBox(g_DialogHwnd, _G(STR_SPACE_VAL_INVALID), _G(STR_ERROR), MB_OK | MB_ICONERROR);
		Log("Invalid space value ...");
		return;
	}

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, InstallVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void OnRefreshBtnClick(HWND hWnd)
{
	int nCurSel;
	int PhyDrive = -1;
	PHY_DRIVE_INFO *pPhyDrive = NULL;

    Log("#### Now Refresh PhyDrive ####");

	nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
	if (CB_ERR != nCurSel)
	{
		pPhyDrive = GetPhyDriveInfoById(nCurSel);
		if (pPhyDrive)
		{
			PhyDrive = pPhyDrive->PhyDrive;
			Log("Current combox selection is PhyDrive%d", PhyDrive);
		}
	}

    Ventoy2DiskDestroy();
    Ventoy2DiskInit();
	InitComboxCtrl(hWnd, PhyDrive);
}

static void OnUpdateBtnClick(void)
{
    int nCurSel;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (MessageBox(g_DialogHwnd, _G(STR_UPDATE_TIP), _G(STR_INFO), MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return;
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, UpdateVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

BOOL PartResizePreCheck(PHY_DRIVE_INFO** ppPhyDrive)
{
    int i;
	int Index;
    int Count;
    int nCurSel;
	int PartStyle;
    BOOL bRet;
	BOOL FindFlag = FALSE;
    BOOL bCheck = FALSE;
    UINT64 FreeSize, Offset;
	UINT64 Part1Start, Part1End, NextPartStart;
    CHAR Drive[8] = { 0 };
    CHAR FsName[MAX_PATH];
	CHAR WinDir[MAX_PATH];
    HANDLE hDrive = INVALID_HANDLE_VALUE;
    VTOY_GPT_INFO* pGPT = NULL;
    PHY_DRIVE_INFO* pPhyDrive = NULL; 
    DWORD dwSize;
    DWORD SectorsPerCluster, BytesPerSector, NumberOfFreeClusters, TotalNumberOfClusters;
    GUID ZeroGuid = { 0 };
    CHAR VolumeGuid[128];

    Log("PartResizePreCheck ...");

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        goto out;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        goto out;
    }


	pPhyDrive->ResizeNoShrink = FALSE;
	pPhyDrive->ResizeVolumeGuid[0] = 0;
	pPhyDrive->FsName[0] = 0;

	if (ppPhyDrive)
	{
		*ppPhyDrive = pPhyDrive;
	}


    if (pPhyDrive->VentoyVersion[0])
    {
        Log("###[FAIL] No need to resize part");
        goto out;
    }

	if (pPhyDrive->DriveLetters[0] == 0)
	{
		Log("###[FAIL] No logical drive letter found for this disk");
		goto out;
	}

	//Get the BytesPerSector parameter
	sprintf_s(Drive, sizeof(Drive), "%C:", pPhyDrive->DriveLetters[0]);
	bRet = GetDiskFreeSpaceA(Drive, &SectorsPerCluster, &BytesPerSector, &NumberOfFreeClusters, &TotalNumberOfClusters);
	if (!bRet)
	{
		Log("Failed to GetDiskFreeSpaceA <%s> %u", Drive, LASTERR);
		goto out;
	}
	Log("BytesPerSector for this disk is %u", BytesPerSector);

	WinDir[0] = 0;
	GetWindowsDirectoryA(WinDir, sizeof(WinDir));

	//Print all logical drive letters
	FindFlag = FALSE;
	Drive[0] = FsName[0] = 0;
	for (i = 0; i < 64 && pPhyDrive->DriveLetters[i]; i++)
	{
		if (WinDir[1] == ':' && WinDir[0] == pPhyDrive->DriveLetters[i])
		{
			FindFlag = TRUE;
		}

		sprintf_s(Drive, sizeof(Drive), "%C: ", pPhyDrive->DriveLetters[i]);
		strcat_s(FsName, sizeof(FsName), Drive);
	}
	Log("Logical drives in this disk: %s (WinDir:%s)", FsName, WinDir);

	if (FindFlag)
	{
		Log("###[FAIL] You can not do non-destructive installation on Windows system disk.");
		goto out;
	}



    pGPT = malloc(sizeof(VTOY_GPT_INFO));
    if (!pGPT)
    {
        goto out;
    }

    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, FALSE, FALSE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        goto out;
    }

    bRet = ReadFile(hDrive, pGPT, sizeof(VTOY_GPT_INFO), &dwSize, NULL);
    if (!bRet)
    {
        Log("Failed to read disk %u %u", bRet, LASTERR);
        goto out;
    }

	memcpy(&pPhyDrive->Gpt, pGPT, sizeof(VTOY_GPT_INFO));

	if (pGPT->MBR.PartTbl[0].FsFlag == 0xEE && memcmp(pGPT->Head.Signature, "EFI PART", 8) == 0)
	{
		pPhyDrive->PartStyle = PartStyle = 1;
	}
	else
	{
		pPhyDrive->PartStyle = PartStyle = 0;
	}

    if (PartStyle == 0)
    {
		PART_TABLE *PartTbl = pGPT->MBR.PartTbl;

        for (Count = 0, i = 0; i < 4; i++)
        {
            if (PartTbl[i].SectorCount > 0)
            {
				Log("MBR Part%d SectorStart:%u SectorCount:%u", i + 1, PartTbl[i].StartSectorId, PartTbl[i].SectorCount);
                Count++;
            }
        }

		//We must have a free partition table for VTOYEFI partition
		if (Count >= 4)
		{
			Log("###[FAIL] 4 MBR partition tables are all used.");
			goto out;
		}

		if (PartTbl[0].SectorCount > 0)
		{
			Part1Start = PartTbl[0].StartSectorId;
			Part1End = PartTbl[0].SectorCount + Part1Start;
		}
		else
		{
			Log("###[FAIL] MBR Partition 1 is invalid");
			goto out;
		}

		Index = -1;
		NextPartStart = (pPhyDrive->SizeInBytes / BytesPerSector);
		for (i = 1; i < 4; i++)
		{
			if (PartTbl[i].SectorCount > 0 && NextPartStart > PartTbl[i].StartSectorId)
			{
				Index = i;
				NextPartStart = PartTbl[i].StartSectorId;
			}
		}

		NextPartStart *= (UINT64)BytesPerSector;
		Log("DiskSize:%llu NextPartStart:%llu(LBA:%llu) Index:%d", pPhyDrive->SizeInBytes, NextPartStart, NextPartStart / BytesPerSector, Index);
    }
    else
    {
		VTOY_GPT_PART_TBL *PartTbl = pGPT->PartTbl;

        for (Count = 0, i = 0; i < 128; i++)
        {
            if (memcmp(&(PartTbl[i].PartGuid), &ZeroGuid, sizeof(GUID)))
            {
				Log("GPT Part%d StartLBA:%llu LastLBA:%llu", i + 1, (ULONGLONG)PartTbl[i].StartLBA, (ULONGLONG)PartTbl[i].LastLBA);
                Count++;
            }
        }

		if (Count >= 128)
		{
			Log("###[FAIL] 128 GPT partition tables are all used.");
			goto out;
		}

		if (memcmp(&(PartTbl[0].PartGuid), &ZeroGuid, sizeof(GUID)))
		{
			Part1Start = PartTbl[0].StartLBA;
			Part1End = PartTbl[0].LastLBA + 1;
		}
		else
		{
			Log("###[FAIL] GPT Partition 1 is invalid");
			goto out;
		}

		Index = -1;
		NextPartStart = (pGPT->Head.PartAreaEndLBA + 1);
		for (i = 1; i < 128; i++)
		{
			if (memcmp(&(PartTbl[i].PartGuid), &ZeroGuid, sizeof(GUID)) && NextPartStart > PartTbl[i].StartLBA)
			{
				Index = i;
				NextPartStart = PartTbl[i].StartLBA;
			}
		}

		NextPartStart *= (UINT64)BytesPerSector;
		Log("DiskSize:%llu NextPartStart:%llu(LBA:%llu) Index:%d", (ULONGLONG)pPhyDrive->SizeInBytes, (ULONGLONG)NextPartStart, (ULONGLONG)NextPartStart / BytesPerSector, Index);
    }

	Log("Valid partition table (%s): Valid partition count:%d", (PartStyle == 0) ? "MBR" : "GPT", Count);

	//Partition 1 MUST start at 1MB
	Part1Start *= (UINT64)BytesPerSector;
	Part1End *= (UINT64)BytesPerSector;

	Log("Partition 1 start at: %llu %lluKB, end:%llu, NextPartStart:%llu", 
		(ULONGLONG)Part1Start, (ULONGLONG)Part1Start / 1024, (ULONGLONG)Part1End, (ULONGLONG)NextPartStart);
    if (Part1Start != SIZE_1MB)
    {
        Log("###[FAIL] Partition 1 is not start at 1MB");
        goto out;
    }

	pPhyDrive->ResizeOldPart1Size = Part1End - Part1Start;

	//If we have free space after partition 1
	if (NextPartStart - Part1End >= VENTOY_EFI_PART_SIZE)
	{
		Log("Free space after partition 1 (%llu) is enough for VTOYEFI part", (ULONGLONG)(NextPartStart - Part1End));
		pPhyDrive->ResizeNoShrink = TRUE;
		pPhyDrive->ResizePart2StartSector = Part1End / BytesPerSector;
		bCheck = TRUE;
		goto out;
	}
	else if (NextPartStart == Part1End)
	{
		Log("There is no free space after partition 1");
	}
	else
	{
		Log("The free space after partition 1 is not enough");
	}


	//We don't have enough free space after partition 1.
	//So we need to shrink partition 1, firstly let's check the free space of the volume.
	for (FindFlag = FALSE, i = 0; i < 64 && pPhyDrive->DriveLetters[i]; i++)
	{
		if (GetPhyDriveByLogicalDrive(pPhyDrive->DriveLetters[i], &Offset) >= 0)
		{
			if (Offset == Part1Start)
			{
				Log("Find the partition 1 logical drive is %C:", pPhyDrive->DriveLetters[i]);

				FindFlag = TRUE;
				pPhyDrive->Part1DriveLetter = pPhyDrive->DriveLetters[i];

				sprintf_s(Drive, sizeof(Drive), "%C:", pPhyDrive->DriveLetters[i]);
				bRet = GetDiskFreeSpaceA(Drive, &SectorsPerCluster, &BytesPerSector, &NumberOfFreeClusters, &TotalNumberOfClusters);
				if (!bRet)
				{
					Log("Failed to GetDiskFreeSpaceA <%s> %u", Drive, LASTERR);
					goto out;
				}

				FreeSize = NumberOfFreeClusters;
				FreeSize *= (UINT64)SectorsPerCluster;
				FreeSize *= (UINT64)BytesPerSector;

				Log("SectorsPerCluster:%u BytesPerSector:%u NumberOfFreeClusters:%u TotalNumberOfClusters:%u  ",
					SectorsPerCluster, BytesPerSector, NumberOfFreeClusters, TotalNumberOfClusters, (ULONGLONG)FreeSize);
				Log("<%s> freespace %llu %lluMB %lluGB", Drive, FreeSize, FreeSize / SIZE_1MB, FreeSize / SIZE_1GB);

				if (FreeSize < VENTOY_EFI_PART_SIZE * 2)
				{
					Log("###[FAIL] Free space is not engough");
					goto out;
				}

				break;
			}
		}
	}

	if (!FindFlag)
	{
		Log("Can not find the logical drive for partition 1");
		goto out;
	}


	//The volume has enough free space. Get the volume GUID for next shrink phase.
    Drive[2] = '\\';
    bRet = GetVolumeNameForVolumeMountPointA(Drive, VolumeGuid, sizeof(VolumeGuid) / 2);
    Drive[2] = 0;
    if (!bRet)
    {
        Log("GetVolumeNameForVolumeMountPointA failed <%s> %u", Drive, LASTERR);
        goto out;
    }

    strcpy_s(pPhyDrive->ResizeVolumeGuid, sizeof(pPhyDrive->ResizeVolumeGuid), VolumeGuid);
    Log("Volume GUID: <%s>", VolumeGuid);

    if (0 == GetVolumeInformationA(Drive, NULL, 0, NULL, NULL, NULL, FsName, MAX_PATH))
    {
        Log("GetVolumeInformationA failed %u", LASTERR);
        goto out;
    }


	//Only NTFS is supported.
	Log("Partition 1 is %s", FsName);
    if (_stricmp(FsName, "NTFS"))
    {
        Log("###[FAIL] Only NTFS is supported.");
        goto out;
    }

	strcpy_s(pPhyDrive->FsName, sizeof(pPhyDrive->FsName), FsName);




    Log("PartResizePreCheck success ...");
    bCheck = TRUE;

out:

    CHECK_FREE(pGPT);
    CHECK_CLOSE_HANDLE(hDrive);

    return bCheck;
}

static void OnPartResize(void)
{
    PHY_DRIVE_INFO* pPhyDrive = NULL;

	if (g_ThreadHandle)
	{
		Log("Another thread is runing");
		return;
	}

    if (!PartResizePreCheck(&pPhyDrive))
    {
        Log("#### Part Resize PreCheck Failed ####");
        MessageBox(g_DialogHwnd, _G(STR_PART_RESIZE_UNSUPPORTED), _G(STR_WARNING), MB_OK | MB_ICONWARNING);
        return;
    }

    if (MessageBox(g_DialogHwnd, _G(STR_PART_RESIZE_TIP), _G(STR_INFO), MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return;
    }

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, PartResizeThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void OnClearVentoy(void)
{
    int nCurSel;
    int SpaceMB = 0;
    int SizeInMB = 0;
    PHY_DRIVE_INFO *pPhyDrive = NULL;

    if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    if (MessageBox(g_DialogHwnd, _G(STR_INSTALL_TIP2), _G(STR_WARNING), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return;
    }

    if (g_ThreadHandle)
    {
        Log("Another thread is runing");
        return;
    }

    nCurSel = (int)SendMessage(g_ComboxHwnd, CB_GETCURSEL, 0, 0);
    if (CB_ERR == nCurSel)
    {
        Log("Failed to get combox sel");
        return;;
    }

    pPhyDrive = GetPhyDriveInfoById(nCurSel);
    if (!pPhyDrive)
    {
        return;
    }

    EnableWindow(g_BtnInstallHwnd, FALSE);
    EnableWindow(g_BtnUpdateHwnd, FALSE);

    g_ThreadHandle = CreateThread(NULL, 0, ClearVentoyThread, (LPVOID)pPhyDrive, 0, NULL);
}

static void MenuProc(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	WORD CtrlID;
    HMENU SubMenu;
	HMENU hMenu = GetMenu(hWnd);

	CtrlID = LOWORD(wParam);

	if (CtrlID == 0)
	{
		g_SecureBoot = !g_SecureBoot;

		if (g_SecureBoot)
		{
            ShowWindow(g_LocalIconSecureHwnd, SW_NORMAL);
			CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_CHECKED);
		}
		else
		{
            ShowWindow(g_LocalIconSecureHwnd, SW_HIDE);
			CheckMenuItem(hMenu, 0, MF_BYCOMMAND | MF_STRING | MF_UNCHECKED);
		}
	}
    else if (CtrlID == VTOY_MENU_PART_CFG)
    {
        DialogBox(g_hInst, MAKEINTRESOURCE(IDD_DIALOG2), hWnd, PartDialogProc);
		UpdateReservedPostfix();
        SetWindowTextA(g_StaticLocalFsHwnd, GetVentoyFsName());
        UpdateClusterTipMsg(IDC_STATIC_LOCAL_FS, hWnd, g_LocalClusterTipHwnd, GetClusterSizeTip());
    }
    else if (CtrlID == VTOY_MENU_CLEAN)
    {
        OnClearVentoy();
    }
    else if (CtrlID == VTOY_MENU_PART_RESIZE)
    {
        OnPartResize();
    }
#if VTSI_SUPPORT  
    else if (CtrlID == VTOY_MENU_VTSI)
    {
        SubMenu = GetSubMenu(hMenu, 0);

        g_WriteImage = 1 - g_WriteImage;
        if (g_WriteImage == 1)
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
        }
        else
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_VTSI, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_VTSI, _G(STR_MENU_VTSI_CREATE));
        }
    }
#endif
    else if (CtrlID == VTOY_MENU_ALL_DEV)
    {
        SubMenu = GetSubMenu(hMenu, 0);

        g_FilterUSB = 1 - g_FilterUSB;
        if (g_FilterUSB == 0)
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_CHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
        }
        else
        {
            ModifyMenu(SubMenu, OPT_SUBMENU_ALL_DEV, MF_STRING | MF_BYPOSITION | MF_UNCHECKED, VTOY_MENU_ALL_DEV, _G(STR_SHOW_ALL_DEV));
        }

        OnRefreshBtnClick(hWnd);
    }
    else if (CtrlID == ID_PARTSTYLE_MBR)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_UNCHECKED);
        g_cur_part_style = 0;
        UpdateLocalVentoyVersion();
        ShowWindow(g_DialogHwnd, SW_HIDE);
        ShowWindow(g_DialogHwnd, SW_NORMAL);
    }
    else if (CtrlID == ID_PARTSTYLE_GPT)
    {
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_MBR, MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuItem(hMenu, (UINT)ID_PARTSTYLE_GPT, MF_BYCOMMAND | MF_CHECKED);
        g_cur_part_style = 1;
        UpdateLocalVentoyVersion();
        ShowWindow(g_DialogHwnd, SW_HIDE);
        ShowWindow(g_DialogHwnd, SW_NORMAL);
    }
	else if (CtrlID >= VTOY_MENU_LANGUAGE_BEGIN && CtrlID < VTOY_MENU_LANGUAGE_BEGIN + g_language_count)
	{
		UpdateItemString(CtrlID - VTOY_MENU_LANGUAGE_BEGIN);
	}
}

INT_PTR CALLBACK DialogProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    WORD NotifyCode;
    WORD CtrlID;

    switch (Message)
    {
		case WM_NOTIFY:
		{
			UINT code = 0;
			UINT_PTR idFrom = 0;

			if (lParam)
			{
				code = ((LPNMHDR)lParam)->code;
				idFrom = ((LPNMHDR)lParam)->idFrom;
			}
			
			if (idFrom == IDC_SYSLINK1 && (NM_CLICK == code || NM_RETURN == code))
			{
				ShellExecute(NULL, L"open", L"https://www.ventoy.net", NULL, NULL, SW_SHOW);
			}

            if (idFrom == IDC_SYSLINK2 && (NM_CLICK == code || NM_RETURN == code))
            {
                if (g_cur_lang_id == 0)
                {
                    ShellExecute(NULL, L"open", L"https://www.ventoy.net/cn/donation.html", NULL, NULL, SW_SHOW);
                }
                else
                {
                    ShellExecute(NULL, L"open", L"https://www.ventoy.net/en/donation.html", NULL, NULL, SW_SHOW);
                }
            }
			break;
		}
        case WM_COMMAND:
        {
            NotifyCode = HIWORD(wParam);
            CtrlID = LOWORD(wParam);

            if (CtrlID == IDC_COMBO1 && NotifyCode == CBN_SELCHANGE)
            {
                OnComboxSelChange((HWND)lParam);
            }

            if (CtrlID == IDC_BUTTON4 && NotifyCode == BN_CLICKED)
            {
                OnInstallBtnClick();
            }
            else if (CtrlID == IDC_BUTTON3 && NotifyCode == BN_CLICKED)
            {
                OnUpdateBtnClick();
            }
            else if (CtrlID == IDC_COMMAND1 && NotifyCode == BN_CLICKED)
            {
                OnRefreshBtnClick(hWnd);
            }

			if (lParam == 0 && NotifyCode == 0)
			{
				MenuProc(hWnd, wParam, lParam);
			}

            break;
        }
        case WM_INITDIALOG:
        {
            InitDialog(hWnd, wParam, lParam);
            break;
        }
        case WM_CTLCOLORSTATIC:
        {
            if (GetDlgItem(hWnd, IDC_STATIC_LOCAL_VER) == (HANDLE)lParam)
            {
                SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, RGB(255, 0, 0));
                return (LRESULT)(HBRUSH)(GetStockObject(HOLLOW_BRUSH));
            }
            else if (GetDlgItem(hWnd, IDC_STATIC_DISK_VER) == (HANDLE)lParam)
            {
                SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, g_InvalidClusterSize ? RGB(0, 0, 255) : RGB(255, 0, 0));
                return (LRESULT)(HBRUSH)(GetStockObject(HOLLOW_BRUSH));
            }
            
#if 0
            else if (GetDlgItem(hWnd, IDC_STATIC_LOCAL_SECURE) == (HANDLE)lParam ||
                GetDlgItem(hWnd, IDC_STATIC_DEV_SECURE) == (HANDLE)lParam)
			{
				SetBkMode((HDC)wParam, TRANSPARENT);
                SetTextColor((HDC)wParam, RGB(0xea, 0x99, 0x1f));
				return (LRESULT)(HBRUSH)(GetStockObject(HOLLOW_BRUSH));
			}
#endif
            else
            {
                break;
            }
        }
        case WM_CLOSE:
        {
            if (g_ThreadHandle)
            {
                MessageBox(g_DialogHwnd, _G(STR_WAIT_PROCESS), _G(STR_INFO), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                EndDialog(hWnd, 0);
            }
			WriteCfgIni();
            break;
        }
    }

    return 0;
}

static DWORD VentoyGetParentProcessId(DWORD pid)
{
    HANDLE h = NULL;
    PROCESSENTRY32 pe = { 0 };
    DWORD ppid = 0;

    pe.dwSize = sizeof(PROCESSENTRY32);
    h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == h)
    {
        return 0;
    }

    if (Process32First(h, &pe))
    {
        do
        {
            if (pe.th32ProcessID == pid)
            {
                ppid = pe.th32ParentProcessID;
                break;
            }
        } while (Process32Next(h, &pe));
    }

    CloseHandle(h);
    return ppid;
}

static int VentoyCheckParentProcess(void)
{
    int i, j;
    int ret = 0;
    HANDLE h;
    DWORD pid, ppid;
    DWORD len = MAX_PATH;
    BYTE* buffer = NULL;
    UINT32* pData = NULL;
    BYTE Magic[] = { 0x4E, 0x75, 0x6C, 0x6C, 0x73, 0x6F, 0x66, 0x74 };
    WCHAR ParentPath[MAX_PATH];    

    pid = GetCurrentProcessId();
    ppid = VentoyGetParentProcessId(pid);

    if (ppid == 0)
    {
        Log("Failed to get parent process id for %u %u", pid, LASTERR);
        return 0;
    }

    Log("id=%u/%u", pid, ppid);
    
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ppid);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("Failed to OpenProcess for %u %u", ppid, LASTERR);
        return 0;
    }

    if (0 == QueryFullProcessImageName(h, 0, ParentPath, &len))
    {
        Log("Failed to QueryFullProcessImageName for %u %u", ppid, LASTERR);
        return 0;
    }

    CHECK_CLOSE_HANDLE(h);

    Log("PPath:<%ls>", ParentPath);

    h = CreateFile(ParentPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("Failed to create file %u", LASTERR);
        return 0;
    }

    len = GetFileSize(h, NULL);
    Log("PSize:<%u %uKB>", len, len / 1024);

    if (len < 8 * SIZE_1MB)
    {
        goto out;
    }

    buffer = malloc(SIZE_1MB);
    if (buffer == NULL)
    {
        goto out;
    }

    if (FALSE == ReadFile(h, buffer, SIZE_1MB, &len, NULL))
    {
        Log("Failed to readfile %u", LASTERR);
        goto out;
    }

    for (i = 0; i + 16 < SIZE_1MB && ret == 0; i += 16)
    {
        pData = (UINT32*)(buffer + i);
        if (pData[0] == 0x6D783F3C && pData[1] == 0x6576206C)
        {
            for (j = 0; j < 1024 && (i + j + 16) < SIZE_1MB; j++)
            {
                if (0 == memcmp(buffer + i + j, Magic, sizeof(Magic)))
                {
                    ret = 1;
                    break;
                }
            }
        }
    }

out:
    Log("Lunch main process %d", ret);
    CHECK_CLOSE_HANDLE(h);
    if (buffer) free(buffer);
    return ret;
}

//
//copy from Rufus
//
#include <delayimp.h>
// For delay-loaded DLLs, use LOAD_LIBRARY_SEARCH_SYSTEM32 to avoid DLL search order hijacking.
FARPROC WINAPI dllDelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliNotePreLoadLibrary) {
        // Windows 7 without KB2533623 does not support the LOAD_LIBRARY_SEARCH_SYSTEM32 flag.
        // That is is OK, because the delay load handler will interrupt the NULL return value
        // to mean that it should perform a normal LoadLibrary.
        return (FARPROC)LoadLibraryExA(pdli->szDll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return NULL;
}

#if defined(_MSC_VER)
// By default the Windows SDK headers have a `const` while MinGW does not.
const
#endif
PfnDliHook __pfnDliNotifyHook2 = dllDelayLoadHook;

typedef BOOL(WINAPI *SetDefaultDllDirectories_t)(DWORD);
static void DllProtect(void)
{
    SetDefaultDllDirectories_t pfSetDefaultDllDirectories = NULL;

    // Disable loading system DLLs from the current directory (sideloading mitigation)
    // PS: You know that official MSDN documentation for SetDllDirectory() that explicitly
    // indicates that "If the parameter is an empty string (""), the call removes the current
    // directory from the default DLL search order"? Yeah, that doesn't work. At all.
    // Still, we invoke it, for platforms where the following call might actually work...
    SetDllDirectoryA("");

    // For libraries on the KnownDLLs list, the system will always load them from System32.
    // For other DLLs we link directly to, we can delay load the DLL and use a delay load
    // hook to load them from System32. Note that, for this to work, something like:
    // 'somelib.dll;%(DelayLoadDLLs)' must be added to the 'Delay Loaded Dlls' option of
    // the linker properties in Visual Studio (which means this won't work with MinGW).
    // For all other DLLs, use SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32).
    // Finally, we need to perform the whole gymkhana below, where we can't call on
    // SetDefaultDllDirectories() directly, because Windows 7 doesn't have the API exposed.
    // Also, no, Coverity, we never need to care about freeing kernel32 as a library.
    // coverity[leaked_storage]

    pfSetDefaultDllDirectories = (SetDefaultDllDirectories_t)
        GetProcAddress(LoadLibraryW(L"kernel32.dll"), "SetDefaultDllDirectories");
    if (pfSetDefaultDllDirectories != NULL)
        pfSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow)
{
    int i, j;
    WCHAR *Pos = NULL;
    WCHAR CurDir[MAX_PATH];
    const char *checkfile[] =
    {
        "boot\\boot.img",
        "boot\\core.img.xz",
        "ventoy\\ventoy.disk.img.xz",
        "ventoy\\version",
        NULL
    };

    UNREFERENCED_PARAMETER(hPrevInstance);

    if (__argc > 1 && __argv[1] && _stricmp(__argv[1], "VTOYCLI") == 0)
    {
        g_CLI_Mode = TRUE;
    }

    DllProtect();
    
    GetCurrentDirectory(MAX_PATH, CurDir);
    Pos = wcsstr(CurDir, L"\\altexe");
    if (Pos)
    {
        *Pos = 0;
        SetCurrentDirectory(CurDir);
    }

    for (i = 0; checkfile[i]; i++)
    {
        if (!IsFileExist("%s", checkfile[i]))
        {
            for (j = 0; j < 50; j++)
            {
                Log("####### File <%s> not found, did you download it from official website ? ######", checkfile[i]);
            }

            if (IsFileExist("grub\\grub.cfg"))
            {
                MessageBox(NULL, TEXT("Don't run me here, please use the released install package."), TEXT("Error"), MB_OK | MB_ICONERROR);
            }
            else
            {
                MessageBox(NULL, TEXT("Please run under the correct directory!"), TEXT("Error"), MB_OK | MB_ICONERROR);
            }
            return ERROR_NOT_FOUND;
        }
    }

	Log("\n##################################################################################\n"		
		"######################### Ventoy2Disk%s (%s) %s #########################\n"
		"##################################################################################",
        current_arch_string(), GetLocalVentoyVersion(),
        g_CLI_Mode ? "CLI MODE" : "");

    Log("Current directory:<%s>", CurDir);

    if (g_CLI_Mode)
    {
        DumpWindowsVersion();

        if (VentoyCLIMain(__argc, __argv))
        {
            Log("[ERROR] ######### Ventoy CLI FAILED #########");
            return 1;
        }
        else
        {
            Log("######### Ventoy CLI SUCCESS #########");
            return 0;
        }
    }

    if (VentoyCheckParentProcess())
    {
        return ERROR_NOT_SUPPORTED;
    }

    ParseCmdLineOption(lpCmdLine);
    LoadCfgIni();

    DumpWindowsVersion();

    Ventoy2DiskInit();

    g_hInst = hInstance;
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DialogProc);

    Ventoy2DiskDestroy();

    return 0;
}
