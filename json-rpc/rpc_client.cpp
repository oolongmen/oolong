#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <sys/time.h>

#include "rpc_client.h"

#define DLOG(fmt, ...) \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__);

inline nlohmann::json
MakeRequest(int id, const char *method,
            const nlohmann::json &params)
{
    return nlohmann::json(
            {
                { "jsonrpc", "2.0" },
                { "id", id },
                { "method", method },
                { "params", params },
            });
}

inline nlohmann::json
MakeNotification(const char *method,
                 const nlohmann::json &params)
{
    return nlohmann::json(
            {
                { "jsonrpc", "2.0" },
                { "method", method },
                { "params", params },
            });
}

OOLONG_NS_BEGIN

RPCClient::RPCClient()
{
}

RPCClient::~RPCClient()
{
    if (m_socket >= 0)
    {
        close(m_socket);
    }
}

int RPCClient::DataLength()
{
    if (m_buffer.size() < 2)
        return 0;

    return (m_buffer.size() - 2);
}

const char* RPCClient::Data()
{
    if (DataLength() == 0)
        return NULL;

    return &m_buffer[2];
}

int RPCClient::ConnectTCP(const char *host, int port)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host,
                    std::to_string(port).c_str(),
                    &hints,
                    &res) != 0)
    {
        DLOG("getaddrinfo failed: %s", strerror(errno));
        return -1;
    }

    do
    {
        m_socket = socket(res->ai_family,
                          res->ai_socktype,
                          res->ai_protocol);

        if (m_socket < 0)
        {
            DLOG("socket failed");
            break;
        }

        if (connect(m_socket,
                    res->ai_addr,
                    res->ai_addrlen) < 0)
        {
            DLOG("connect failed");
            break;
        }

        freeaddrinfo(res);
        return 0;

    } while (0);

    if (m_socket >= 0)
    {
        close(m_socket);
        m_socket = -1;
    }

    freeaddrinfo(res);
    return -1;
}

int RPCClient::Send(const char *method, nlohmann::json &param)
{
    if (m_socket < 0)
        return -1;

    nlohmann::json req = MakeRequest(1,
                                     method,
                                     param);

    std::string s = req.dump();
    uint16_t datalen = htons(s.size());

    s.insert(0, (char*) &datalen, 2);

    return send(m_socket, s.c_str(), s.size(), 0);
}

int RPCClient::Recv(long timeout)
{
    if (m_socket < 0)
        return -1;

    m_buffer.clear();

    struct timeval tv, *ptv = NULL;
    struct timeval now;

    fd_set rfds;
    long start = 0;

    if (timeout <= 0)
    {
        tv.tv_sec = 0;
        tv.tv_usec = 1000; // 1 millsec
        ptv = &tv;
    }

    gettimeofday(&now, NULL);

    start = (now.tv_sec * 1000) +
            (now.tv_usec / 1000);

    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(m_socket, &rfds);

        int rc = select(m_socket + 1,
                        &rfds,
                        NULL,
                        NULL,
                        ptv);

        if (rc < 0)
        {
            return -1;
        }
        else if (!rc)
        {
            long cur = 0;

            gettimeofday(&now, NULL);

            cur = (now.tv_sec * 1000) +
                  (now.tv_usec / 1000);

            if ((cur - start) < timeout)
                continue;

            // timeout
            errno = ETIME;
            return -1;
        }

        char tmp[1024];
        rc = recv(m_socket, tmp, 1024, 0);

        if (rc < 0)
        {
            // read error
            return -1;
        }

        if (rc == 0)
        {
            // connection close
            errno = ECONNRESET;
            return -1;
        }

        m_buffer.insert(m_buffer.end(),
                        tmp, tmp + rc);

        if (m_buffer.size() < 2)
        {
            // no header
            continue;
        }

        uint16_t datalen = ntohs(*(uint16_t*) m_buffer.data()); 

        if (m_buffer.size() < (datalen + 2))
        {
            // not enuf
            continue;
        }

        // got everything
        break;
    }

    return 0;
}

OOLONG_NS_END
