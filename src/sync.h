#pragma once
// Движок автоматической синхронизации общей папки между узлами.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sync {

// Фоновый цикл: забирает у соседей то, чего не хватает локально.
void start();
void stop();
void wake();

// Кнопка «Синхронизировать»: этот узел сам выкладывает ВСЕ свои папки соседям.
// Узел не командует другими: папку, которая лежит на ПК2, отправляет только
// сам ПК2 — нажатием кнопки на нём.
void push_now();

// Применяет надгробия, присланные соседом. Возвращает число удалённых файлов.
int apply_remote_tombstones(const std::string& share,
                            const std::vector<std::pair<std::string, int64_t>>& items);

}  // namespace sync
