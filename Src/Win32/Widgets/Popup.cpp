// Popup.cpp
//
// Copyright (c) 2016 The Dasher Team
//
// This file is part of Dasher.
//
// Dasher is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Dasher is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Dasher; if not, write to the Free Software 
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
// NOTES:
//

#include "WinCommon.h"

#include "Popup.h"
#include "../../DasherCore/Event.h"
#include "FilenameGUI.h"
#include "../resource.h"
#include "../../DasherCore/DasherInterfaceBase.h"

using namespace Dasher;
using namespace std;
using namespace WinLocalisation;
using namespace WinUTF8;

CPopup::CPopup(CAppSettings *pAppSettings) {
  m_FilenameGUI = 0;
  
  // TODO: Check that this is all working okay (it quite probably
  // isn't). In the long term need specialised editor classes.
  targetwindow = 0;

  m_pAppSettings = pAppSettings;

  UINT CodePage = GetUserCodePage();
  m_Font = GetCodePageFont(CodePage, 14);
}

HWND CPopup::Create(HWND hParent, bool bNewWithDate) {
  RECT r = {
	  100,100,400,500
  };
  m_popup = CWindowImpl<CPopup>::Create(hParent, r, NULL, WS_OVERLAPPEDWINDOW | WS_VISIBLE);

  Tstring WindowTitle;
  WinLocalisation::GetResourceString(IDS_APP_TITLE, &WindowTitle);
  m_FilenameGUI = new CFilenameGUI(hParent, WindowTitle.c_str(), bNewWithDate);
  
  return *this;
}


CPopup::~CPopup() {
  DeleteObject(m_Font);
  delete m_FilenameGUI;
}

void CPopup::Move(int x, int y, int Width, int Height) {
  MoveWindow( x, y, Width, Height, TRUE);
}

bool CPopup::Save() {
  if (m_filename == TEXT(""))
    return false;

  HANDLE FileHandle = CreateFile(m_filename.c_str(), GENERIC_WRITE, 0, 
    (LPSECURITY_ATTRIBUTES)NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, (HANDLE)NULL);
  if (FileHandle == INVALID_HANDLE_VALUE)
    return false;

  CString wideText;
  GetWindowText(wideText);

  DWORD NumberOfBytesWritten;   // Used by WriteFile
  switch (m_pAppSettings->GetLongParameter(APP_LP_FILE_ENCODING))
  {
  case Opts::UTF8: {
    WriteFile(FileHandle, "\xEF\xBB\xBF", 3, &NumberOfBytesWritten, NULL);
    string utf8Text = wstring_to_UTF8string(wideText);
    WriteFile(FileHandle, utf8Text.c_str(), utf8Text.size(), &NumberOfBytesWritten, NULL);
    break;
  }
  case Opts::UTF16LE: {
    // TODO I am assuming this machine is LE. Do any windows (perhaps CE) machines run on BE?
    WriteFile(FileHandle, "\xFF\xFE", 2, &NumberOfBytesWritten, NULL);
    WriteFile(FileHandle, wideText.GetBuffer(), wideText.GetLength() * 2, &NumberOfBytesWritten, NULL);
    break;
  }
  case Opts::UTF16BE: {
    // TODO I am again assuming this machine is LE.
    WriteFile(FileHandle, "\xFE\xFF", 2, &NumberOfBytesWritten, NULL);
    for (unsigned int i = 0; i < wideText.GetLength(); i++) {
      wideText.SetAt(i, _byteswap_ushort(wideText[i]));
    }
    WriteFile(FileHandle, wideText.GetBuffer(), wideText.GetLength() * 2, &NumberOfBytesWritten, NULL);
    break;
  }
  default:
    CStringA mbcsText(wideText); // converts wide string to current locale
    WriteFile(FileHandle, mbcsText, mbcsText.GetLength(), &NumberOfBytesWritten, NULL);
    break;
  }
  CloseHandle(FileHandle);

  m_FilenameGUI->SetDirty(false);
  m_dirty = false;
  return true;
}

