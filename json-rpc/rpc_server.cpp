#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "rpc_server.h"

#define DLOG(fmt, ...) \
{ \
    fprintf(stderr, "%ld [%s] %s(%d): " fmt "\n", \
            time(NULL), __func__, __FILE__, __LINE__, ##__VA_ARGS__); \
}

OOLONG_NS_BEGIN

class Client
{
public:
    Client(int sock, JSONRPCServer &s);
    virtual ~Client();

    void Close();
    void CloseOnEmpty();

    int Read();
    int Write(const char *data, unsigned int datalen);
    int Flush();

    static void OnRead(int, short, void*);
    static void OnWrite(int, short, void*);

    friend JSONRPCServer;

private:
    int m_id;
    long m_timestamp;

    int m_socket;
    struct event *m_ev[2];
    Buffer m_buffer[2];

    bool m_got_all;
    bool m_close_on_empty;

    void (*m_on_close_cb)(Client*, void *);
    void *m_on_close_param;

    JSONRPCServer &m_server;
};

Client::Client(int sock, JSONRPCServer &srv)
    : m_id(srv.GenUID()),
      m_timestamp(time(NULL)),
      m_socket(sock),
      m_ev { NULL, NULL },
      m_got_all(false),
      m_close_on_empty(false),
      m_on_close_cb(NULL),
      m_on_close_param(NULL),
      m_server(srv)
{
    DLOG();
}

Client::~Client()
{
    DLOG();
    Close();
}

void Client::Close()
{
    DLOG();

    if (m_ev[0])
    {
        event_free(m_ev[0]);
        m_ev[0] = NULL;
    }

    if (m_ev[1])
    {
        event_free(m_ev[1]);
        m_ev[1] = NULL;
    }

    if (m_socket >= 0)
    {
        close(m_socket);
        m_socket = -1;

        if (m_on_close_cb)
        {
            m_on_close_cb(this, m_on_close_param);
        }
    }
}

void Client::CloseOnEmpty()
{
    DLOG();
    m_close_on_empty = true;
    event_add(m_ev[1], NULL);
}

int Client::Read()
{
    DLOG();
    auto &b = m_buffer[0];

    int rc = read(m_socket,
                  b.Tail(),
                  b.Unused());

    if (rc <= 0)
    {
        return rc;
    }

    b.Commit(rc);

    if (b.Used() > 0x10001)
    {
        errno = EOVERFLOW;
        return -1;
    }

    if (!m_got_all)
    {
        if (b.Used() < 2)
        {
            // no header
            return rc;
        }

        uint16_t datalen = ntohs(*(uint16_t*) b.Data());

        if (b.Used() < (datalen + 2))
        {
            // no enuf data
            return rc;
        }

        m_got_all = true;

        if (m_server.Push(m_id,
                          b.Head() + 2,
                          datalen) < 0)
        {
            CloseOnEmpty();
        }
    }

    return rc;
}

int Client::Write(const char *data, unsigned int datalen)
{
    DLOG();
    auto &b = m_buffer[1];

    if (datalen == 0)
        return datalen;

    if (b.Unused() < datalen)
    {
        errno = ENOBUFS;
        return -1;
    }

    memcpy(b.Tail(), data, datalen);
    b.Commit(datalen);

    event_add(m_ev[1], NULL);
    return datalen;
}

int Client::Flush()
{
    DLOG();
    auto &b = m_buffer[1];

    if (b.Empty())
    {
        event_del(m_ev[1]);
        return 0;
    }

    int rc = write(m_socket,
                   b.Head(),
                   b.Used());

    if (rc <= 0)
    {
        return -1;
    }

    b.Remove(rc);

    if (b.Empty())
    {
        event_del(m_ev[1]);
    }

    return rc;
}


void Client::OnRead(int, short, void *userdata)
{
    DLOG();
    auto *c = static_cast<Client*> (userdata);

    if (!c)
        return;

    if (c->Read() <= 0)
    {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN)
        {
            return;
        }

        c->Close();
    }
}

void Client::OnWrite(int, short, void *userdata)
{
    DLOG();
    auto *c = static_cast<Client*> (userdata);

    if (!c)
        return;

    if (c->Flush() < 0)
    {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN)
        {
            return;
        }

        DLOG("flush failed: %s", strerror(errno));
        c->Close();
        return;
    }

    // close on empty
    if (c->m_close_on_empty &&
        c->m_buffer[1].Empty())
    {
        c->Close();
        // if (c->m_on_close_cb)
        // {
        //     c->m_on_close_cb(c, c->m_on_close_param);
        //     return;
        // }
        // delete c;
    }
}

