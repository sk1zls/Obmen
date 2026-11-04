# Сторонние компоненты

Собственный код проекта распространяется по лицензии MIT (см. `LICENSE`).
Ниже — компоненты, права на которые принадлежат не автору проекта.

## Microsoft Edge WebView2 SDK

Каталог `third_party/webview2/` содержит файлы Microsoft Edge WebView2 SDK:

- `include/WebView2.h` — заголовки API, сгенерированные MIDL;
- `include/WebView2EnvironmentOptions.h` — вспомогательный заголовок SDK;
- `x64/WebView2LoaderStatic.lib` — статический лоадер WebView2 (x64).

Права: © Microsoft Corporation. Файлы используются как часть этой программы
(заголовки включаются при сборке, лоадер линкуется в `obmen.exe`) на условиях
лицензии Microsoft Edge WebView2 SDK. Самостоятельным продуктом они не являются,
и лицензия MIT на код проекта на них не распространяется.

Перед публикацией положите официальный файл лицензии SDK рядом с этими файлами —
`third_party/webview2/LICENSE.txt`. Взять его можно из NuGet-пакета
`Microsoft.Web.WebView2` (https://www.nuget.org/packages/Microsoft.Web.WebView2,
раздел «License Info») или из документации WebView2
(https://learn.microsoft.com/microsoft-edge/webview2/).

Если выкладываете готовый `obmen.exe` (например, в Releases), приложите к нему
тот же файл лицензии SDK: лоадер WebView2 вкомпилирован в исполняемый файл.

## Microsoft Edge WebView2 Runtime

Программа **не** поставляет среду выполнения WebView2: используется уже
установленный в Windows компонент «Microsoft Edge WebView2 Runtime» (условия
Microsoft). Без него окно не открывается и интерфейс показывается в браузере —
см. раздел «Ограничения и безопасность» в README.

## Системные библиотеки Windows

`ws2_32`, `shell32`, `ole32`, `oleaut32`, `user32`, `gdi32`, `advapi32`,
`iphlpapi`, `windowsapp` — системные библиотеки Windows, поставляются с ОС
и в репозитории не лежат.

## Прочее

- Иконка `res/obmen.ico` нарисована скриптом `tools/make_icon.ps1`; сторонних
  графических материалов в проекте нет.
- HTTP-сервер, FTP-сервер, JSON, синхронизация и веб-интерфейс — собственный код
  проекта. Внешних библиотек, кроме SDK WebView2 и системных библиотек Windows,
  нет.
