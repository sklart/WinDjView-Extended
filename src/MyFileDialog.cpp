//	WinDjView
//	Copyright (C) 2004-2012 Andrew Zhezherun
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program; if not, write to the Free Software Foundation, Inc.,
//	51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.
//	http://www.gnu.org/copyleft/gpl.html

#include "stdafx.h"
#include "WinDjView.h"
#include "MyFileDialog.h"
#include "Global.h"

#include "PathUtil.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

struct MyFileDialogData : public CNoTrackObject
{
	MyFileDialogData() : hHook(NULL) {}
	virtual ~MyFileDialogData() {}
	HHOOK hHook;
};
THREAD_LOCAL(MyFileDialogData, _myFileDlgData)

template <typename T>
class ComPtr
{
public:
	ComPtr() : m_ptr(NULL) {}
	~ComPtr() { if (m_ptr != NULL) m_ptr->Release(); }

	T* operator->() const { return m_ptr; }
	operator T*() const { return m_ptr; }
	T** Receive()
	{
		if (m_ptr != NULL)
		{
			m_ptr->Release();
			m_ptr = NULL;
		}
		return &m_ptr;
	}

private:
	ComPtr(const ComPtr&);
	ComPtr& operator=(const ComPtr&);
	T* m_ptr;
};


// CMyFileDialog

IMPLEMENT_DYNAMIC(CMyFileDialog, CDialog)

CMyFileDialog::CMyFileDialog(bool bOpenFileDialog, LPCTSTR lpszDefExt,
		LPCTSTR lpszFileName, DWORD dwFlags, LPCTSTR lpszFilter, CWnd* pParentWnd)
	: CCommonDialog(pParentWnd)
{
	ASSERT((dwFlags & (OFN_ENABLETEMPLATE | OFN_ENABLEHOOK)) == 0);

	ZeroMemory(&m_ofn, sizeof(m_ofn));
	m_legacyFileBuffer.resize(32768);
	m_legacyTitleBuffer.resize(32768);
	ZeroMemory(&m_legacyFileBuffer[0], m_legacyFileBuffer.size() * sizeof(TCHAR));
	ZeroMemory(&m_legacyTitleBuffer[0], m_legacyTitleBuffer.size() * sizeof(TCHAR));

	if (IsWin2kOrLater())
	{
		m_ofn.lStructSize = sizeof(m_ofn);
	}
	else
	{
#if defined(OPENFILENAME_SIZE_VERSION_400)
		m_ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
		m_ofn.lStructSize = sizeof(OPENFILENAME);
#endif
	}

	m_bOpenFileDialog = bOpenFileDialog;
	m_nIDHelp = bOpenFileDialog ? AFX_IDD_FILEOPEN : AFX_IDD_FILESAVE;

	m_ofn.lpstrFile = &m_legacyFileBuffer[0];
	m_ofn.nMaxFile = (DWORD)m_legacyFileBuffer.size();
	m_ofn.lpstrDefExt = lpszDefExt;
	m_ofn.lpstrFileTitle = &m_legacyTitleBuffer[0];
	m_ofn.nMaxFileTitle = (WORD)m_legacyTitleBuffer.size();
	m_ofn.Flags |= dwFlags | OFN_EXPLORER | OFN_ENABLESIZING;
	m_ofn.hInstance = AfxGetResourceHandle();

	// setup initial file name
	if (lpszFileName != NULL)
		SetInitialFileName(lpszFileName);

	// Translate filter into commdlg format (lots of \0)
	if (lpszFilter != NULL)
	{
		m_strFilter = lpszFilter;
		LPTSTR pch = m_strFilter.GetBuffer(0); // modify the buffer in place
		// MFC delimits with '|' not '\0'
		while ((pch = _tcschr(pch, '|')) != NULL)
			*pch++ = '\0';
		m_ofn.lpstrFilter = m_strFilter;
		// do not call ReleaseBuffer() since the string contains '\0' characters
	}
}


BEGIN_MESSAGE_MAP(CMyFileDialog, CCommonDialog)
END_MESSAGE_MAP()


INT_PTR CMyFileDialog::DoModal()
{
	ASSERT_VALID(this);
	if (IsWinVistaOrLater())
	{
		HWND owner = PreModal();
		INT_PTR result = DoModernModal(owner);
		PostModal();
		if (result != -1)
			return result;
	}
	return DoLegacyModal();
}

