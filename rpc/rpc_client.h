#ifndef OOLONG_RPC_CLIENT_H
#define OOLONG_RPC_CLIENT_H

#include "oolong.h"
#include "json.hpp"

OOLONG_NS_BEGIN

class RPCClient
{
public:
    RPCClient();
    virtual ~RPCClient();

    int ConnectTCP(int port);

    int Send(const char *method, nlohmann::json &param);

    int Recv();

    int DataLength();

    const char* Data();

private:
    int m_socket = -1;
    std::vector<char> m_buffer;
};

OOLONG_NS_END

#endif