bool CPopup::ConfirmAndSaveIfNeeded() {
  if (!m_pAppSettings->GetBoolParameter(APP_BP_CONFIRM_UNSAVED))
    return true;
  
  switch (m_FilenameGUI->QuerySaveFirst()) {
  case IDYES:
    if (!Save())
      if (!TSaveAs(m_FilenameGUI->SaveAs()))
        return false;
    break;
  case IDNO:
    break;
  default:
    return false;
  }
  return true;
}

void CPopup::New() {
  if (ConfirmAndSaveIfNeeded())
    TNew(TEXT(""));
}

void CPopup::Open() {
  if (ConfirmAndSaveIfNeeded())
    TOpen(m_FilenameGUI->Open());
}

void CPopup::SaveAs() {
  TSaveAs(m_FilenameGUI->SaveAs());
}

std::string CPopup::Import() {
  string filename;
  wstring_to_UTF8string(m_FilenameGUI->Open(), filename);
  return filename;
}

void CPopup::SetDirty() {
  m_dirty = true;
  m_FilenameGUI->SetDirty(true);
}

void CPopup::TNew(const Tstring &filename) {
  // TODO: Send a message to the parent to say that the buffer has
  // changed (as in the Linux version).

  if(filename == TEXT(""))
    m_filename = m_FilenameGUI->New();
  else
    m_filename = filename;
  Clear();
}

bool CPopup::TOpen(const Tstring &filename) {
  // Could try and detect unicode formats from BOMs like notepad.
  // Could also base codepage on menu.
  // Best thing is probably to trust any BOMs at the beginning of file, but otherwise
  // to believe menu. Unicode files don't necessarily have BOMs, especially from Unix.

  HANDLE FileHandle = CreateFile(filename.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, (LPSECURITY_ATTRIBUTES) NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                (HANDLE) NULL);

  if(FileHandle == INVALID_HANDLE_VALUE)
    return false;

  m_filename = filename;
  SetFilePointer(FileHandle, NULL, NULL, FILE_BEGIN);
  DWORD filesize = GetFileSize(FileHandle, NULL);
  unsigned long amountread = 0;
  CStringA filestr;
  char* filebuffer = filestr.GetBufferSetLength(filesize+2);
  ReadFile(FileHandle, filebuffer, filesize, &amountread, NULL);
  CloseHandle(FileHandle);
  filebuffer[amountread] = 0;
  filebuffer[amountread+1] = 0;
  long encoding = m_pAppSettings->GetLongParameter(APP_LP_FILE_ENCODING);
  bool removeBOM = false;
  if (amountread >= 3 && strncmp(filebuffer, "\xEF\xBB\xBF", 3) == 0) {
    encoding = Opts::UTF8;
    removeBOM = true;
  }
  if (amountread >= 2 && strncmp(filebuffer, "\xFF\xFE", 2) == 0) {
    encoding = Opts::UTF16LE;
    removeBOM = true;
  }
  if (amountread >= 2 && strncmp(filebuffer, "\xFE\xFF", 2) == 0) {
    encoding = Opts::UTF16BE;
    removeBOM = true;
  }

  wstring inserttext;
  switch (encoding) {
  case Opts::UTF8: {
    UTF8string_to_wstring(filebuffer + (removeBOM ? 3 : 0), inserttext);
    break;
  }
  case Opts::UTF16LE: {
    inserttext = reinterpret_cast<wchar_t*>(filebuffer+ (removeBOM ? 2 : 0));
    break;
  }
  case Opts::UTF16BE: {
    wchar_t* widePtr = reinterpret_cast<wchar_t*>(filebuffer + (removeBOM ? 2 : 0));
    for (unsigned int i = 0; widePtr[i]; i++) {
      widePtr[i] = _byteswap_ushort(widePtr[i]);
    }
    inserttext = widePtr;
    break;
  }
  default:
    CString wideFromMBCS(filestr); // converts mbcs to wide string
    inserttext = wideFromMBCS;
    break;
  }
  InsertText(inserttext);

  m_FilenameGUI->SetFilename(m_filename);
  m_FilenameGUI->SetDirty(false);
  m_dirty = false;
  return true;
}

