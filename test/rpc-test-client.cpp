
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <string>

#include "json-rpc/rpc_client.h"

#define DLOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);

int main(int argc, char *argv[])
{
    oolong::RPCClient cc;
    
    if (cc.ConnectTCP(8899) < 0)
    {
        printf("connect failed\n");
        return -1;
    }

    nlohmann::json param = nlohmann::json::object();

    if (argc <= 1)
    {
        printf("%s [method] [param]\n", argv[0]);
        return -1;
    }

    if (argc > 2)
    {
        param = nlohmann::json::parse(argv[2]);
    }

    cc.Send(argv[1], param);

    if (cc.Recv() < 0)
    {
        printf("recv failed\n");
        return -1;
    }

    printf("%s\n", cc.Data());
    return 0;
}
