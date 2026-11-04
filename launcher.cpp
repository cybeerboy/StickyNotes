#include <windows.h>
#include <strsafe.h>

namespace {
void error(const wchar_t* text) { MessageBoxW(nullptr,text,L"StickyNotes",MB_OK|MB_ICONERROR); }
bool programFolder(wchar_t* path) {
    wchar_t* slash=wcsrchr(path,L'\\'); if(!slash) return false;
    if(slash==path+2 && path[1]==L':') slash[1]=L'\0'; else *slash=L'\0';
    return true;
}
bool extract(WORD id,const wchar_t* path) {
    HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA); if(!resource) return false;
    DWORD size=SizeofResource(nullptr,resource); HGLOBAL loaded=LoadResource(nullptr,resource); const void* data=loaded?LockResource(loaded):nullptr;
    if(!data || !size) return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0; bool ok=WriteFile(file,data,size,&written,nullptr) && written==size;
    ok=CloseHandle(file) && ok; if(!ok) DeleteFileW(path); return ok;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    wchar_t home[32768]{}; DWORD n=GetModuleFileNameW(nullptr,home,32768);
    if(!n || n>=32768) { error(L"Der Programmordner konnte nicht gefunden werden."); return 1; }
    if(!programFolder(home)) { error(L"Der Programmordner konnte nicht gefunden werden."); return 1; }
    wchar_t tempPath[MAX_PATH]{}, dir[MAX_PATH]{};
    DWORD tempLength=GetTempPathW(MAX_PATH,tempPath);
    if(!tempLength || tempLength>=MAX_PATH || !GetTempFileNameW(tempPath,L"snt",0,dir)) { error(L"Der temporäre Ordner ist nicht verfügbar."); return 1; }
    if(!DeleteFileW(dir) || !CreateDirectoryW(dir,nullptr)) { error(L"Der temporäre Programmordner konnte nicht erstellt werden."); return 1; }
    wchar_t app[MAX_PATH]{}, dll[MAX_PATH]{}, command[MAX_PATH+4]{};
    if(FAILED(StringCchPrintfW(app,MAX_PATH,L"%s\\StickyNotesApp.exe",dir)) || FAILED(StringCchPrintfW(dll,MAX_PATH,L"%s\\libwinpthread-1.dll",dir))) { RemoveDirectoryW(dir); error(L"Der temporäre Pfad ist zu lang."); return 1; }
    bool hasDll=FindResourceW(nullptr,MAKEINTRESOURCEW(2),RT_RCDATA)!=nullptr;
    if(!extract(1,app) || (hasDll && !extract(2,dll))) { DeleteFileW(app); DeleteFileW(dll); RemoveDirectoryW(dir); error(L"StickyNotes konnte nicht gestartet werden."); return 1; }
    if(!SetEnvironmentVariableW(L"STICKYNOTES_PORTABLE_DIR",home)) { DeleteFileW(app); if(hasDll) DeleteFileW(dll); RemoveDirectoryW(dir); error(L"Der Speicherort für die Notizen konnte nicht gesetzt werden."); return 1; }
    StringCchPrintfW(command,MAX_PATH+4,L"\"%s\"",app); STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    bool started=CreateProcessW(app,command,nullptr,nullptr,FALSE,0,nullptr,dir,&startup,&process);
    DWORD exitCode=1;
    if(started) { WaitForSingleObject(process.hProcess,INFINITE); GetExitCodeProcess(process.hProcess,&exitCode); CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    else error(L"StickyNotes konnte nicht gestartet werden.");
    DeleteFileW(app); if(hasDll) DeleteFileW(dll); RemoveDirectoryW(dir);
    return static_cast<int>(exitCode);
}