bool CPopup::TSaveAs(const Tstring &filename) {
  m_filename = filename;
  if(Save()) {
    m_FilenameGUI->SetFilename(m_filename);
    return true;
  }
  else
    return false;
}

void CPopup::Cut() {
  SendMessage(WM_CUT, 0, 0);
}

void CPopup::Copy() {
  SendMessage(WM_COPY, 0, 0);
}

void CPopup::Paste() {
  SendMessage(WM_PASTE, 0, 0);
}

void CPopup::SelectAll() {
  SendMessage(EM_SETSEL, 0, -1);
}

void CPopup::Clear() {
  SendMessage(WM_SETTEXT, 0, (LPARAM) TEXT(""));
}

void CPopup::SetFont(string Name, long Size) {
  Tstring FontName;
  UTF8string_to_wstring(Name, FontName);

  if(Size == 0)
    Size = 14;

  DeleteObject(m_Font);
  if (Name == "") {
    UINT CodePage = GetUserCodePage();
    m_Font = GetCodePageFont(CodePage, -Size);
  }
  else
    m_Font = CreateFont(-Size, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE, FontName.c_str());    // DEFAULT_CHARSET => font made just from Size and FontName

  SendMessage(WM_SETFONT, (WPARAM) m_Font, true);
}

void CPopup::SetInterface(Dasher::CDasherInterfaceBase *DasherInterface) {
  m_pDasherInterface = DasherInterface;
}

void CPopup::output(const std::string &sText) {
  wstring String;
  WinUTF8::UTF8string_to_wstring(sText, String);

  OutputDebugStringW(String.c_str());
  InsertText(String);

  if(m_pAppSettings->GetLongParameter(APP_LP_STYLE) == APP_STYLE_DIRECT) {
    const char *DisplayText = sText.c_str();
    if(DisplayText[0] == 0xd && DisplayText[1] == 0xa) {
      // Newline, so we want to fake an enter
      fakekey[0].type = fakekey[1].type = INPUT_KEYBOARD;
      fakekey[0].ki.wVk = fakekey[1].ki.wVk = VK_RETURN;
      fakekey[0].ki.time = fakekey[1].ki.time = 0;
      fakekey[1].ki.dwFlags = KEYEVENTF_KEYUP;
      SendInput(2, fakekey, sizeof(INPUT));
    }
    else {    
      for(std::wstring::iterator it(String.begin()); it != String.end(); ++it) {
        fakekey[0].type = INPUT_KEYBOARD;
        fakekey[0].ki.dwFlags = KEYEVENTF_UNICODE;
        fakekey[0].ki.wVk = 0;
        fakekey[0].ki.time = NULL;
        fakekey[0].ki.wScan = *it;
        SendInput(1, fakekey, sizeof(INPUT));
      }
    }
  }
  m_Output += sText;
}

int _findAfterChars2(const wchar_t* str, const wchar_t* chrs, int startPos) {
  const wchar_t* ptr = str + startPos;
  ptr += wcscspn(ptr, chrs);
  ptr += wcsspn(ptr, chrs);
  return ptr - str;
}

int _findBeforeChars2(const wchar_t* str, const wchar_t* chrs, int startPos) {
  const wchar_t* ptr = str + startPos;

  // over separators
  while (ptr > str && wcschr(chrs, *ptr)) {
    --ptr;
  }
  // over non separators
  while (ptr > str && !wcschr(chrs, *ptr)) {
    --ptr;
  }
  if (wcschr(chrs, *ptr))
    ++ptr;

  return max(0, ptr - str);
}