INT_PTR CMyFileDialog::DoModernModal(HWND owner)
{
	ComPtr<IFileDialog> dialog;
	const CLSID& classId = m_bOpenFileDialog ? CLSID_FileOpenDialog : CLSID_FileSaveDialog;
	HRESULT result = ::CoCreateInstance(classId, NULL, CLSCTX_INPROC_SERVER,
		IID_IFileDialog, reinterpret_cast<void**>(dialog.Receive()));
	if (FAILED(result))
		return -1; // Request the legacy implementation as a defensive fallback.

	DWORD options = 0;
	dialog->GetOptions(&options);
	if (m_bOpenFileDialog)
	{
		options |= FOS_FORCEFILESYSTEM;
		if ((m_ofn.Flags & OFN_FILEMUSTEXIST) != 0)
			options |= FOS_FILEMUSTEXIST;
	}
	else
	{
		options |= FOS_FORCEFILESYSTEM;
		if ((m_ofn.Flags & OFN_OVERWRITEPROMPT) != 0)
			options |= FOS_OVERWRITEPROMPT;
	}
	if ((m_ofn.Flags & OFN_PATHMUSTEXIST) != 0)
		options |= FOS_PATHMUSTEXIST;
	dialog->SetOptions(options);

	if (m_ofn.lpstrTitle != NULL)
		dialog->SetTitle(m_ofn.lpstrTitle);
	if (m_ofn.lpstrDefExt != NULL)
		dialog->SetDefaultExtension(m_ofn.lpstrDefExt);
	if (m_ofn.nFilterIndex != 0)
		dialog->SetFileTypeIndex(m_ofn.nFilterIndex);

	vector<COMDLG_FILTERSPEC> filters;
	if (m_ofn.lpstrFilter != NULL)
	{
		LPCTSTR current = m_ofn.lpstrFilter;
		while (*current != 0)
		{
			LPCTSTR name = current;
			current += _tcslen(current) + 1;
			if (*current == 0)
				break;
			COMDLG_FILTERSPEC filter = { name, current };
			filters.push_back(filter);
			current += _tcslen(current) + 1;
		}
	}
	if (!filters.empty())
		dialog->SetFileTypes((UINT)filters.size(), &filters[0]);

	CString initial = m_strInitialFile;
	if (initial.IsEmpty() && m_ofn.lpstrFile != NULL)
		initial = m_ofn.lpstrFile;
	if (!initial.IsEmpty())
	{
		int slash = initial.ReverseFind(_T('\\'));
		if (slash >= 0)
		{
			CString directory = slash == 2 && initial.GetLength() > 2 && initial[1] == _T(':')
				? initial.Left(3) : initial.Left(slash);
			ComPtr<IShellItem> folder;
			if (SUCCEEDED(::SHCreateItemFromParsingName(directory, NULL, IID_IShellItem,
				reinterpret_cast<void**>(folder.Receive()))))
			{
				dialog->SetFolder(folder);
			}
			initial = initial.Mid(slash + 1);
		}
		dialog->SetFileName(initial);
	}

	result = dialog->Show(owner);
	if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		return IDCANCEL;
	if (FAILED(result))
		return IDCANCEL;

	ComPtr<IShellItem> item;
	result = dialog->GetResult(item.Receive());
	if (SUCCEEDED(result))
	{
		PWSTR path = NULL;
		result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
		if (SUCCEEDED(result) && path != NULL)
		{
			m_strPathName = path;
			int slash = m_strPathName.ReverseFind(_T('\\'));
			m_strFileName = slash >= 0 ? m_strPathName.Mid(slash + 1) : m_strPathName;
			::CoTaskMemFree(path);
		}
		if (SUCCEEDED(result))
			dialog->GetFileTypeIndex(&m_ofn.nFilterIndex);
	}
	return SUCCEEDED(result) ? IDOK : IDCANCEL;
}

