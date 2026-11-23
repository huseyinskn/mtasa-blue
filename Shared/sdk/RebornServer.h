#pragma once

#include <string_view>

namespace Reborn
{
    inline constexpr std::string_view ServerHost = "141.98.50.226";
    inline constexpr unsigned short   ServerPort = 22003;

    constexpr bool AllowsServer(std::string_view host, unsigned short port)
    {
#ifdef MTA_DEBUG
        // Debug builds can exercise a private development server without touching production.
        if (host == "127.0.0.1" && port == 32731)
            return true;
#endif
        return host == ServerHost && port == ServerPort;
    }
}
