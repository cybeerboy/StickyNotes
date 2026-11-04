#define wWinMain stickynotes_launcher_entry
#include "../launcher.cpp"
#undef wWinMain
#include <cwchar>
#include <cstdio>

int main() {
    wchar_t root[]=L"E:\\StickyNotes.exe";
    wchar_t folder[]=L"E:\\Notizen\\StickyNotes.exe";
    wchar_t network[]=L"\\\\server\\freigabe\\StickyNotes.exe";
    wchar_t invalid[]=L"StickyNotes.exe";
    if(!programFolder(root) || std::wcscmp(root,L"E:\\")!=0) return 1;
    if(!programFolder(folder) || std::wcscmp(folder,L"E:\\Notizen")!=0) return 2;
    if(!programFolder(network) || std::wcscmp(network,L"\\\\server\\freigabe")!=0) return 3;
    if(programFolder(invalid)) return 4;
    std::puts("OK: Laufwerkswurzel, Unterordner und Netzwerkpfad");
    return 0;
}