// amajorek: Add list of word and sentence separators to alphabet definitions. 
// And use that list instead of hardcoded. 
// Fix exery place where boundaries are needed.
const wchar_t* _wordSeparators2 = L" \t\v\f\r\n";
const wchar_t* _sentenceSeparators2 = L".?!\r\n";
const wchar_t* _paragraphSeparators2 = L"\r\n";


void CPopup::GetRange(bool bForwards, CControlManager::EditDistance iDist, int* pStart, int* pEnd) {
  int& iStart = *pStart;
  int& iEnd = *pEnd;

  switch (iDist) {
  case CControlManager::EDIT_CHAR:
    if (bForwards)
      iEnd = min(iEnd + 1, (int)SendMessage(WM_GETTEXTLENGTH, 0, 0));
    else
      iStart = max(iStart - 1, 0);
    break;

  case CControlManager::EDIT_LINE: {
    if (bForwards) {
      // Make it behave like the 'End' key, unless we're at the end of the current line.
      // Then go down a line.
      int iEndLine = SendMessage(EM_LINEFROMCHAR, iEnd, 0);
      int iNewEnd = SendMessage(EM_LINEINDEX, iEndLine + 1, 0) - 1; // end of this line
      // if we were already at the end so go down a line
      if (iNewEnd <= iEnd)
        iNewEnd = SendMessage(EM_LINEINDEX, iEndLine + 2, 0) - 1;
      // on last line go to the end of text 
      if (iNewEnd <= iEnd)
        iNewEnd = SendMessage(WM_GETTEXTLENGTH, 0, 0);
      iEnd = max(0, iNewEnd);
    }
    else {
      int iStartLine = SendMessage(EM_LINEFROMCHAR, iStart, 0);
      int iNewStart = SendMessage(EM_LINEINDEX, iStartLine, 0); // start of this line
      // if we were already at the start so go up a line
      iStart = (iNewStart == iStart && iStartLine>0) ? SendMessage(EM_LINEINDEX, iStartLine - 1, 0) : iNewStart;
    }
    break;
  }

  case CControlManager::EDIT_FILE:
    if (bForwards)
      iEnd = SendMessage(WM_GETTEXTLENGTH, 0, 0);
    else
      iStart = 0;
    break;

  case CControlManager::EDIT_WORD:
  case CControlManager::EDIT_SENTENCE:
  case CControlManager::EDIT_PARAGRAPH:
  {
    const wchar_t* separators = L"";
    if (iDist == CControlManager::EDIT_WORD)
      separators = _wordSeparators2;
    else if (iDist == CControlManager::EDIT_SENTENCE)
      separators = _sentenceSeparators2;
    else if (iDist == CControlManager::EDIT_PARAGRAPH)
      separators = _paragraphSeparators2;

    CString wideText;
    GetWindowText(wideText);
    if (bForwards)
      iEnd = _findAfterChars2(wideText, separators, iEnd);
    else
      iStart = _findBeforeChars2(wideText, separators, max(0, iStart-1));
    break;
  }
  }
}

std::string CPopup::GetTextAroundCursor(CControlManager::EditDistance iDist) {
  int iStart = 0;
  int iEnd = 0;
  SendMessage(EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);
  if (iStart == iEnd) { // Ignore distance if text is selected. 
    GetRange(true, iDist, &iStart, &iEnd);
    iStart = iEnd;
    GetRange(false, iDist, &iStart, &iEnd);
  }
  CString wideText;
  GetWindowText(wideText);

  return wstring_to_UTF8string(wideText.Mid(iStart, iEnd-iStart));
}

unsigned int CPopup::OffsetAfterMove(unsigned int offsetBefore, bool bForwards, CControlManager::EditDistance iDist) {
  int iStart, iEnd;
  iStart = iEnd = offsetBefore;
  GetRange(bForwards, iDist, &iStart, &iEnd);
  return bForwards ? iEnd : iStart;
}

