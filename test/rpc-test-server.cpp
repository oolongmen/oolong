#include "rpc/rpc_server.h"

int Test(const nlohmann::json &, nlohmann::json &res)
{
    res["data"] = "test ok";
    return 0;
}

int main()
{
    auto &s = oolong::JSONRPCServer::Instance();

    s.BindTCP(8899);

    s.AddMethod("test", Test);
    s.StartListen();
}