JSONRPCServer::JSONRPCServer()
    : m_socket(-1),
      m_ev_base(NULL),
      m_ev { NULL, NULL },
      m_stop(false),
      m_counter(0)
{
    ;
}

JSONRPCServer::~JSONRPCServer()
{
}

int JSONRPCServer::GenUID()
{
    int id;

    while (1)
    {
        id = ++m_counter;

        if (!id)
            continue;

        if (HasClient(id))
            continue;

        break;
    }

    return id;
}

bool JSONRPCServer::Ready() const
{
    return (m_socket >= 0);
}

int JSONRPCServer::BindTCP(int port)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL,
                    std::to_string(port).c_str(),
                    &hints,
                    &res) != 0)
    {
        return -1;
    }

    do
    {
        m_socket = socket(res->ai_family,
                          res->ai_socktype,
                          res->ai_protocol);

        if (m_socket < 0)
            break;

        int opt = 1;

        if (setsockopt(m_socket,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &opt, sizeof(opt)) == -1)
        {
            break;
        }

        // non-blocking
        evutil_make_socket_nonblocking(m_socket);

        if (bind(m_socket,
                 res->ai_addr,
                 res->ai_addrlen) < 0)
        {
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

void JSONRPCServer::Stop()
{
    if (m_stop)
        return;

    {
        std::unique_lock<std::mutex>
            lock(m_task_lock);

        m_stop = true;
        m_task_cond.notify_all();
    }

    for (auto  &w : m_workers)
    {
        if (!w.joinable())
            continue;
        w.join();
    }

    if (m_ev_base)
    {
        event_base_loopbreak(m_ev_base);
    }
}

int JSONRPCServer::StartListen(int worker_num)
{
    if (!Ready())
        return -1;

    if (listen(m_socket, 20) < 0)
    {
        return -1;
    }

    m_ev_base = event_base_new();

    if (!m_ev_base)
    {
        return -1;
    }

    m_ev[0] = event_new(m_ev_base,
                        m_socket,
                        EV_READ | EV_PERSIST,
                        OnNewConn,
                        this);

    if (!m_ev[0])
    {
        event_base_free(m_ev_base);
        m_ev_base = NULL;
        return -1;
    }

    event_add(m_ev[0], NULL);

    int ii = 0;

    while (worker_num--)
    {
        ++ii;

        m_workers.emplace_back([ii, this]
        {
            DLOG("worker (%d) started", ii);

            for (;;)
            {
                Task t;

                {
                    std::unique_lock<std::mutex>
                        lock(this->m_task_lock);

                    this->m_task_cond.wait(
                        lock,
                        [this]
                        {
                            return (this->m_stop ||
                                    !this->m_tasks.empty());
                        });

                    if (this->m_stop &&
                        this->m_tasks.empty())
                    {
                        break;
                    }

                    DLOG("worker (%d) got a task", ii);

                    t = std::move(m_tasks.front());
                    m_tasks.pop();
                }

                doTask(std::move(t));
            }

            DLOG("worker (%d) stopped", ii);
        });
    }

    DLOG("start dispatching ...");
    event_base_dispatch(m_ev_base);

    return 0;
}

bool JSONRPCServer::HasMethod(const std::string &name)
{
    return m_methods.find(name) != m_methods.end();
}

void JSONRPCServer::RemoveMethod(const std::string &name)
{
    if (!HasMethod(name))
        return;

    m_methods.erase(name);
}

int JSONRPCServer::AddMethod(const std::string &name, const std::string &desc, Callback cb)
{
    if (!cb)
    {
        return -1;
    }

    if (HasMethod(name))
    {
        errno = EEXIST;
        return -1;
    }

    m_methods.emplace(name, Method { name, desc, cb});
    return 0;
}

int JSONRPCServer::AddMethod(const std::string &name, Callback cb)
{
    return AddMethod(name, name, cb);
}

void JSONRPCServer::OnNewConn(int ,short, void *userdata)
{
    auto *server = static_cast<JSONRPCServer*>(userdata);

    if (!server)
        return;

    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    int sock = accept(server->m_socket,
                      (struct sockaddr*) &addr,
                      &addrlen);

    if (sock < 0)
    {
        return;
    }

    server->NewClient(sock);
}

void JSONRPCServer::OnClientClose(Client *c, void *userdata)
{
    DLOG();
    auto *server = static_cast<JSONRPCServer*>(userdata);

    if (!server)
        return;

    server->RemoveClient(c->m_id);
}

bool JSONRPCServer::HasClient(int cid)
{
    return m_clients.find(cid) != m_clients.end();
}

Client* JSONRPCServer::GetClient(int cid)
{
    if (!HasClient(cid))
        return NULL;

    return m_clients.at(cid).get();
}

void JSONRPCServer::NewClient(int sock)
{
    std::unique_ptr<Client> c(
        new (std::nothrow) Client(sock, *this)); 

    if (!c)
        return;

    c->m_ev[0] = event_new(m_ev_base,
                           c->m_socket,
                           EV_READ | EV_PERSIST,
                           Client::OnRead,
                           c.get());

    if (!c->m_ev[0])
        return;

    c->m_ev[1] = event_new(m_ev_base,
                           c->m_socket,
                           EV_WRITE | EV_PERSIST,
                           Client::OnWrite,
                           c.get());

    if (!c->m_ev[1])
        return;

    c->m_on_close_cb = OnClientClose;
    c->m_on_close_param = this;
    
    // add read event
    event_add(c->m_ev[0], NULL);

    m_clients.emplace(c->m_id, std::move(c));
}

void JSONRPCServer::RemoveClient(int cid)
{
    if (!HasClient(cid))
        return;

    DLOG("remaining: %ld", m_clients.size());
    m_clients.erase(cid);
    DLOG("remaining: %ld", m_clients.size());
}

inline bool HasKey(nlohmann::json &j, const char *key)
{
    return j.find(key) != j.end();
}

inline nlohmann::json MakeError(int code, const char *msg)
{
    return nlohmann::json(
            {
                { "jsonrpc", "2.0" },
                { "id", NULL },
                { "error",
                    nlohmann::json(
                    {
                        "code", code,
                        "message", msg,
                    }
                )},
            });
}

inline nlohmann::json MakeResult(int id, const nlohmann::json &j)
{
    return nlohmann::json(
            {
                { "jsonrpc", "2.0" },
                { "id", id },
                { "result", j },
            });
}

int JSONRPCServer::Push(int cid, const char *data, unsigned int datalen)
{
    DLOG("%s: %d", data, datalen);

    Task t;

    std::unique_lock<std::mutex>
        lock(m_task_lock);

    if (m_stop)
    {
        return -1;
    }

    t.cid = cid;

    try
    {
        t.req = nlohmann::json::parse(
                  std::string(data, datalen));

        if (!HasKey(t.req, "jsonrpc") ||
            !HasKey(t.req, "method"))
        {
            doReply(t.cid,
                    MakeError(-32600, "Invalid Request."));
            return -1;
        }

        if (t.req["jsonrpc"] != "2.0")
        {
            doReply(t.cid,
                    MakeError(-32600, "Invalid Request."));
            return -1;
        }

        if (!HasMethod(t.req["method"]))
        {
            doReply(t.cid,
                    MakeError(-32601, "Method not found."));
            return -1;
        }

        bool is_notificaiton = !HasKey(t.req, "id");

        m_tasks.push(t);
        m_task_cond.notify_one();

        return (is_notificaiton) ? -1 : 0;
    }
    catch (nlohmann::json::parse_error &e)
    {
        doReply(cid,
                MakeError(-32700, "Parse Error"));
        return -1;
    }
}

void JSONRPCServer::doTask(Task &&t)
{
    auto *c = GetClient(t.cid);

    if (!c)
        return;

    auto &req = t.req;
    auto &resp = t.resp;

    auto &m = m_methods[req["method"]];

    int rc = m.cb(req["params"], resp);

    if (!HasKey(req, "id"))
    {
        // notification
        return;
    }

    auto &id = req["id"];

    if (rc < 0)
    {
        doReply(t.cid,
                MakeError(rc, "do task failed"));
        return;
    }

    if (resp.is_null())
    {
        doReply(t.cid,
                MakeResult(id, true));
        return;
    }

    doReply(t.cid,
            MakeResult(id, resp));
    return;
}

void JSONRPCServer::doReply(int cid, nlohmann::json &&r)
{
    auto *c = GetClient(cid);

    if (!c)
        return;

    std::string s = r.dump();
    uint16_t datalen = htons(s.size());

    c->Write((char*) &datalen, 2);
    c->Write(s.c_str(), s.size());

    c->CloseOnEmpty();

    return;
}

OOLONG_NS_END
