#pragma once
#include <windows.h>

enum class TaskAction { None, NewNote, Find, Arrange, Quit, Invalid };
TaskAction parseTask(const wchar_t* arguments);
bool setupTaskbarTasks();