INT_PTR CMyFileDialog::DoLegacyModal()
{
	// From MFC:  CFileDialog::DoModal
	// Uses the OpenFileNameEx structure on Win2k+

	set<CWnd*> disabled;
	theApp.DisableTopLevelWindows(disabled);

	ASSERT((m_ofn.Flags & OFN_EXPLORER) != 0);
	ASSERT((m_ofn.Flags & OFN_ENABLEHOOK) == 0);
	ASSERT(m_ofn.lpfnHook == NULL);  // Not using hooks

	// zero out the file buffer for consistent parsing later
	ASSERT(AfxIsValidAddress(m_ofn.lpstrFile, m_ofn.nMaxFile));
	DWORD nOffset = lstrlen(m_ofn.lpstrFile) + 1;
	ASSERT(nOffset <= m_ofn.nMaxFile);
	ZeroMemory(m_ofn.lpstrFile + nOffset, (m_ofn.nMaxFile - nOffset)*sizeof(TCHAR));

	// WINBUG: This is a special case for the file open/save dialog,
	//  which sometimes pumps while it is coming up but before it has
	//  disabled the main window.
	HWND hWndFocus = ::GetFocus();
	BOOL bEnableParent = FALSE;
	m_ofn.hwndOwner = PreModal();
	if (m_ofn.hwndOwner != NULL && ::IsWindowEnabled(m_ofn.hwndOwner))
	{
		bEnableParent = TRUE;
		::EnableWindow(m_ofn.hwndOwner, FALSE);
	}

	AfxUnhookWindowCreate();

	_AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
	ASSERT(pThreadState->m_pAlternateWndInit == NULL);
	pThreadState->m_pAlternateWndInit = this;

	// Install our own hook to subclass the file dialog
	MyFileDialogData* pMyData = _myFileDlgData.GetData();
	ASSERT(pMyData->hHook == NULL);
	pMyData->hHook = ::SetWindowsHookEx(WH_CBT, &HookProc, NULL, ::GetCurrentThreadId());

	int nResult;
	if (m_bOpenFileDialog)
		nResult = ::GetOpenFileName(&m_ofn);
	else
		nResult = ::GetSaveFileName(&m_ofn);

	if (nResult)
	{
		ASSERT(pThreadState->m_pAlternateWndInit == NULL);
		m_strPathName = m_ofn.lpstrFile;
		m_strFileName = m_ofn.lpstrFileTitle;
	}
	pThreadState->m_pAlternateWndInit = NULL;

	::UnhookWindowsHookEx(pMyData->hHook);
	pMyData->hHook = NULL;

	// WINBUG: Second part of special case for file open/save dialog.
	if (bEnableParent)
		::EnableWindow(m_ofn.hwndOwner, TRUE);
	if (::IsWindow(hWndFocus))
		::SetFocus(hWndFocus);

	PostModal();

	theApp.EnableWindows(disabled);
	return nResult ? nResult : IDCANCEL;
}

LRESULT CALLBACK CMyFileDialog::HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	_AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
	if (nCode == HCBT_ACTIVATE)
	{
		HWND hWnd = (HWND) wParam;
		ASSERT(hWnd != NULL);

		if (pThreadState->m_pAlternateWndInit != NULL && CWnd::FromHandlePermanent(hWnd) == NULL)
		{
			ASSERT_KINDOF(CMyFileDialog, pThreadState->m_pAlternateWndInit);
			pThreadState->m_pAlternateWndInit->SubclassWindow(hWnd);

			pThreadState->m_pAlternateWndInit->CenterWindow();
			pThreadState->m_pAlternateWndInit = NULL;
		}
	}

	MyFileDialogData* pMyData = _myFileDlgData.GetData();
	return ::CallNextHookEx(pMyData->hHook, nCode, wParam, lParam);
}

BOOL CMyFileDialog::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	return CCommonDialog::OnNotify(wParam, lParam, pResult);
}

CString CMyFileDialog::GetPathName() const
{
	ASSERT(m_hWnd == NULL);
	return m_strPathName;
}

CString CMyFileDialog::GetFileName() const
{
	ASSERT(m_hWnd == NULL);
	return m_strFileName;
}

CString CMyFileDialog::GetFileExt() const
{
	ASSERT(m_hWnd == NULL);
	int dot = m_strFileName.ReverseFind(_T('.'));
	if (dot < 0)
		return _T("");
	else
		return m_strFileName.Mid(dot + 1);
}

CString CMyFileDialog::GetFileTitle() const
{
	CString strResult = GetFileName();
	LPTSTR pszBuffer = strResult.GetBuffer(0);
	::PathRemoveExtension(pszBuffer);
	strResult.ReleaseBuffer();
	return strResult;
}

bool CMyFileDialog::GetReadOnlyPref() const
{
	ASSERT(m_hWnd == NULL);
	return (m_ofn.Flags & OFN_READONLY) != 0;
}

void CMyFileDialog::SetInitialFileName(const CString& fileName)
{
	m_strInitialFile = fileName;
	if (!m_legacyFileBuffer.empty())
	{
		_tcsncpy(&m_legacyFileBuffer[0], fileName, m_legacyFileBuffer.size() - 1);
		m_legacyFileBuffer[m_legacyFileBuffer.size() - 1] = 0;
	}
}
