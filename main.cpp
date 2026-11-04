#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <strsafe.h>
#include <richedit.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int ID_EDIT = 101, ID_NEW = 102, ID_COLOR = 103, ID_DELETE = 104, ID_QUIT = 105;
constexpr int ID_TOP = 110, ID_TRANSPARENT = 111, ID_BOLD = 112, ID_ITALIC = 113, ID_UNDERLINE = 114, ID_LARGER = 115, ID_SMALLER = 116, ID_BULLET = 117, ID_FIND = 118, ID_EXPORT = 119, ID_DEFAULT = 120, ID_ARRANGE = 121, ID_IMPORT = 122;
constexpr int ID_ATTACH = 123, ID_ATTACHMENT_BASE = 500, ID_REMOVE_ATTACHMENT_BASE = 520, ID_SAVE_ATTACHMENT_BASE = 540;
constexpr int ID_ABOUT = 124;
const ACCEL noteShortcuts[] = {
    {FVIRTKEY|FCONTROL,'N',ID_NEW}, {FVIRTKEY|FCONTROL,'F',ID_FIND},
    {FVIRTKEY|FCONTROL,'B',ID_BOLD}, {FVIRTKEY|FCONTROL,'I',ID_ITALIC}, {FVIRTKEY|FCONTROL,'U',ID_UNDERLINE},
    {FVIRTKEY|FCONTROL|FSHIFT,'L',ID_BULLET},
    {FVIRTKEY|FCONTROL,VK_ADD,ID_LARGER}, {FVIRTKEY|FCONTROL,VK_OEM_PLUS,ID_LARGER},
    {FVIRTKEY|FCONTROL|FSHIFT,VK_OEM_PLUS,ID_LARGER},
    {FVIRTKEY|FCONTROL,VK_SUBTRACT,ID_SMALLER}, {FVIRTKEY|FCONTROL,VK_OEM_MINUS,ID_SMALLER}
};
constexpr UINT_PTR SAVE_TIMER = 1;
constexpr UINT UPDATE_SCROLLBAR = WM_APP + 1;
constexpr int HEADER = 24, BORDER = 4, COLLAPSED_HEIGHT = 26;
constexpr size_t MAX_NOTE_TEXT = 1024 * 1024;
const COLORREF colors[] = {RGB(255, 244, 153), RGB(180, 239, 184), RGB(161, 237, 247), RGB(255, 193, 211), RGB(221, 198, 250), RGB(235, 235, 230)};
const wchar_t* colorNames[] = {L"Gelb", L"Grün", L"Blau", L"Rosa", L"Lila", L"Grau"};
struct Attachment { std::wstring name; std::vector<char> data; };
struct Note { HWND window = nullptr, edit = nullptr; HBRUSH brush = nullptr; int color = 0; bool collapsed = false, topmost = false, transparent = false; int expandedHeight = 260; int resizing = 0; POINT resizeStart{}; RECT originalRect{}; std::vector<Attachment> attachments; };
std::vector<std::unique_ptr<Note>> notes;
HINSTANCE instance;
HFONT font;
bool loading = true, canSave = true, quitting = false;
std::filesystem::path dataPath;
int defaultColor=0, defaultWidth=300, defaultHeight=260;
int defaultSize=240; DWORD defaultEffects=0;
bool defaultTop=false, defaultTransparent=false;
DWORD CALLBACK streamOut(DWORD_PTR cookie, LPBYTE data, LONG count, LONG* written) { auto& value=*reinterpret_cast<std::string*>(cookie); value.append(reinterpret_cast<char*>(data),count); *written=count; return 0; }
struct InputStream { const std::string& value; size_t pos=0; };
DWORD CALLBACK streamIn(DWORD_PTR cookie, LPBYTE data, LONG count, LONG* read) { auto& input=*reinterpret_cast<InputStream*>(cookie); size_t n=std::min(static_cast<size_t>(count),input.value.size()-input.pos); memcpy(data,input.value.data()+input.pos,n); input.pos+=n; *read=static_cast<LONG>(n); return 0; }
std::string getRtf(HWND edit) { std::string result; EDITSTREAM stream{}; stream.dwCookie=reinterpret_cast<DWORD_PTR>(&result); stream.pfnCallback=streamOut; SendMessageW(edit,EM_STREAMOUT,SF_RTF,reinterpret_cast<LPARAM>(&stream)); if(stream.dwError) result.clear(); return result; }
bool setRtf(HWND edit, const std::string& rtf) { InputStream input{rtf}; EDITSTREAM stream{}; stream.dwCookie=reinterpret_cast<DWORD_PTR>(&input); stream.pfnCallback=streamIn; SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&stream)); return stream.dwError==0; }

std::wstring normalizedEditorText(const std::wstring& text) {
    std::wstring result; result.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        if(text[i]==L'\r' && i+1<text.size() && text[i+1]==L'\n') { result+=L'\r'; ++i; }
        else result+=text[i];
    }
    return result;
}
std::wstring editorText(HWND edit) {
    int length=GetWindowTextLengthW(edit); std::wstring text(static_cast<size_t>(length)+1,L'\0');
    int copied=GetWindowTextW(edit,text.data(),length+1); text.resize(static_cast<size_t>(std::max(0,copied)));
    return normalizedEditorText(text);
}
bool restoreFormattedText(HWND edit,const std::string& rtf,std::wstring expected) {
    if(!setRtf(edit,rtf)) return false;
    while(!expected.empty() && expected.back()==L'\0') expected.pop_back();
    if(expected.find(L'\0')!=std::wstring::npos) return false;
    expected=normalizedEditorText(expected);
    std::wstring actual=editorText(edit);
    if(actual==expected) return true;
    struct Correction { LONG position; bool insert; };
    std::vector<Correction> corrections;
    size_t i=0,j=0;
    while(i<expected.size() || j<actual.size()) {
        if(i<expected.size() && j<actual.size() && expected[i]==actual[j]) { ++i; ++j; continue; }
        if(corrections.size()>=4096) return false;
        if(i<expected.size() && expected[i]==L' ') { corrections.push_back({static_cast<LONG>(j),true}); ++i; continue; }
        if(j<actual.size() && actual[j]==L' ') { corrections.push_back({static_cast<LONG>(j),false}); ++j; continue; }
        return false;
    }
    for(auto it=corrections.rbegin();it!=corrections.rend();++it) {
        CHARRANGE range{it->position,it->position+(it->insert?0:1)};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
        SendMessageW(edit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(it->insert?L" ":L""));
    }
    SendMessageW(edit,EM_EMPTYUNDOBUFFER,0,0);
    return editorText(edit)==expected;
}

void warn(const wchar_t* message) { MessageBoxW(nullptr, message, L"StickyNotes", MB_OK | MB_ICONWARNING); }
void scheduleSave() { if (!loading && canSave && !notes.empty()) SetTimer(notes.front()->window, SAVE_TIMER, 700, nullptr); }
template<class T> bool readValue(std::istream& in, T& value) { return bool(in.read(reinterpret_cast<char*>(&value), sizeof(value))); }
template<class T> void writeValue(std::ostream& out, const T& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); }

class BinaryWriter {
    HANDLE file;
    bool valid=true;
public:
    explicit BinaryWriter(HANDLE handle):file(handle) {}
    BinaryWriter(const BinaryWriter&)=delete;
    ~BinaryWriter() { if(file!=INVALID_HANDLE_VALUE) CloseHandle(file); }
    void write(const char* data,size_t size) {
        while(valid && size) {
            DWORD part=static_cast<DWORD>(std::min<size_t>(size,1024u*1024u)), written=0;
            if(!WriteFile(file,data,part,&written,nullptr) || written!=part) { valid=false; break; }
            data+=written; size-=written;
        }
    }
    bool finish() {
        if(valid && !FlushFileBuffers(file)) valid=false;
        if(!CloseHandle(file)) valid=false;
        file=INVALID_HANDLE_VALUE; return valid;
    }
};
template<class T> void writeValue(BinaryWriter& out, const T& value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); }

