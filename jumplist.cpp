#include "jumplist.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propidl.h>
#include <cwchar>

namespace {
#ifdef STICKYNOTES_ISOLATED_QA
constexpr wchar_t appId[] = L"CyBeerBoy.StickyNotes.IsolatedQA";
#else
constexpr wchar_t appId[] = L"CyBeerBoy.StickyNotes.Desktop";
#endif
struct Task { const wchar_t* arguments; const wchar_t* title; TaskAction action; };
constexpr Task tasks[] = {
    {L"--task=new", L"Neue Notiz", TaskAction::NewNote},
    {L"--task=find", L"Notizen durchsuchen", TaskAction::Find},
    {L"--task=arrange", L"Notizen anordnen", TaskAction::Arrange},
    {L"--task=quit", L"StickyNotes beenden", TaskAction::Quit}
};
}
TaskAction parseTask(const wchar_t* arguments) {
    if (!arguments || !*arguments) return TaskAction::None;
    for (const auto& task : tasks) if (std::wcscmp(arguments, task.arguments)==0) return task.action;
    return TaskAction::Invalid;
}
bool setupTaskbarTasks() {
    if (FAILED(SetCurrentProcessExplicitAppUserModelID(appId))) return false;
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return false;
    ICustomDestinationList* list=nullptr; IObjectCollection* collection=nullptr; IObjectArray* removed=nullptr; IObjectArray* array=nullptr;
    wchar_t path[32768]{}; DWORD length=GetModuleFileNameW(nullptr,path,32768);
    HRESULT hr=length && length<32768 ? CoCreateInstance(CLSID_DestinationList,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&list)) : E_FAIL;
    if (SUCCEEDED(hr)) hr=list->SetAppID(appId);
    UINT slots=0; bool begun=false;
    if (SUCCEEDED(hr)) { hr=list->BeginList(&slots,IID_PPV_ARGS(&removed)); begun=SUCCEEDED(hr); }
    if (SUCCEEDED(hr)) hr=CoCreateInstance(CLSID_EnumerableObjectCollection,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&collection));
    for (const auto& task : tasks) {
        if (FAILED(hr)) break;
        IShellLinkW* link=nullptr; IPropertyStore* properties=nullptr;
        hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
        if (SUCCEEDED(hr)) hr=link->SetPath(path);
        if (SUCCEEDED(hr)) hr=link->SetArguments(task.arguments);
        if (SUCCEEDED(hr)) hr=link->SetIconLocation(path,0);
        if (SUCCEEDED(hr)) hr=link->QueryInterface(IID_PPV_ARGS(&properties));
        PROPVARIANT title{}; title.vt=VT_LPWSTR; title.pwszVal=const_cast<wchar_t*>(task.title);
        if (SUCCEEDED(hr)) hr=properties->SetValue(PKEY_Title,title);
        if (SUCCEEDED(hr)) hr=properties->Commit();
        if (SUCCEEDED(hr)) hr=collection->AddObject(link);
        if (properties) properties->Release(); if (link) link->Release();
    }
    if (SUCCEEDED(hr)) hr=collection->QueryInterface(IID_PPV_ARGS(&array));
    if (SUCCEEDED(hr)) hr=list->AddUserTasks(array);
    if (SUCCEEDED(hr)) hr=list->CommitList();
    else if (begun) list->AbortList();
    if (array) array->Release(); if (collection) collection->Release(); if (removed) removed->Release(); if (list) list->Release();
    CoUninitialize();
    return SUCCEEDED(hr);
}
