#ifndef OOLONG_RPC_SERVER_H
#define OOLONG_RPC_SERVER_H

#include <stddef.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>

#include <event2/event.h>

#include "json.hpp"

#include "oolong.h"
#include "buffer/buffer.h"

OOLONG_NS_BEGIN

class Client;

class JSONRPCServer
{
public:
    typedef int (*Callback)(const nlohmann::json &params,
                            nlohmann::json &result);

    struct Method
    {
        std::string name;
        std::string desc;
        Callback cb;
    };

    struct Task
    {
        int cid;
        nlohmann::json req;
        nlohmann::json resp;
    };

    static JSONRPCServer& Instance()
    {
        static JSONRPCServer s_server;
        return s_server;
    }

    bool Ready() const;

    int BindTCP(int port);
    int BindUnix(const std::string &path);

    int StartListen(int worker_num = 4);

    void Stop();

    int AddMethod(const std::string &name, Callback cb);

    int AddMethod(const std::string &name,
                  const std::string &info,
                  Callback cb);

    void RemoveMethod(const std::string &name);

    bool HasMethod(const std::string &name);

    // new conn event cb
    static void OnNewConn(int, short, void*);

    // client close cb
    static void OnClientClose(Client*, void*);

    friend Client;

private:

    int GenUID();

    // AddTask
    int Push(int cid, const char *data, unsigned int datalen);

    void NewClient(int sock);

    bool HasClient(int);

    void RemoveClient(int);

    Client* GetClient(int);

    JSONRPCServer();
    virtual ~JSONRPCServer();

    void doTask(Task &&t);
    void doReply(int cid, nlohmann::json &&result);

    int m_socket;
    struct event_base *m_ev_base;
    struct event *m_ev[2];

    bool m_stop;

    uint32_t m_counter;

    // rpc clients
    std::map<int, std::unique_ptr<Client>> m_clients;

    // worker
    std::vector<std::thread> m_workers;

    // task queue
    std::mutex m_task_lock;
    std::condition_variable m_task_cond;
    std::queue<Task> m_tasks;

    //RPC methods
    std::map<std::string, Method> m_methods;
};

OOLONG_NS_END

#endif
