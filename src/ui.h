#pragma once
// Встроенное окно приложения: HTML-интерфейс в WebView2 + иконка в трее.
#include <string>

namespace ui {

// Есть ли встроенное окно на этой платформе.
bool available();

// Создаёт окно с веб-интерфейсом и иконку в трее, работает до выхода.
// hidden — стартовать свёрнутым в трей; keepConsole — не прятать консоль.
// Возвращает 0 при штатном выходе, -1 если окно создать не удалось.
int run(bool hidden, bool keepConsole);

}  // namespace ui
