#pragma once
// Встроенный FTP-сервер (PASV) для доступа к общей папке любым FTP-клиентом.
#include <cstdint>
#include <string>

namespace ftp {

bool start(uint16_t port, std::string& err);
void stop();

}  // namespace ftp