int CPopup::Move(bool bForwards, CControlManager::EditDistance iDist) {
  int iStart = 0;
  int iEnd = 0;
  SendMessage(EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);
  if (iStart == iEnd) // Ignore distance if text is selected. 
    GetRange(bForwards, iDist, &iStart, &iEnd);
  int pos = bForwards ? iEnd : iStart;
  SendMessage(EM_SETSEL, (WPARAM)pos, (LPARAM)pos);
  SendMessage(EM_SCROLLCARET, 0, 0); //scroll the caret into view!
  return pos;
}

int CPopup::Delete(bool bForwards, CControlManager::EditDistance iDist) {
  int iStart = 0;
  int iEnd = 0;
  SendMessage(EM_GETSEL, (WPARAM)&iStart, (LPARAM)&iEnd);
  if (iStart == iEnd) // Ignore distance if text is selected. 
    GetRange(bForwards, iDist, &iStart, &iEnd);
  SendMessage(EM_SETSEL, (WPARAM)iStart, (LPARAM)iEnd);
  SendMessage(EM_REPLACESEL, (WPARAM)true, (LPARAM)TEXT(""));
  SendMessage(EM_SCROLLCARET, 0, 0); //scroll the caret into view!
  return min(iStart, iEnd);
}

/////////////////////////////////////////////////////////////////////////////

void CPopup::SetKeyboardTarget(HWND hwnd)
{
  m_bForwardKeyboard = true;
  m_hTarget = hwnd;
}

void CPopup::InsertText(Tstring InsertText) {
  SendMessage(EM_REPLACESEL, TRUE, (LPARAM) InsertText.c_str());
}

/// Delete text from the editbox

void CPopup::deletetext(const std::string &sText) {
  // Lookup the unicode string that we need to delete - we only actually 
  // need the length of the string, but this is important eg for newline
  // characters which are actually two symbols

  wstring String;
  WinUTF8::UTF8string_to_wstring(sText, String);

  int iLength(String.size());

  // Get the start and end of the current selection, and decrement the start
  // by the number of characters to be deleted

  DWORD start, finish;
  SendMessage(EM_GETSEL, (LONG) & start, (LONG) & finish);
  start -= iLength;
  SendMessage(EM_SETSEL, (LONG) start, (LONG) finish);

  // Replace the selection with a null string

  TCHAR out[2];
  wsprintf(out, TEXT(""));
  SendMessage(EM_REPLACESEL, TRUE, (LONG) out);

  // FIXME - I *think* we still only want to send one keyboard event to delete a 
  // newline pair, but we're now assuming we'll never have two real characters for
  // a single symbol

if(m_pAppSettings->GetLongParameter(APP_LP_STYLE) == APP_STYLE_DIRECT) {

    fakekey[0].type = fakekey[1].type = INPUT_KEYBOARD;
    fakekey[0].ki.wVk = fakekey[1].ki.wVk = VK_BACK;
    fakekey[0].ki.time = fakekey[1].ki.time = 0;
    fakekey[1].ki.dwFlags = KEYEVENTF_KEYUP;

	::SetFocus(targetwindow);
    SendInput(2, fakekey, sizeof(INPUT));
  }

  // And the output buffer (?)
  if(m_Output.length() >= iLength) {
    m_Output.resize(m_Output.length() - iLength);
  }
}

void CPopup::SetNewWithDate(bool bNewWithDate) {
  if(m_FilenameGUI)
    m_FilenameGUI->SetNewWithDate(bNewWithDate);
}

void CPopup::HandleParameterChange(int iParameter) {
  switch(iParameter) {
  case APP_SP_EDIT_FONT:
  case APP_LP_EDIT_FONT_SIZE:
    SetFont(m_pAppSettings->GetStringParameter(APP_SP_EDIT_FONT), m_pAppSettings->GetLongParameter(APP_LP_EDIT_FONT_SIZE));
    break;
  default:
    break;
  }
}
