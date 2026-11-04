#pragma once
// Обнаружение узлов в локальной сети: UDP broadcast + multicast.
#include <cstdint>
#include <functional>
#include <string>

namespace discovery {

using Callback = std::function<void(const std::string& id, const std::string& name,
                                    const std::string& ip, int httpPort, int ftpPort)>;

bool start(uint16_t port, Callback cb, std::string& err);
void stop();
void announce();

}  // namespace discovery
