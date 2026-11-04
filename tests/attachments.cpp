#define wWinMain stickynotes_entry
#include "../main.cpp"
#undef wWinMain
#include <cstdio>

namespace {
struct Fixture {
    std::filesystem::path dir, source, first, second, copy;
    Fixture() {
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        if(!GetTempPathW(MAX_PATH,temp) || !GetTempFileNameW(temp,L"sna",0,unique)) return;
        if(!DeleteFileW(unique) || !CreateDirectoryW(unique,nullptr)) return;
        dir=unique; source=dir/L"Original & ü.pdf"; first=dir/L"first.dat"; second=dir/L"portable-copy.dat"; copy=dir/L"export.pdf";
    }
    ~Fixture() {
        if(dir.empty()) return;
        DeleteFileW(source.c_str()); DeleteFileW(first.c_str()); DeleteFileW(second.c_str()); DeleteFileW(copy.c_str());
        RemoveDirectoryW(dir.c_str());
    }
};
std::vector<char> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void closeNotes() { for(auto& n:notes) { DestroyWindow(n->window); if(n->brush) DeleteObject(n->brush); } notes.clear(); }
}
int main() {
    Fixture fixture; if(fixture.dir.empty()) return 1;
    const std::vector<char> original{'%','P','D','F','-','t','e','s','t','\0','X'};
    { std::ofstream out(fixture.source,std::ios::binary); out.write(original.data(),original.size()); if(!out) return 2; }
    instance=GetModuleHandleW(nullptr); if(!LoadLibraryW(L"Msftedit.dll")) return 3;
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{}; wc.style=CS_DBLCLKS; wc.lpfnWndProc=procedure; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesWindow"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!font || !RegisterClassW(&wc)) return 4;
    loading=true; dataPath=fixture.source/L"cannot-create.dat"; createNote(50,50,300,260,0,L"Test"); if(notes.size()!=1) return 5;
    Note* n=notes[0].get();
    if(attachFile(n,fixture.source)!=AttachmentResult::saveFailed || !n->attachments.empty() || readBytes(fixture.source)!=original) return 6;
    dataPath=fixture.first;
    if(attachFile(n,fixture.source)!=AttachmentResult::ok || n->attachments.size()!=1 || readBytes(fixture.source)!=original) return 7;
    if(!CopyFileW(fixture.first.c_str(),fixture.second.c_str(),TRUE)) return 8;
    closeNotes(); dataPath=fixture.second;
    if(!load() || notes.size()!=1 || notes[0]->attachments.size()!=1 || notes[0]->attachments[0].data!=original) return 9;
    n=notes[0].get();
    if(writeAttachmentCopy(n->attachments[0],fixture.source)!=AttachmentResult::alreadyExists || readBytes(fixture.source)!=original) return 10;
    if(writeAttachmentCopy(n->attachments[0],fixture.copy)!=AttachmentResult::ok || readBytes(fixture.copy)!=original) return 11;
    if(writeAttachmentCopy(n->attachments[0],fixture.copy)!=AttachmentResult::alreadyExists || readBytes(fixture.copy)!=original) return 12;
    std::filesystem::path temporaryCopy;
    if(!createTemporaryAttachmentCopy(n->attachments[0],temporaryCopy) || temporaryCopy==fixture.source || temporaryCopy.extension()!=L".pdf" || readBytes(temporaryCopy)!=original || readBytes(fixture.source)!=original) return 18;
    if(!DeleteFileW(temporaryCopy.c_str())) return 19;
    dataPath=fixture.source/L"cannot-create.dat";
    if(removeAttachment(n,0) || n->attachments.size()!=1 || n->attachments[0].data!=original || readBytes(fixture.source)!=original) return 13;
    dataPath=fixture.second;
    if(!removeAttachment(n,0) || !n->attachments.empty() || readBytes(fixture.source)!=original) return 14;
    closeNotes(); if(!load() || notes.size()!=1 || !notes[0]->attachments.empty()) return 15;
    closeNotes(); dataPath=fixture.first; if(!load() || notes.size()!=1 || notes[0]->attachments.size()!=1 || notes[0]->attachments[0].data!=original) return 16;
    if(readBytes(fixture.source)!=original || readBytes(fixture.copy)!=original) return 17;
    closeNotes(); DeleteObject(font);
    std::puts("OK: Original bleibt erhalten, Anlage portabel, Kopie ohne Ueberschreiben, Rollback bei Speicherfehler");
    return 0;
}