bool save() {
    if (!canSave) return false;
    std::error_code ec;
    std::filesystem::create_directories(dataPath.parent_path(), ec);
    if(ec) return false;
    static unsigned long counter=0;
    std::filesystem::path temp; HANDLE file=INVALID_HANDLE_VALUE;
    for(int attempt=0;attempt<32;++attempt) {
        temp=dataPath; temp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++counter);
        file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE) break;
        DWORD error=GetLastError(); if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS) return false;
    }
    if(file==INVALID_HANDLE_VALUE) return false;
    BinaryWriter out(file);
    const uint32_t magic = 0x34544e53, count = static_cast<uint32_t>(notes.size());
    writeValue(out, magic); writeValue(out, count);
    int32_t dc=defaultColor,dw=defaultWidth,dh=defaultHeight; uint32_t df=(defaultTop?1u:0u)|(defaultTransparent?2u:0u);
    writeValue(out,dc); writeValue(out,dw); writeValue(out,dh); writeValue(out,df);
    int32_t ds=defaultSize; uint32_t de=defaultEffects; writeValue(out,ds); writeValue(out,de);
    for (const auto& n : notes) {
        RECT r{}; GetWindowRect(n->window, &r);
        int32_t x = r.left, y = r.top, w = r.right-r.left, h = n->collapsed ? n->expandedHeight : r.bottom-r.top, color = n->color;
        uint32_t collapsed = n->collapsed ? 1 : 0;
        int len = GetWindowTextLengthW(n->edit);
        std::wstring text(static_cast<size_t>(len) + 1, L'\0');
        int copied=GetWindowTextW(n->edit, text.data(), len + 1); text.resize(static_cast<size_t>(std::max(0,copied)));
        uint32_t size = static_cast<uint32_t>(text.size());
        writeValue(out, x); writeValue(out, y); writeValue(out, w); writeValue(out, h); writeValue(out, color); writeValue(out, collapsed); writeValue(out, size);
        out.write(reinterpret_cast<const char*>(text.data()), size * sizeof(wchar_t));
        uint32_t flags=(n->topmost?1u:0u)|(n->transparent?2u:0u); std::string rtf=getRtf(n->edit);
        if(rtf.empty() || rtf.size()>16u*1024*1024) { out.finish(); DeleteFileW(temp.c_str()); return false; }
        uint32_t rtfSize=static_cast<uint32_t>(rtf.size());
        writeValue(out,flags); writeValue(out,rtfSize); out.write(rtf.data(),rtfSize);
        uint32_t attachmentCount=static_cast<uint32_t>(n->attachments.size()); writeValue(out,attachmentCount);
        for(const auto& attachment:n->attachments) { uint32_t nameSize=static_cast<uint32_t>(attachment.name.size()), dataSize=static_cast<uint32_t>(attachment.data.size()); writeValue(out,nameSize); out.write(reinterpret_cast<const char*>(attachment.name.data()),nameSize*sizeof(wchar_t)); writeValue(out,dataSize); out.write(attachment.data.data(),dataSize); }
    }
    bool ok=out.finish();
    if (!ok || !MoveFileExW(temp.c_str(), dataPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(temp.c_str()); return false; }
    return true;
}
void saveOrWarn() { if (!save() && canSave) warn(L"Die Notizen konnten nicht gespeichert werden. Bitte prüfe den verfügbaren Speicherplatz und die Zugriffsrechte."); }
void setColor(Note* n, int color) {
    n->color = color;
    if (n->brush) DeleteObject(n->brush);
    n->brush = CreateSolidBrush(colors[color]);
    SendMessageW(n->edit, EM_SETBKGNDCOLOR, 0, colors[color]);
    InvalidateRect(n->window, nullptr, TRUE); InvalidateRect(n->edit, nullptr, TRUE);
}
void layout(Note* n) {
    RECT r{}; GetClientRect(n->window, &r);
    int width = r.right, height = r.bottom;
    ShowWindow(n->edit, n->collapsed ? SW_HIDE : SW_SHOW);
    if (!n->collapsed) MoveWindow(n->edit, 12, HEADER + 4, std::max(0, width-24), std::max(0, height-HEADER-16), TRUE);
}
void createNote(int x, int y, int width, int height, int color, const std::wstring& text, bool collapsed = false);
void applyAppearance(Note* n) {
    SetWindowPos(n->window,n->topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    LONG_PTR style=GetWindowLongPtrW(n->window,GWL_EXSTYLE);
    SetWindowLongPtrW(n->window,GWL_EXSTYLE,n->transparent?style|WS_EX_LAYERED:style&~static_cast<LONG_PTR>(WS_EX_LAYERED));
    if(n->transparent) SetLayeredWindowAttributes(n->window,0,220,LWA_ALPHA);
    InvalidateRect(n->window,nullptr,TRUE); scheduleSave();
}
void toggleCollapse(Note* n) {
    RECT r{}; GetWindowRect(n->window, &r);
    if (!n->collapsed) n->expandedHeight = r.bottom-r.top;
    n->collapsed = !n->collapsed;
    SetWindowPos(n->window, nullptr, 0, 0, r.right-r.left, n->collapsed ? COLLAPSED_HEIGHT : n->expandedHeight, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    layout(n); InvalidateRect(n->window, nullptr, TRUE); scheduleSave();
}
enum class NewFileResult { ok, alreadyExists, writeFailed };
NewFileResult writeNewFile(const std::filesystem::path& destination, const void* data, size_t size) {
    HANDLE file=CreateFileW(destination.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) { DWORD error=GetLastError(); return error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS ? NewFileResult::alreadyExists : NewFileResult::writeFailed; }
    const char* current=static_cast<const char*>(data); bool ok=true;
    while(size && ok) {
        DWORD part=static_cast<DWORD>(std::min<size_t>(size,1024u*1024u)),written=0;
        ok=WriteFile(file,current,part,&written,nullptr) && written==part;
        current+=written; size-=written;
    }
    if(ok) ok=FlushFileBuffers(file)!=FALSE;
    if(!ok) { FILE_DISPOSITION_INFO disposition{TRUE}; SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition)); }
    if(!CloseHandle(file)) ok=false;
    return ok ? NewFileResult::ok : NewFileResult::writeFailed;
}
bool isDataFilePath(const std::filesystem::path& destination) {
    std::error_code ec;
    auto target=std::filesystem::absolute(destination,ec).lexically_normal(); if(ec) return true;
    auto database=std::filesystem::absolute(dataPath,ec).lexically_normal(); if(ec) return true;
    if(_wcsicmp(target.c_str(),database.c_str())==0) return true;
    return std::filesystem::equivalent(target,database,ec);
}
bool exportText(Note* n, HWND owner) {
    wchar_t filename[32768] = L"Notiz.txt";
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=owner;
    dialog.lpstrFilter=L"Textdatei (*.txt)\0*.txt\0Alle Dateien\0*.*\0";
    dialog.lpstrFile=filename; dialog.nMaxFile=32768; dialog.lpstrDefExt=L"txt";
    dialog.lpstrTitle=L"Notiz als Textdatei speichern";
    dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    HMODULE library=LoadLibraryW(L"comdlg32.dll");
    if (!library) { warn(L"Der Speicherdialog konnte nicht geöffnet werden."); return false; }
    auto getSave=reinterpret_cast<BOOL (WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(library,"GetSaveFileNameW"));
    bool selected=getSave && getSave(&dialog); FreeLibrary(library);
    if (!selected) return false;
    if (isDataFilePath(std::filesystem::path(filename))) {
        warn(L"Bitte wähle eine andere Datei als die Notizdatenbank."); return false;
    }
    int len=GetWindowTextLengthW(n->edit);
    std::wstring text(static_cast<size_t>(len)+1,L'\0'); int copied=GetWindowTextW(n->edit,text.data(),len+1); text.resize(static_cast<size_t>(std::max(0,copied)));
    len=static_cast<int>(text.size());
    int bytes=len ? WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),len,nullptr,0,nullptr,nullptr) : 0;
    if (len && !bytes) { warn(L"Der Text konnte nicht als UTF-8 gespeichert werden. Die Notiz bleibt erhalten."); return false; }
    std::string utf8="\xef\xbb\xbf"; utf8.resize(static_cast<size_t>(bytes)+3);
    if (bytes && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),len,utf8.data()+3,bytes,nullptr,nullptr)) { warn(L"Der Text konnte nicht als UTF-8 gespeichert werden."); return false; }
    NewFileResult result=writeNewFile(std::filesystem::path(filename),utf8.data(),utf8.size());
    if(result==NewFileResult::alreadyExists) warn(L"Diese Datei existiert bereits. Bitte wähle einen neuen Namen, damit nichts überschrieben wird.");
    else if(result!=NewFileResult::ok) warn(L"Die Textdatei konnte nicht gespeichert werden. Die Notiz bleibt erhalten.");
    return result==NewFileResult::ok;
}
bool appendImportedText(Note* n, const std::wstring& text) {
    if (text.find(L'\0') != std::wstring::npos) return false;
    GETTEXTLENGTHEX query{}; query.flags=GTL_NUMCHARS|GTL_PRECISE; query.codepage=1200;
    LRESULT length=SendMessageW(n->edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&query),0);
    if (length < 0 || static_cast<size_t>(length) > MAX_NOTE_TEXT || text.size() > MAX_NOTE_TEXT-static_cast<size_t>(length)) return false;
    SendMessageW(n->edit,EM_SETSEL,-1,-1);
    SendMessageW(n->edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
    scheduleSave(); return true;
}
bool importText(Note* n) {
    wchar_t filename[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=n->window;
    dialog.lpstrFilter=L"Textdatei (*.txt)\0*.txt\0Alle Dateien\0*.*\0"; dialog.lpstrFile=filename; dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR; dialog.lpstrTitle=L"Textdatei importieren";
    HMODULE library=LoadLibraryW(L"comdlg32.dll"); if (!library) return false;
    auto open=reinterpret_cast<BOOL (WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(library,"GetOpenFileNameW")); bool selected=open && open(&dialog); FreeLibrary(library);
    if (!selected) return false;
    std::ifstream file(std::filesystem::path(filename),std::ios::binary|std::ios::ate);
    if (!file || file.tellg()<0 || file.tellg()>4*1024*1024) { warn(L"Die Textdatei konnte nicht gelesen werden oder ist zu groß."); return false; }
    auto length=static_cast<size_t>(file.tellg()); file.seekg(0); std::string data(length,'\0'); if (length && !file.read(data.data(),length)) { warn(L"Die Textdatei konnte nicht gelesen werden."); return false; }
    size_t offset=(length>=3 && static_cast<unsigned char>(data[0])==0xef && static_cast<unsigned char>(data[1])==0xbb && static_cast<unsigned char>(data[2])==0xbf)?3:0;
    int chars=length>offset?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data.data()+offset,static_cast<int>(length-offset),nullptr,0):0;
    if (length>offset && !chars) { warn(L"Bitte eine UTF-8-Textdatei auswählen."); return false; }
    std::wstring text(static_cast<size_t>(chars),L'\0'); if (chars) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data.data()+offset,static_cast<int>(length-offset),text.data(),chars);
    if (!appendImportedText(n,text)) { warn(L"Die Datei enthält ungültige Nullzeichen oder passt nicht vollständig in die Notiz. Es wurde nichts importiert."); return false; }
    return true;
}
bool supportedAttachment(const std::wstring& name) {
    std::wstring ext=std::filesystem::path(name).extension().wstring(); std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
    return ext==L".png" || ext==L".jpg" || ext==L".jpeg" || ext==L".gif" || ext==L".bmp" || ext==L".pdf";
}
enum class AttachmentResult { ok, invalidFormat, tooMany, tooLarge, totalLimit, readFailed, saveFailed, alreadyExists, writeFailed };
AttachmentResult attachFile(Note* note, const std::filesystem::path& path) {
    if(note->attachments.size()>=10) return AttachmentResult::tooMany;
    std::wstring name=path.filename().wstring();
    if(name.empty() || name.size()>255 || !supportedAttachment(name)) return AttachmentResult::invalidFormat;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return AttachmentResult::readFailed;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<0) { CloseHandle(file); return AttachmentResult::readFailed; }
    if(size.QuadPart>10*1024*1024) { CloseHandle(file); return AttachmentResult::tooLarge; }
    size_t total=static_cast<size_t>(size.QuadPart);
    for(const auto& existingNote:notes) for(const auto& attachment:existingNote->attachments) total+=attachment.data.size();
    if(total>100u*1024*1024) { CloseHandle(file); return AttachmentResult::totalLimit; }
    Attachment attachment; attachment.name=std::move(name); attachment.data.resize(static_cast<size_t>(size.QuadPart));
    DWORD bytesRead=0; bool ok=attachment.data.empty() || (ReadFile(file,attachment.data.data(),static_cast<DWORD>(attachment.data.size()),&bytesRead,nullptr) && bytesRead==attachment.data.size());
    if(!CloseHandle(file)) ok=false;
    if(!ok) return AttachmentResult::readFailed;
    note->attachments.push_back(std::move(attachment));
    if(!save()) { note->attachments.pop_back(); return AttachmentResult::saveFailed; }
    return AttachmentResult::ok;
}
bool removeAttachment(Note* note, size_t index) {
    if(index>=note->attachments.size() || !canSave) return false;
    Attachment removed=std::move(note->attachments[index]);
    note->attachments.erase(note->attachments.begin()+static_cast<std::ptrdiff_t>(index));
    if(!save()) { note->attachments.insert(note->attachments.begin()+static_cast<std::ptrdiff_t>(index),std::move(removed)); return false; }
    return true;
}
AttachmentResult writeAttachmentCopy(const Attachment& attachment, const std::filesystem::path& destination) {
    NewFileResult result=writeNewFile(destination,attachment.data.data(),attachment.data.size());
    return result==NewFileResult::ok ? AttachmentResult::ok : result==NewFileResult::alreadyExists ? AttachmentResult::alreadyExists : AttachmentResult::writeFailed;
}
void addAttachment(Note* n) {
    if(n->attachments.size()>=10) { warn(L"Eine Notiz kann höchstens zehn Anlagen enthalten."); return; }
    wchar_t filename[32768]{}; OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=n->window;
    dialog.lpstrFilter=L"Bilder und PDF\0*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.pdf\0";
    dialog.lpstrFile=filename; dialog.nMaxFile=32768; dialog.lpstrTitle=L"Bild oder PDF hinzufügen"; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    HMODULE library=LoadLibraryW(L"comdlg32.dll"); if(!library) return;
    auto open=reinterpret_cast<BOOL (WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(library,"GetOpenFileNameW")); bool selected=open && open(&dialog); FreeLibrary(library); if(!selected) return;
    switch(attachFile(n,std::filesystem::path(filename))) {
    case AttachmentResult::ok: return;
    case AttachmentResult::tooMany: warn(L"Eine Notiz kann höchstens zehn Anlagen enthalten."); return;
    case AttachmentResult::invalidFormat: warn(L"Bitte ein Bild oder eine PDF-Datei mit einem Dateinamen bis 255 Zeichen auswählen."); return;
    case AttachmentResult::tooLarge: warn(L"Die Anlage darf höchstens 10 MB groß sein."); return;
    case AttachmentResult::totalLimit: warn(L"Die Anlagen aller Notizen dürfen zusammen höchstens 100 MB belegen."); return;
    case AttachmentResult::saveFailed: warn(L"Die Anlage konnte nicht in der Notiz gespeichert werden. Die Originaldatei bleibt unverändert."); return;
    default: warn(L"Die Anlage konnte nicht vollständig gelesen werden. Die Originaldatei bleibt unverändert."); return;
    }
}
void saveAttachmentAs(Note* note, size_t index) {
    if(index>=note->attachments.size()) return;
    wchar_t filename[32768]{}; if(FAILED(StringCchCopyW(filename,32768,note->attachments[index].name.c_str()))) return;
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=note->window;
    dialog.lpstrFilter=L"Alle Dateien\0*.*\0"; dialog.lpstrFile=filename; dialog.nMaxFile=32768;
    std::wstring extension=std::filesystem::path(note->attachments[index].name).extension().wstring();
    if(!extension.empty()) { extension.erase(0,1); dialog.lpstrDefExt=extension.c_str(); }
    dialog.lpstrTitle=L"Anlage als neue Datei speichern"; dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    HMODULE library=LoadLibraryW(L"comdlg32.dll"); if(!library) { warn(L"Der Speicherdialog konnte nicht geöffnet werden."); return; }
    auto saveDialog=reinterpret_cast<BOOL (WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(library,"GetSaveFileNameW"));
    bool selected=saveDialog && saveDialog(&dialog); FreeLibrary(library); if(!selected) return;
    AttachmentResult result=writeAttachmentCopy(note->attachments[index],std::filesystem::path(filename));
    if(result==AttachmentResult::alreadyExists) warn(L"Diese Datei existiert bereits. Bitte wähle einen neuen Dateinamen, damit nichts überschrieben wird.");
    else if(result!=AttachmentResult::ok) warn(L"Die Anlage konnte nicht gespeichert werden.");
}
bool createTemporaryAttachmentCopy(const Attachment& attachment, std::filesystem::path& output) {
    wchar_t directory[MAX_PATH]{}; DWORD length=GetTempPathW(MAX_PATH,directory);
    if(!length || length>=MAX_PATH) return false;
    for(int attempt=0;attempt<16;++attempt) {
        wchar_t placeholder[MAX_PATH]{};
        if(!GetTempFileNameW(directory,L"snt",0,placeholder)) return false;
        auto candidate=std::filesystem::path(placeholder).replace_extension(std::filesystem::path(attachment.name).extension());
        AttachmentResult result=writeAttachmentCopy(attachment,candidate);
        DeleteFileW(placeholder);
        if(result==AttachmentResult::ok) { output=std::move(candidate); return true; }
        if(result!=AttachmentResult::alreadyExists) return false;
    }
    return false;
}
void openAttachment(Note* n, size_t index) {
    if(index>=n->attachments.size()) return;
    const auto& attachment=n->attachments[index]; std::filesystem::path output;
    if(!createTemporaryAttachmentCopy(attachment,output)) { warn(L"Die Anlage konnte nicht vorbereitet werden."); return; }
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(n->window,L"open",output.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32) warn(L"Für die Anlage konnte kein Programm geöffnet werden.");
}
void formatSelection(Note* n, int command) {
    if (command==ID_BULLET) {
        PARAFORMAT p{}; p.cbSize=sizeof(p); p.dwMask=PFM_NUMBERING; SendMessageW(n->edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));
        p.wNumbering=p.wNumbering==PFN_BULLET?0:PFN_BULLET; SendMessageW(n->edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));
    } else {
        CHARFORMATW f{}; f.cbSize=sizeof(f); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&f));
        if (command==ID_LARGER || command==ID_SMALLER) { f.dwMask=CFM_SIZE; f.yHeight=std::clamp<LONG>(f.yHeight+(command==ID_LARGER?40:-40),120,1440); }
        else { DWORD bit=command==ID_BOLD?CFE_BOLD:command==ID_ITALIC?CFE_ITALIC:CFE_UNDERLINE; f.dwMask=bit; f.dwEffects^=bit; }
        SendMessageW(n->edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&f));
    }
    SetFocus(n->edit); scheduleSave();
}
struct SearchDialog { Note* start; HWND edit=nullptr; bool done=false; HWND found=nullptr; };
LRESULT CALLBACK searchProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* s=reinterpret_cast<SearchDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { s=static_cast<SearchDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s)); }
    if(!s) return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_CREATE) {
        s->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,18,18,330,26,hwnd,reinterpret_cast<HMENU>(401),instance,nullptr);
        HWND one=CreateWindowW(L"BUTTON",L"Diese Notiz",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,58,104,32,hwnd,reinterpret_cast<HMENU>(402),instance,nullptr);
        HWND all=CreateWindowW(L"BUTTON",L"Alle Notizen",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,129,58,110,32,hwnd,reinterpret_cast<HMENU>(403),instance,nullptr);
        HWND close=CreateWindowW(L"BUTTON",L"Schließen",WS_CHILD|WS_VISIBLE|WS_TABSTOP,246,58,102,32,hwnd,reinterpret_cast<HMENU>(IDCANCEL),instance,nullptr);
        for(HWND child : {s->edit,one,all,close}) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return 0;
    }
    if(msg==WM_COMMAND && (LOWORD(wp)==402 || LOWORD(wp)==403)) {
        wchar_t query[512]{}; GetWindowTextW(s->edit,query,512); if(!*query) return 0;
        auto first=std::find_if(notes.begin(),notes.end(),[&](const auto& n){return n.get()==s->start;});
        size_t start=first==notes.end()?0:static_cast<size_t>(first-notes.begin()), count=LOWORD(wp)==402?1:notes.size();
        for(size_t i=0;i<count && !notes.empty();++i) { Note* n=notes[(start+i)%notes.size()].get(); FINDTEXTEXW f{}; f.chrg.cpMin=0; f.chrg.cpMax=-1; f.lpstrText=query;
            if(SendMessageW(n->edit,EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&f))!=-1) {
                if(n->collapsed) toggleCollapse(n); SendMessageW(n->edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&f.chrgText)); SendMessageW(n->edit,EM_SCROLLCARET,0,0);
                s->found=n->window; s->done=true; DestroyWindow(hwnd); return 0;
            }
        }
        MessageBoxW(hwnd,L"Kein Treffer gefunden.",L"Suchen",MB_OK|MB_ICONINFORMATION); return 0;
    }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(wp)==IDCANCEL)) { s->done=true; DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void searchNotes(Note* n) {
    static bool registered=false; if(!registered) { WNDCLASSW wc{}; wc.lpfnWndProc=searchProc; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesSearch"; wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); if(!RegisterClassW(&wc)) return; registered=true; }
    SearchDialog s{n}; RECT r{}; GetWindowRect(n->window,&r); HWND hwnd=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"StickyNotesSearch",L"Notizen durchsuchen",WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left+20,r.top+20,382,145,n->window,nullptr,instance,&s); if(!hwnd) return;
    std::vector<HWND> disabled; for(const auto& note:notes) { disabled.push_back(note->window); EnableWindow(note->window,FALSE); }
    ShowWindow(hwnd,SW_SHOW); SetFocus(s.edit); MSG msg{}; while(!s.done) { int result=GetMessageW(&msg,nullptr,0,0); if(result<=0) { if(result==0) PostQuitMessage(static_cast<int>(msg.wParam)); break; } if(!IsDialogMessageW(hwnd,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } } if(IsWindow(hwnd)) DestroyWindow(hwnd);
    for(HWND window:disabled) if(IsWindow(window)) EnableWindow(window,TRUE);
    if(s.found && IsWindow(s.found)) { SetForegroundWindow(s.found); SetFocus(GetDlgItem(s.found,ID_EDIT)); }
}
void drawDialogButton(const DRAWITEMSTRUCT& item) {
    HDC dc=item.hDC; RECT r=item.rcItem; COLORREF background=RGB(241,242,244), border=RGB(201,204,210), text=RGB(44,48,56);
    if(item.CtlID==301) { background=RGB(50,55,64); border=background; text=RGB(255,255,255); }
    if(item.CtlID==302) { background=RGB(255,225,225); border=RGB(231,157,157); text=RGB(158,39,43); }
    if(item.CtlID==502) { background=RGB(43,47,56); border=background; text=RGB(255,255,255); }
    if(item.itemState&ODS_SELECTED) { background=RGB(GetRValue(background)*9/10,GetGValue(background)*9/10,GetBValue(background)*9/10); }
    HBRUSH brush=CreateSolidBrush(background); HPEN pen=CreatePen(PS_SOLID,1,border);
    HGDIOBJ oldBrush=SelectObject(dc,brush), oldPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right-1,r.bottom-1,16,16);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
    wchar_t label[256]{}; GetWindowTextW(item.hwndItem,label,256); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,text);
    HGDIOBJ oldFont=SelectObject(dc,font); DrawTextW(dc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); SelectObject(dc,oldFont);
    if(item.itemState&ODS_FOCUS) { InflateRect(&r,-7,-6); DrawFocusRect(dc,&r); }
}
RECT dialogPosition(HWND parent, int width, int height) {
    RECT p{}; GetWindowRect(parent,&p); RECT work{};
    MONITORINFO info{}; info.cbSize=sizeof(info); HMONITOR monitor=MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST);
    if(GetMonitorInfoW(monitor,&info)) work=info.rcWork; else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int x=p.left+(p.right-p.left-width)/2, y=p.top+(p.bottom-p.top-height)/2;
    x=std::max<int>(work.left,std::min<int>(x,work.right-width)); y=std::max<int>(work.top,std::min<int>(y,work.bottom-height));
    return {x,y,x+width,y+height};
}
struct DeleteDialog { Note* note; bool finished=false, remove=false; HFONT titleFont=nullptr; };
LRESULT CALLBACK deleteDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state=reinterpret_cast<DeleteDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { state=static_cast<DeleteDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state)); }
    if (!state) return DefWindowProcW(hwnd,msg,wp,lp);
    if (msg==WM_CREATE) {
        auto add=[&](const wchar_t* kind,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id) -> HWND {
            HWND child=CreateWindowW(kind,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
            return child;
        };
        state->titleFont=CreateFontW(-20,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HWND title=add(L"STATIC",L"Diese Notiz löschen?",0,28,72,330,28,0);
        if(state->titleFont) SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(state->titleFont),TRUE);
        add(L"STATIC",L"Der gespeicherte Inhalt wird entfernt. Du kannst ihn vorher als Textdatei sichern.",0,28,108,330,50,0);
        add(L"BUTTON",L"Als Textdatei sichern und löschen …",WS_TABSTOP|BS_OWNERDRAW,28,170,330,38,301);
        add(L"BUTTON",L"Notiz löschen",WS_TABSTOP|BS_OWNERDRAW,28,216,330,38,302);
        add(L"BUTTON",L"Abbrechen",WS_TABSTOP|BS_OWNERDRAW,28,262,330,38,IDCANCEL);
        return 0;
    }
    if(msg==WM_PAINT) { PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); HBRUSH b=CreateSolidBrush(RGB(255,233,143)); HPEN p=CreatePen(PS_SOLID,1,RGB(218,190,96)); auto oldB=SelectObject(dc,b); auto oldP=SelectObject(dc,p); RoundRect(dc,28,20,68,60,9,9); SelectObject(dc,oldB); SelectObject(dc,oldP); DeleteObject(b); DeleteObject(p); EndPaint(hwnd,&ps); return 0; }
    if(msg==WM_DRAWITEM) { drawDialogButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE; }
    if (msg==WM_CTLCOLORSTATIC) { SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH)); }
    if(msg==WM_DESTROY) { if(state->titleFont) DeleteObject(state->titleFont); state->titleFont=nullptr; return 0; }
    if (msg==WM_COMMAND) {
        switch(LOWORD(wp)) {
        case 301: if (!exportText(state->note,hwnd)) return 0; state->remove=true; break;
        case 302: state->remove=true; break;
        case IDCANCEL: break;
        default: return 0;
        }
        state->finished=true; DestroyWindow(hwnd); return 0;
    }
    if (msg==WM_CLOSE) { state->finished=true; DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
bool confirmDelete(Note* note) {
    static bool registered=false;
    if (!registered) {
        WNDCLASSW wc{}; wc.lpfnWndProc=deleteDialogProc; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesDeleteDialog";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        if (!RegisterClassW(&wc)) { warn(L"Der Bestätigungsdialog konnte nicht geöffnet werden."); return false; } registered=true;
    }
    DeleteDialog state{note}; RECT r=dialogPosition(note->window,394,365);
    HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"StickyNotesDeleteDialog",L"Notiz löschen",WS_POPUP|WS_CAPTION|WS_SYSMENU,
        r.left,r.top,394,365,note->window,nullptr,instance,&state);
    if (!dialog) { warn(L"Der Bestätigungsdialog konnte nicht geöffnet werden."); return false; }
    EnableWindow(note->window,FALSE); ShowWindow(dialog,SW_SHOW); SetFocus(GetDlgItem(dialog,IDCANCEL));
    MSG msg{}; while (!state.finished) {
        int result=GetMessageW(&msg,nullptr,0,0);
        if (result<=0) { if (result==0) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        if (!IsDialogMessageW(dialog,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    EnableWindow(note->window,TRUE); SetActiveWindow(note->window); return state.remove;
}
struct AboutDialog { bool finished=false; HFONT titleFont=nullptr; };
LRESULT CALLBACK aboutDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state=reinterpret_cast<AboutDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { state=static_cast<AboutDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state)); }
    if(!state) return DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_CREATE) {
        auto add=[&](const wchar_t* kind,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id) -> HWND {
            HWND child=CreateWindowW(kind,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return child;
        };
        state->titleFont=CreateFontW(-22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HWND title=add(L"STATIC",L"StickyNotes",0,92,29,265,30,0); if(state->titleFont) SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(state->titleFont),TRUE);
        add(L"STATIC",L"Version 1.0 · Haftnotizen für Windows",0,92,59,265,23,601);
        add(L"STATIC",L"Gedanken festhalten.\nEinfach, persönlich und portabel.",0,28,113,330,48,602);
        add(L"STATIC",L"© CyBeerBoy",0,28,177,330,24,603);
        add(L"BUTTON",L"GitHub-Profil öffnen  ↗",WS_TABSTOP|BS_OWNERDRAW,28,215,330,39,502);
        add(L"BUTTON",L"Schließen",WS_TABSTOP|BS_OWNERDRAW,28,263,330,38,IDCANCEL);
        return 0;
    }
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
        HBRUSH fill=CreateSolidBrush(RGB(255,232,133)); HPEN pen=CreatePen(PS_SOLID,1,RGB(221,194,94));
        HGDIOBJ oldBrush=SelectObject(dc,fill), oldPen=SelectObject(dc,pen); RoundRect(dc,28,25,76,73,12,12);
        MoveToEx(dc,39,41,nullptr); LineTo(dc,65,41); MoveToEx(dc,39,49,nullptr); LineTo(dc,62,49); MoveToEx(dc,39,57,nullptr); LineTo(dc,57,57);
        SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(fill); DeleteObject(pen);
        HPEN divider=CreatePen(PS_SOLID,1,RGB(232,234,237)); oldPen=SelectObject(dc,divider); MoveToEx(dc,28,96,nullptr); LineTo(dc,358,96); SelectObject(dc,oldPen); DeleteObject(divider);
        EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_DRAWITEM) { drawDialogButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE; }
    if(msg==WM_CTLCOLORSTATIC) { HDC dc=reinterpret_cast<HDC>(wp); SetBkMode(dc,TRANSPARENT); int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp)); if(id==601 || id==603) SetTextColor(dc,RGB(105,111,123)); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH)); }
    if(msg==WM_COMMAND && LOWORD(wp)==502) { if(reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",L"https://github.com/cybeerboy/",nullptr,nullptr,SW_SHOWNORMAL))<=32) MessageBoxW(hwnd,L"GitHub konnte nicht geöffnet werden.",L"StickyNotes",MB_OK|MB_ICONWARNING); return 0; }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(wp)==IDCANCEL)) { state->finished=true; DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { if(state->titleFont) DeleteObject(state->titleFont); state->titleFont=nullptr; return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void showAbout(Note* note) {
    static bool registered=false;
    if(!registered) { WNDCLASSW wc{}; wc.lpfnWndProc=aboutDialogProc; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesAboutDialog"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); if(!RegisterClassW(&wc)) { warn(L"Der Über-Dialog konnte nicht geöffnet werden."); return; } registered=true; }
    AboutDialog state; RECT r=dialogPosition(note->window,394,366);
    HWND dialog=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"StickyNotesAboutDialog",L"Über StickyNotes",WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left,r.top,394,366,note->window,nullptr,instance,&state);
    if(!dialog) { warn(L"Der Über-Dialog konnte nicht geöffnet werden."); return; }
    EnableWindow(note->window,FALSE); ShowWindow(dialog,SW_SHOW); SetFocus(GetDlgItem(dialog,IDCANCEL));
    MSG msg{}; while(!state.finished) { int result=GetMessageW(&msg,nullptr,0,0); if(result<=0) { if(result==0) PostQuitMessage(static_cast<int>(msg.wParam)); break; } if(!IsDialogMessageW(dialog,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    if(IsWindow(dialog)) DestroyWindow(dialog); EnableWindow(note->window,TRUE); SetActiveWindow(note->window);
}
void quit() {
    if (quitting) return;
    if (canSave && !save()) { warn(L"Die Notizen konnten nicht gespeichert werden. Die Anwendung bleibt geöffnet, damit du es erneut versuchen kannst."); return; }
    quitting = true; PostQuitMessage(0);
}
bool removeNote(Note* note) {
    if (!canSave) return false;
    auto it=std::find_if(notes.begin(),notes.end(),[note](const auto& item){ return item.get()==note; });
    if(it==notes.end()) return false;
    const auto index=static_cast<size_t>(it-notes.begin());
    KillTimer(note->window,SAVE_TIMER);
    auto removed=std::move(*it);
    notes.erase(it);
    if(!save()) {
        notes.insert(notes.begin()+static_cast<std::ptrdiff_t>(index),std::move(removed));
        return false;
    }
    DestroyWindow(removed->window);
    if(removed->brush) DeleteObject(removed->brush);
    if(notes.empty()) PostQuitMessage(0);
    return true;
}
LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Note* n = reinterpret_cast<Note*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) { n = static_cast<Note*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); n->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(n)); }
    if (!n) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCALCSIZE: return 0;
    case WM_CREATE: {
        n->edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL, 0,0,0,0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDIT)), instance, nullptr);
        if (!n->edit) return -1;
        SendMessageW(n->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(n->edit, EM_EXLIMITTEXT, 0, 1024 * 1024);
        SendMessageW(n->edit,EM_SETEVENTMASK,0,ENM_CHANGE);
        CHARFORMATW base{}; base.cbSize=sizeof(base); base.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE; base.dwEffects=defaultEffects; base.yHeight=defaultSize; lstrcpyW(base.szFaceName,L"Segoe UI"); SendMessageW(n->edit,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&base));
        setColor(n, n->color); layout(n); return 0;
    }
    case WM_SIZE: if (n->edit) { layout(n); PostMessageW(hwnd,UPDATE_SCROLLBAR,0,0); } InvalidateRect(hwnd, nullptr, TRUE); scheduleSave(); return 0;
    case WM_MOVE: scheduleSave(); return 0;
    case WM_GETMINMAXINFO: { auto* m = reinterpret_cast<MINMAXINFO*>(lp); m->ptMinTrackSize = {230, n->collapsed ? COLLAPSED_HEIGHT : 150}; return 0; }
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &p); RECT r{}; GetClientRect(hwnd, &r);
        bool l = p.x < BORDER, right = p.x >= r.right-BORDER, top = p.y < BORDER, bottom = p.y >= r.bottom-BORDER;
        if (top && l) return HTTOPLEFT; if (top && right) return HTTOPRIGHT; if (bottom && l) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
        if (l) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
        if (p.y < HEADER && (p.x < 27 || p.x >= r.right-52)) return HTCLIENT;
        if (p.y < HEADER) return HTCAPTION; return HTCLIENT;
    }
    case WM_NCLBUTTONDOWN:
        if (wp>=HTLEFT && wp<=HTBOTTOMRIGHT) {
            n->resizing=static_cast<int>(wp); GetCursorPos(&n->resizeStart); GetWindowRect(hwnd,&n->originalRect); SetCapture(hwnd); return 0;
        } break;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wp)==hwnd) {
            int hit=LOWORD(lp); LPCWSTR cursor=nullptr;
            if (hit==HTLEFT || hit==HTRIGHT) cursor=IDC_SIZEWE;
            else if (hit==HTTOP || hit==HTBOTTOM) cursor=IDC_SIZENS;
            else if (hit==HTTOPLEFT || hit==HTBOTTOMRIGHT) cursor=IDC_SIZENWSE;
            else if (hit==HTTOPRIGHT || hit==HTBOTTOMLEFT) cursor=IDC_SIZENESW;
            if (cursor) { SetCursor(LoadCursorW(nullptr,cursor)); return TRUE; }
        } break;
    case WM_MOUSEMOVE:
        if (n->resizing && GetCapture()==hwnd) {
            POINT p{}; GetCursorPos(&p); int dx=p.x-n->resizeStart.x, dy=p.y-n->resizeStart.y;
            RECT r=n->originalRect; int edge=n->resizing;
            if (edge==HTLEFT || edge==HTTOPLEFT || edge==HTBOTTOMLEFT) r.left=std::clamp<LONG>(r.left+dx,r.right-10000,r.right-230);
            if (edge==HTRIGHT || edge==HTTOPRIGHT || edge==HTBOTTOMRIGHT) r.right=std::clamp<LONG>(r.right+dx,r.left+230,r.left+10000);
            if (!n->collapsed) {
                if (edge==HTTOP || edge==HTTOPLEFT || edge==HTTOPRIGHT) r.top=std::clamp<LONG>(r.top+dy,r.bottom-10000,r.bottom-150);
                if (edge==HTBOTTOM || edge==HTBOTTOMLEFT || edge==HTBOTTOMRIGHT) r.bottom=std::clamp<LONG>(r.bottom+dy,r.top+150,r.top+10000);
            }
            SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE); return 0;
        } break;
    case WM_CAPTURECHANGED: n->resizing=0; return 0;
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) {
            toggleCollapse(n); return 0;
        } break;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        COLORREF c = colors[n->color];
        HBRUSH bar = CreateSolidBrush(RGB(GetRValue(c)*9/10, GetGValue(c)*9/10, GetBValue(c)*9/10));
        RECT header{0,0,r.right,HEADER}; FillRect(dc, &header, bar); DeleteObject(bar);
        HBRUSH outline = CreateSolidBrush(RGB(70,70,70)); FrameRect(dc, &r, outline); DeleteObject(outline);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(40,40,40)); SelectObject(dc, font);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70,70,70)); HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 9, 12, nullptr); LineTo(dc, 17, 12); MoveToEx(dc, 13, 8, nullptr); LineTo(dc, 13, 17);
        int cx = r.right-39;
        if (n->collapsed) { MoveToEx(dc,cx-4,14,nullptr); LineTo(dc,cx,10); LineTo(dc,cx+4,14); }
        else { MoveToEx(dc,cx-4,10,nullptr); LineTo(dc,cx,14); LineTo(dc,cx+4,10); }
        for (int x=r.right-20; x<=r.right-12; x+=4) { MoveToEx(dc,x,12,nullptr); LineTo(dc,x+1,12); }
        SelectObject(dc, oldPen); DeleteObject(pen);
        if (n->collapsed) {
            int len=GetWindowTextLengthW(n->edit); std::wstring text(static_cast<size_t>(len)+1,L'\0');
            GetWindowTextW(n->edit,text.data(),len+1); text.resize(len);
            for (auto& ch : text) if (ch==L'\r' || ch==L'\n' || ch==L'\t') ch=L' ';
            if (text.empty()) text=L"Leere Notiz";
            RECT title{30,0,r.right-54,HEADER}; DrawTextW(dc,text.c_str(),-1,&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_LBUTTONUP:
        if (n->resizing) { n->resizing=0; ReleaseCapture(); scheduleSave(); return 0; }
        if (GET_Y_LPARAM(lp) < HEADER) {
            RECT r{}; GetClientRect(hwnd, &r); int x = GET_X_LPARAM(lp);
            if (x < 27) { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_NEW, BN_CLICKED), 0); return 0; }
            if (x < r.right-27) { toggleCollapse(n); return 0; }
            HMENU menu = CreatePopupMenu(), palette = CreatePopupMenu(), appearance=CreatePopupMenu(), files=CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_NEW, L"Neue Notiz\tStrg+N");
            AppendMenuW(menu, MF_STRING, ID_FIND, L"Notizen durchsuchen …\tStrg+F");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            for (int i=0; i<6; ++i) AppendMenuW(palette, MF_STRING | (i==n->color ? MF_CHECKED : 0), 200+i, colorNames[i]);
            HMENU formatting=CreatePopupMenu();
            AppendMenuW(formatting,MF_STRING,ID_BOLD,L"Fett\tStrg+B"); AppendMenuW(formatting,MF_STRING,ID_ITALIC,L"Kursiv\tStrg+I"); AppendMenuW(formatting,MF_STRING,ID_UNDERLINE,L"Unterstrichen\tStrg+U");
            AppendMenuW(formatting,MF_SEPARATOR,0,nullptr);
            AppendMenuW(formatting,MF_STRING,ID_LARGER,L"Schrift größer\tStrg+Plus"); AppendMenuW(formatting,MF_STRING,ID_SMALLER,L"Schrift kleiner\tStrg+Minus"); AppendMenuW(formatting,MF_STRING,ID_BULLET,L"Aufzählung\tStrg+Umschalt+L");
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(formatting),L"Textformat");
            AppendMenuW(appearance,MF_POPUP,reinterpret_cast<UINT_PTR>(palette),L"Farbe");
            AppendMenuW(appearance,MF_STRING|(n->topmost?MF_CHECKED:0),ID_TOP,L"Im Vordergrund halten");
            AppendMenuW(appearance,MF_STRING|(n->transparent?MF_CHECKED:0),ID_TRANSPARENT,L"Transparent");
            AppendMenuW(appearance,MF_SEPARATOR,0,nullptr);
            AppendMenuW(appearance,MF_STRING,ID_DEFAULT,L"Als Standard verwenden"); AppendMenuW(appearance,MF_STRING,ID_ARRANGE,L"Notizen anordnen");
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(appearance),L"Darstellung");
            AppendMenuW(files,MF_STRING,ID_IMPORT,L"Text importieren …"); AppendMenuW(files,MF_STRING,ID_EXPORT,L"Text exportieren …");
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(files),L"Textdatei");
            HMENU attachments=CreatePopupMenu(); AppendMenuW(attachments,MF_STRING,ID_ATTACH,L"Bild oder PDF hinzufügen …");
            if(!n->attachments.empty()) {
                HMENU open=CreatePopupMenu(), copy=CreatePopupMenu(), remove=CreatePopupMenu();
                for(size_t i=0;i<n->attachments.size();++i) {
                    std::wstring label; for(wchar_t ch:n->attachments[i].name) { label+=ch; if(ch==L'&') label+=ch; }
                    AppendMenuW(open,MF_STRING,ID_ATTACHMENT_BASE+static_cast<UINT>(i),label.c_str());
                    AppendMenuW(copy,MF_STRING,ID_SAVE_ATTACHMENT_BASE+static_cast<UINT>(i),label.c_str());
                    AppendMenuW(remove,MF_STRING,ID_REMOVE_ATTACHMENT_BASE+static_cast<UINT>(i),label.c_str());
                }
                AppendMenuW(attachments,MF_SEPARATOR,0,nullptr);
                AppendMenuW(attachments,MF_POPUP,reinterpret_cast<UINT_PTR>(open),L"Kopie öffnen");
                AppendMenuW(attachments,MF_POPUP,reinterpret_cast<UINT_PTR>(copy),L"Kopie speichern unter …");
                AppendMenuW(attachments,MF_POPUP,reinterpret_cast<UINT_PTR>(remove),L"Anlage entfernen");
            }
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(attachments),L"Anlagen");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_DELETE, L"Notiz löschen");
            AppendMenuW(menu, MF_STRING, ID_ABOUT, L"Über StickyNotes");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, ID_QUIT, L"StickyNotes beenden");
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ClientToScreen(hwnd, &pt);
            int choice=TrackPopupMenu(menu, TPM_RETURNCMD|TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr); DestroyMenu(menu);
            if (choice>=200 && choice<206) { setColor(n, choice-200); scheduleSave(); }
            else if (choice) SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(choice, BN_CLICKED), 0);
            return 0;
        } break;
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(hwnd, &r); FillRect(reinterpret_cast<HDC>(wp), &r, n->brush); return 1; }
    case UPDATE_SCROLLBAR:
        if (n->edit) {
            RECT r{}; GetClientRect(n->edit,&r); HDC dc=GetDC(n->edit);
            HGDIOBJ old=SelectObject(dc,font); TEXTMETRICW metric{}; GetTextMetricsW(dc,&metric); SelectObject(dc,old); ReleaseDC(n->edit,dc);
            int lines=static_cast<int>(SendMessageW(n->edit,EM_GETLINECOUNT,0,0));
            ShowScrollBar(n->edit,SB_VERT,lines*metric.tmHeight>r.bottom-r.top);
        } return 0;
    case WM_CTLCOLOREDIT: { HDC dc = reinterpret_cast<HDC>(wp); SetBkColor(dc, colors[n->color]); SetTextColor(dc, RGB(45,45,45)); return reinterpret_cast<LRESULT>(n->brush); }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE) { PostMessageW(hwnd,UPDATE_SCROLLBAR,0,0); InvalidateRect(hwnd, nullptr, FALSE); scheduleSave(); return 0; }
        if (HIWORD(wp) != BN_CLICKED && !(HIWORD(wp)==1 && lp==0)) break;
        switch (LOWORD(wp)) {
        case ID_NEW: { RECT r{}; GetWindowRect(hwnd, &r); size_t before=notes.size(); createNote(r.left+30, r.top+30, defaultWidth, defaultHeight, defaultColor, L""); if(notes.size()>before) { notes.back()->topmost=defaultTop; notes.back()->transparent=defaultTransparent; applyAppearance(notes.back().get()); } scheduleSave(); return 0; }
        case ID_TOP: n->topmost=!n->topmost; applyAppearance(n); return 0;
        case ID_TRANSPARENT: n->transparent=!n->transparent; applyAppearance(n); return 0;
        case ID_BOLD: case ID_ITALIC: case ID_UNDERLINE: case ID_LARGER: case ID_SMALLER: case ID_BULLET: formatSelection(n,LOWORD(wp)); return 0;
        case ID_FIND: searchNotes(n); return 0;
        case ID_EXPORT: exportText(n,hwnd); return 0;
        case ID_IMPORT: importText(n); return 0;
        case ID_ABOUT: showAbout(n); return 0;
        case ID_ATTACH: addAttachment(n); return 0;
        case ID_DEFAULT: { RECT r{}; GetWindowRect(hwnd,&r); defaultColor=n->color; defaultWidth=r.right-r.left; defaultHeight=n->collapsed?n->expandedHeight:r.bottom-r.top; defaultTop=n->topmost; defaultTransparent=n->transparent; CHARFORMATW f{}; f.cbSize=sizeof(f); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&f)); if(f.dwMask&CFM_SIZE) defaultSize=std::clamp<LONG>(f.yHeight,120,1440); defaultEffects=f.dwEffects&(CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE); scheduleSave(); return 0; }
        case ID_ARRANGE: { RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0); int x=area.left+15,y=area.top+15,rowHeight=0; for(auto& item:notes) { RECT r{}; GetWindowRect(item->window,&r); int w=r.right-r.left,h=r.bottom-r.top; if(x+w>area.right && x>area.left+15) { x=area.left+15; y+=rowHeight+12; rowHeight=0; } if(y+h>area.bottom) y=area.top+15; SetWindowPos(item->window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); x+=w+12; rowHeight=std::max(rowHeight,h); } scheduleSave(); return 0; }
        case ID_COLOR: { HMENU menu = CreatePopupMenu(); for (int i=0; i<6; ++i) AppendMenuW(menu, MF_STRING | (i==n->color ? MF_CHECKED : 0), 200+i, colorNames[i]); RECT r{}; GetWindowRect(GetDlgItem(hwnd, ID_COLOR), &r); int choice = TrackPopupMenu(menu, TPM_RETURNCMD|TPM_NONOTIFY, r.left, r.bottom, 0, hwnd, nullptr); DestroyMenu(menu); if (choice>=200 && choice<206) { setColor(n, choice-200); scheduleSave(); } return 0; }
        case ID_DELETE:
            if (confirmDelete(n)) {
                if(!removeNote(n)) warn(L"Die Änderung konnte nicht gespeichert werden. Die Notiz bleibt geöffnet und erhalten.");
            } return 0;
        case ID_QUIT: quit(); return 0;
        default:
            if(LOWORD(wp)>=ID_ATTACHMENT_BASE && LOWORD(wp)<ID_ATTACHMENT_BASE+10) { openAttachment(n,LOWORD(wp)-ID_ATTACHMENT_BASE); return 0; }
            if(LOWORD(wp)>=ID_SAVE_ATTACHMENT_BASE && LOWORD(wp)<ID_SAVE_ATTACHMENT_BASE+10) { saveAttachmentAs(n,LOWORD(wp)-ID_SAVE_ATTACHMENT_BASE); return 0; }
            if(LOWORD(wp)>=ID_REMOVE_ATTACHMENT_BASE && LOWORD(wp)<ID_REMOVE_ATTACHMENT_BASE+10) { size_t index=LOWORD(wp)-ID_REMOVE_ATTACHMENT_BASE; if(index<n->attachments.size() && MessageBoxW(hwnd,L"Diese Anlage aus der Notiz entfernen? Die ursprüngliche Datei bleibt erhalten.",L"Anlage entfernen",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES) { if(!removeAttachment(n,index)) warn(L"Die Änderung konnte nicht gespeichert werden. Die Anlage bleibt in der Notiz erhalten."); } return 0; }
        } break;
    case WM_TIMER: if (wp == SAVE_TIMER) { KillTimer(hwnd, SAVE_TIMER); saveOrWarn(); return 0; } break;
    case WM_CLOSE: quit(); return 0;
    case WM_QUERYENDSESSION: if (canSave && !save()) return FALSE; return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void createNote(int x, int y, int width, int height, int color, const std::wstring& text, bool collapsed) {
    if(notes.size()>=1000) { warn(L"Es können höchstens 1000 Notizen gleichzeitig geöffnet sein."); return; }
    auto n = std::make_unique<Note>(); n->color = color; n->collapsed = collapsed; n->expandedHeight = height;
    if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) { RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0); x=work.left+40; y=work.top+40; }
    RECT proposed{x,y,x+width,y+height};
    if (!MonitorFromRect(&proposed, MONITOR_DEFAULTTONULL)) { RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0); x=work.left+40; y=work.top+40; }
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"StickyNotesWindow", L"StickyNotes", WS_POPUP, x,y,width,collapsed ? COLLAPSED_HEIGHT : height, nullptr,nullptr,instance,n.get());
    if (!hwnd) { warn(L"Das Notizfenster konnte nicht erstellt werden."); return; }
    HMODULE dwm=LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        auto setAttribute=reinterpret_cast<HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD)>(GetProcAddress(dwm,"DwmSetWindowAttribute"));
        if (setAttribute) { DWORD noBorder=0xfffffffe, noRound=1; setAttribute(hwnd,34,&noBorder,sizeof(noBorder)); setAttribute(hwnd,33,&noRound,sizeof(noRound)); }
        FreeLibrary(dwm);
    }
    SetWindowTextW(n->edit, text.c_str()); notes.push_back(std::move(n)); ShowWindow(hwnd, SW_SHOWNORMAL); UpdateWindow(hwnd);
}
struct SavedNote { int32_t x,y,w,h,color; uint32_t collapsed = 0,flags=0; std::wstring text; std::string rtf; std::vector<Attachment> attachments; };
bool load() {
    std::error_code ec;
    if (!std::filesystem::exists(dataPath, ec)) return !ec;
    std::ifstream in(dataPath, std::ios::binary); uint32_t magic=0,count=0; size_t totalAttachmentBytes=0;
    if (!readValue(in, magic) || (magic != 0x31544e53 && magic != 0x32544e53 && magic != 0x33544e53 && magic != 0x34544e53) || !readValue(in,count) || count > 1000) return false;
    if (magic>=0x33544e53) { int32_t c,w,h; uint32_t f; if(!readValue(in,c)||!readValue(in,w)||!readValue(in,h)||!readValue(in,f)||c<0||c>=6||w<230||w>10000||h<150||h>10000||f>3) return false; defaultColor=c; defaultWidth=w; defaultHeight=h; defaultTop=f&1; defaultTransparent=f&2; }
    if (magic==0x34544e53) { int32_t size; uint32_t effects; if(!readValue(in,size)||!readValue(in,effects)||size<120||size>1440||(effects&~static_cast<uint32_t>(CFE_BOLD|CFE_ITALIC|CFE_UNDERLINE))) return false; defaultSize=size; defaultEffects=effects; }
    std::vector<SavedNote> loaded;
    for (uint32_t i=0; i<count; ++i) {
        SavedNote n{}; uint32_t size=0;
        if (!readValue(in,n.x)||!readValue(in,n.y)||!readValue(in,n.w)||!readValue(in,n.h)||!readValue(in,n.color)||(magic != 0x31544e53 && !readValue(in,n.collapsed))||!readValue(in,size) || n.w<230 || n.h<150 || n.w>10000 || n.h>10000 || n.color<0 || n.color>=6 || n.collapsed>1 || size>1024*1024) return false;
        n.text.resize(size); if (!in.read(reinterpret_cast<char*>(n.text.data()), size*sizeof(wchar_t))) return false;
        if(magic>=0x33544e53) { uint32_t bytes; if(!readValue(in,n.flags)||n.flags>3||!readValue(in,bytes)||bytes>16*1024*1024) return false; n.rtf.resize(bytes); if(!in.read(n.rtf.data(),bytes) || n.rtf.rfind("{\\rtf",0)!=0) return false; }
        if(magic==0x34544e53) { uint32_t count; if(!readValue(in,count)||count>10) return false; for(uint32_t j=0;j<count;++j) { Attachment a; uint32_t length,bytes; if(!readValue(in,length)||length==0||length>255) return false; a.name.resize(length); if(!in.read(reinterpret_cast<char*>(a.name.data()),length*sizeof(wchar_t)) || std::filesystem::path(a.name).filename()!=std::filesystem::path(a.name) || !supportedAttachment(a.name) || !readValue(in,bytes)||bytes>10*1024*1024) return false; totalAttachmentBytes+=bytes; if(totalAttachmentBytes>100u*1024*1024) return false; a.data.resize(bytes); if(!in.read(a.data.data(),bytes)) return false; n.attachments.push_back(std::move(a)); } }
        loaded.push_back(std::move(n));
    }
    if (in.peek() != std::char_traits<char>::eof()) return false;
    for (auto& n : loaded) { size_t before=notes.size(); createNote(n.x,n.y,n.w,n.h,n.color,n.text,n.collapsed != 0); if(notes.size()==before) return false; Note* created=notes.back().get(); if(magic>=0x33544e53 && !restoreFormattedText(created->edit,n.rtf,n.text)) return false; created->topmost=n.flags&1; created->transparent=n.flags&2; created->attachments=std::move(n.attachments); applyAppearance(created); }
    return notes.size() == loaded.size();
}
}
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\StickyNotesSingleInstance");
    if (!mutex) { warn(L"Die Anwendung konnte nicht gestartet werden."); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); warn(L"StickyNotes ist bereits geöffnet."); return 0; }
    instance = h;
    if (!LoadLibraryW(L"Msftedit.dll")) { warn(L"Das Windows-Textsteuerelement konnte nicht geladen werden."); return 1; }
    SetProcessDPIAware();
    wchar_t folder[32768]{};
    DWORD length=GetEnvironmentVariableW(L"STICKYNOTES_PORTABLE_DIR",folder,32768);
    if (!length || length>=32768) {
        length=GetModuleFileNameW(nullptr,folder,32768);
        if (!length || length>=32768) { warn(L"Der Programmordner konnte nicht gefunden werden."); return 1; }
        dataPath=std::filesystem::path(folder).parent_path()/L"notes.dat";
    } else dataPath=std::filesystem::path(folder)/L"notes.dat";
    std::error_code existsError;
    bool portableExists=std::filesystem::exists(dataPath,existsError);
    if (existsError) { warn(L"Auf den Programmordner kann nicht zugegriffen werden."); return 1; }
    if (!portableExists) {
        PWSTR local=nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) {
            auto previous=std::filesystem::path(local)/L"StickyNotes"/L"notes.dat"; CoTaskMemFree(local);
            std::error_code ec; if (std::filesystem::exists(previous,ec) && !std::filesystem::copy_file(previous,dataPath,std::filesystem::copy_options::none,ec)) {
                warn(L"Vorhandene Notizen konnten nicht in den Programmordner übernommen werden. Bitte prüfe die Schreibrechte. Die bisherigen Daten bleiben erhalten."); return 1;
            }
            if(ec) { warn(L"Der bisherige Notizordner konnte nicht geprüft werden. Die bisherigen Daten bleiben erhalten."); return 1; }
        }
    }
    font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    WNDCLASSW wc{}; wc.style=CS_DBLCLKS; wc.lpfnWndProc=procedure; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesWindow"; wc.hCursor=LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;
    if (!load()) {
        warn(L"Die gespeicherten Notizen konnten nicht gelesen werden. Die Datei wird nicht überschrieben. Bitte sichere oder repariere notes.dat und starte die Anwendung neu.");
        for(auto& n:notes) { DestroyWindow(n->window); if(n->brush) DeleteObject(n->brush); }
        notes.clear(); DeleteObject(font); CloseHandle(mutex); return 1;
    }
    if (notes.empty()) { createNote(CW_USEDEFAULT,CW_USEDEFAULT,defaultWidth,defaultHeight,defaultColor,L""); if(!notes.empty()) { notes.back()->topmost=defaultTop; notes.back()->transparent=defaultTransparent; applyAppearance(notes.back().get()); } }
    loading=false;
    if (notes.empty()) return 1;
    HACCEL shortcuts=CreateAcceleratorTableW(const_cast<ACCEL*>(noteShortcuts),static_cast<int>(sizeof(noteShortcuts)/sizeof(noteShortcuts[0])));
    MSG msg{}; while (GetMessageW(&msg,nullptr,0,0)>0) {
        HWND root=GetAncestor(msg.hwnd,GA_ROOT);
        bool isNote=std::any_of(notes.begin(),notes.end(),[root](const auto& n){ return n->window==root; });
        if (isNote && shortcuts && TranslateAcceleratorW(root,shortcuts,&msg)) continue;
        if (!IsDialogMessageW(GetActiveWindow(), &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if(shortcuts) DestroyAcceleratorTable(shortcuts);
    for (auto& n : notes) { DestroyWindow(n->window); if (n->brush) DeleteObject(n->brush); }
    DeleteObject(font); CloseHandle(mutex); return 0;
}


