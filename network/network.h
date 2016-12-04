#ifndef SKYWALKER_NETWORK_NETWORK_H_
#define SKYWALKER_NETWORK_NETWORK_H_

#include <functional>
#include <map>

#include <voyager/core/bg_eventloop.h>
#include <voyager/core/eventloop.h>
#include <voyager/core/sockaddr.h>
#include <voyager/core/tcp_client.h>
#include <voyager/core/tcp_server.h>
#include <voyager/core/buffer.h>

#include "skywalker/nodeinfo.h"
#include "skywalker/slice.h"



namespace skywalker {

class Network {
 public:
  Network(const NodeInfo& my);
  ~Network();

  void StartServer(const std::function<void (const Slice& s)>& cb);
  void StopServer();

  void SendMessage(const NodeInfo& other, const Slice& message);

 private:
  void SendMessageInLoop(const voyager::SockAddr& addr, std::string* s);

  voyager::SockAddr addr_;
  voyager::BGEventLoop bg_loop_;
  voyager::EventLoop* loop_;
  voyager::TcpServer* server_;
  std::map<std::string, voyager::TcpConnectionPtr> connection_map_;

  // No copying allowed
  Network(const Network&);
  void operator=(const Network&);
};

}  // namespace skywalker

#endif   // SKYWALKER_NETWORK_NETWORK_H_
