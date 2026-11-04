#define wWinMain stickynotes_entry
#include "../main.cpp"
#undef wWinMain
#include <cstdio>

namespace {
struct TemporaryFile {
    wchar_t path[MAX_PATH]{};
    std::filesystem::path stale, exported;
    bool ownsStale=false, ownsExport=false;
    TemporaryFile() {
        wchar_t dir[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH,dir) || !GetTempFileNameW(dir,L"snt",0,path)) path[0]=0;
    }
    ~TemporaryFile() { if(ownsStale) DeleteFileW(stale.c_str()); if(ownsExport) DeleteFileW(exported.c_str()); if(*path) DeleteFileW(path); }
};
void closeNotes() {
    for(auto& note:notes) { DestroyWindow(note->window); if(note->brush) DeleteObject(note->brush); }
    notes.clear();
}
bool textEquals(Note* n,const wchar_t* expected) {
    int len=GetWindowTextLengthW(n->edit); std::wstring text(static_cast<size_t>(len)+1,L'\0');
    int copied=GetWindowTextW(n->edit,text.data(),len+1); text.resize(copied); return text==expected;
}
std::wstring noteText(Note* n) {
    int len=GetWindowTextLengthW(n->edit); std::wstring text(static_cast<size_t>(len)+1,L'\0');
    int copied=GetWindowTextW(n->edit,text.data(),len+1); text.resize(static_cast<size_t>(std::max(0,copied))); return text;
}
}
int main() {
    TemporaryFile testFile;
    if(!*testFile.path) return 1;
    instance=GetModuleHandleW(nullptr); if(!LoadLibraryW(L"Msftedit.dll")) return 2;
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    WNDCLASSW wc{}; wc.style=CS_DBLCLKS; wc.lpfnWndProc=procedure; wc.hInstance=instance; wc.lpszClassName=L"StickyNotesWindow"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!font || !RegisterClassW(&wc)) return 3;
    dataPath=testFile.path; loading=true;
    auto futureDatabase=dataPath; futureDatabase+=L".future";
    auto currentDatabase=dataPath; dataPath=futureDatabase;
    if(!isDataFilePath(futureDatabase)) return 34;
    dataPath=currentDatabase;
    {
        std::ofstream out(dataPath,std::ios::binary|std::ios::trunc);
        uint32_t magic=0x32544e53,count=1,collapsed=0,size=9; int32_t x=50,y=50,w=300,h=260,c=2;
        writeValue(out,magic);writeValue(out,count);writeValue(out,x);writeValue(out,y);writeValue(out,w);writeValue(out,h);writeValue(out,c);writeValue(out,collapsed);writeValue(out,size);
        out.write(reinterpret_cast<const char*>(L"Altä Text"),size*sizeof(wchar_t)); if(!out) return 4;
    }
    if(!load() || notes.size()!=1 || !textEquals(notes[0].get(),L"Altä Text")) return 5;
    Note* n=notes[0].get();
    if(!isDataFilePath(dataPath) || !isDataFilePath(dataPath.parent_path()/L"."/dataPath.filename())) return 24;
    testFile.stale=dataPath; testFile.stale+=L".tmp";
    HANDLE stale=CreateFileW(testFile.stale.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(stale==INVALID_HANDLE_VALUE) return 25;
    testFile.ownsStale=true; const char recovery[]="nicht ueberschreiben"; DWORD written=0;
    bool staleOk=WriteFile(stale,recovery,sizeof(recovery),&written,nullptr) && written==sizeof(recovery);
    staleOk=CloseHandle(stale) && staleOk; if(!staleOk) return 26;
    testFile.exported=dataPath; testFile.exported+=L".txt";
    const char exportData[]="\xef\xbb\xbfTest";
    if(writeNewFile(testFile.exported,exportData,sizeof(exportData)-1)!=NewFileResult::ok) return 27;
    testFile.ownsExport=true;
    if(writeNewFile(testFile.exported,"anderen Inhalt",14)!=NewFileResult::alreadyExists) return 28;
    { std::ifstream in(testFile.exported,std::ios::binary); std::string data((std::istreambuf_iterator<char>(in)),{}); if(data!=std::string(exportData,sizeof(exportData)-1)) return 29; }
    if(appendImportedText(n,std::wstring(MAX_NOTE_TEXT,L'x')) || !textEquals(n,L"Altä Text")) return 6;
    if(appendImportedText(n,std::wstring(L"A\0B",3)) || !textEquals(n,L"Altä Text")) return 7;
    if(!appendImportedText(n,L" ergänzt") || !textEquals(n,L"Altä Text ergänzt")) return 8;
    SendMessageW(n->edit,EM_SETSEL,0,4); SendMessageW(n->window,WM_COMMAND,MAKEWPARAM(ID_BOLD,1),0);
    CHARFORMATW selected{}; selected.cbSize=sizeof(selected); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&selected));
    if(!(selected.dwEffects&CFE_BOLD)) return 19;
    LONG originalSize=selected.yHeight;
    SendMessageW(n->window,WM_COMMAND,MAKEWPARAM(ID_LARGER,1),0);
    selected={}; selected.cbSize=sizeof(selected); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&selected));
    if(selected.yHeight!=originalSize+40) return 22;
    SendMessageW(n->window,WM_COMMAND,MAKEWPARAM(ID_SMALLER,1),0);
    selected={}; selected.cbSize=sizeof(selected); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&selected));
    if(selected.yHeight!=originalSize) return 23;
    HACCEL keys=CreateAcceleratorTableW(const_cast<ACCEL*>(noteShortcuts),static_cast<int>(sizeof(noteShortcuts)/sizeof(noteShortcuts[0])));
    if(!keys) return 20;
    ACCEL entries[32]{}; int count=CopyAcceleratorTableW(keys,entries,32); DestroyAcceleratorTable(keys);
    bool bold=false,italic=false,underline=false,bullet=false,larger=false,smaller=false,newNote=false,find=false;
    for(int i=0;i<count;++i) {
        if(entries[i].cmd==ID_BOLD && entries[i].key=='B' && (entries[i].fVirt&FCONTROL)) bold=true;
        if(entries[i].cmd==ID_ITALIC && entries[i].key=='I' && (entries[i].fVirt&FCONTROL)) italic=true;
        if(entries[i].cmd==ID_UNDERLINE && entries[i].key=='U' && (entries[i].fVirt&FCONTROL)) underline=true;
        if(entries[i].cmd==ID_BULLET && entries[i].key=='L' && (entries[i].fVirt&(FCONTROL|FSHIFT))==(FCONTROL|FSHIFT)) bullet=true;
        if(entries[i].cmd==ID_LARGER && entries[i].key==VK_OEM_PLUS && (entries[i].fVirt&FCONTROL)) larger=true;
        if(entries[i].cmd==ID_SMALLER && entries[i].key==VK_OEM_MINUS && (entries[i].fVirt&FCONTROL)) smaller=true;
        if(entries[i].cmd==ID_NEW && entries[i].key=='N' && (entries[i].fVirt&FCONTROL)) newNote=true;
        if(entries[i].cmd==ID_FIND && entries[i].key=='F' && (entries[i].fVirt&FCONTROL)) find=true;
    }
    if(!bold||!italic||!underline||!bullet||!larger||!smaller||!newNote||!find) return 21;
    n->topmost=true; n->transparent=true; n->attachments.push_back({L"bild.png",{'t','e','s','t'}}); toggleCollapse(n);
    defaultColor=3; defaultWidth=330; defaultHeight=270; defaultTop=true; defaultTransparent=true; defaultSize=280; defaultEffects=CFE_BOLD;
    if(!save()) return 9;
    { std::ifstream in(testFile.stale,std::ios::binary); std::string data((std::istreambuf_iterator<char>(in)),{}); if(data!=std::string(recovery,sizeof(recovery))) return 30; }
    closeNotes();
    if(!load() || notes.size()!=1) return 10;
    n=notes[0].get();
    if(!textEquals(n,L"Altä Text ergänzt") || !n->collapsed || !n->topmost || !n->transparent || n->color!=2 || n->attachments.size()!=1 || n->attachments[0].name!=L"bild.png" || n->attachments[0].data!=std::vector<char>({'t','e','s','t'})) return 11;
    if(defaultColor!=3 || defaultWidth!=330 || defaultHeight!=270 || !defaultTop || !defaultTransparent || defaultSize!=280 || defaultEffects!=CFE_BOLD) return 12;
    SendMessageW(n->edit,EM_SETSEL,0,4); CHARFORMATW f{}; f.cbSize=sizeof(f); SendMessageW(n->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&f)); if(!(f.dwEffects&CFE_BOLD)) return 13;
    toggleCollapse(n); RECT r{}; GetWindowRect(n->window,&r); if(r.bottom-r.top!=260) return 14;
    createNote(80,80,300,260,0,L"Nicht verlieren"); if(notes.size()!=2) return 15;
    Note* candidate=notes.back().get(); auto originalPath=dataPath;
    dataPath=originalPath/L"nicht-erstellbar.dat";
    if(removeNote(candidate) || notes.size()!=2 || !IsWindow(candidate->window) || !textEquals(candidate,L"Nicht verlieren")) return 16;
    dataPath=originalPath;
    if(!removeNote(candidate) || notes.size()!=1 || !textEquals(notes[0].get(),L"Altä Text ergänzt")) return 17;
    closeNotes();
    if(!load() || notes.size()!=1 || !textEquals(notes[0].get(),L"Altä Text ergänzt")) return 18;
    createNote(100,100,300,260,0,L"Erste Zeile\r\nZweite Zeile – äöü 😀 📝 🚀 ❤️ 👩‍💻");
    if(notes.size()!=2) return 31;
    std::wstring multiline=noteText(notes[1].get());
    std::string originalRtf=getRtf(notes[1]->edit);
    if(multiline.empty() || !save()) return 32;
    closeNotes();
    if(!load()) { std::puts("Mehrzeilige Notiz konnte nicht geladen werden."); return 33; }
    if(notes.size()!=2) { std::printf("Unerwartete Notizanzahl: %zu\n",notes.size()); return 33; }
    std::wstring restored=noteText(notes[1].get());
    if(restored!=multiline) {
        std::printf("Unicode-Roundtrip unterscheidet sich: vorher %zu, nachher %zu UTF-16-Einheiten\n",multiline.size(),restored.size());
        std::printf("Vorher:"); for(wchar_t c:multiline) std::printf(" %04x",static_cast<unsigned>(c));
        std::printf("\nNachher:"); for(wchar_t c:restored) std::printf(" %04x",static_cast<unsigned>(c)); std::printf("\n");
        std::printf("RTF: %s\n",originalRtf.c_str());
        return 33;
    }
    defaultEffects=0;
    createNote(120,120,300,260,0,L"Erste\r\nZweite");
    if(notes.size()!=3) return 39;
    Note* correction=notes.back().get(); CHARRANGE selectedWord{6,12};
    SendMessageW(correction->edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selectedWord)); formatSelection(correction,ID_BOLD);
    std::string correctionRtf=getRtf(correction->edit);
    if(!restoreFormattedText(correction->edit,correctionRtf,L"Erste\r\nZwei te") || editorText(correction->edit)!=normalizedEditorText(L"Erste\r\nZwei te")) return 40;
    CHARRANGE endOfWord{11,13}; SendMessageW(correction->edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&endOfWord));
    CHARFORMATW afterCorrection{}; afterCorrection.cbSize=sizeof(afterCorrection); SendMessageW(correction->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&afterCorrection));
    if(!(afterCorrection.dwEffects&CFE_BOLD)) {
        std::printf("Format nach Reparatur: Maske=%lx Effekte=%lx\n",static_cast<unsigned long>(afterCorrection.dwMask),static_cast<unsigned long>(afterCorrection.dwEffects));
        for(LONG pos=0;pos<14;++pos) { CHARRANGE one{pos,pos+1}; SendMessageW(correction->edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&one)); CHARFORMATW each{}; each.cbSize=sizeof(each); SendMessageW(correction->edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&each)); std::printf("%ld:%lx ",pos,static_cast<unsigned long>(each.dwEffects)); }
        std::printf("\n"); return 41;
    }
    if(restoreFormattedText(correction->edit,correctionRtf,L"Erste\r\nAnders")) return 42;
    closeNotes();
    TemporaryFile corrupted; if(!*corrupted.path) return 35;
    { std::ofstream out(corrupted.path,std::ios::binary|std::ios::trunc); out<<"defekt"; if(!out) return 36; }
    dataPath=corrupted.path; if(load() || !notes.empty()) return 37;
    { std::ifstream in(corrupted.path,std::ios::binary); std::string data((std::istreambuf_iterator<char>(in)),{}); if(data!="defekt") return 38; }
    DeleteObject(font);
    std::puts("OK: Altformat, Importgrenzen, Formatierung, Anlagen, Wiederherstellung und Loeschen bei Speicherfehler");
    return 0;
}
