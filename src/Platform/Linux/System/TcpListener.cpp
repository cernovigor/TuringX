// Copyright (c) 2021-2022, The TuringX Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Parts of this file are originally copyright (c) 2012-2016 The Cryptonote developers

#include "TcpListener.h"
#include <cassert>
#include <stdexcept>

#include <fcntl.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>

#include "Dispatcher.h"
#include "TcpConnection.h"
#include <System/ErrorMessage.h>
#include <System/InterruptedException.h>
#include <System/Ipv4Address.h>

namespace System {

TcpListener::TcpListener() : dispatcher(nullptr) {
}

TcpListener::TcpListener(Dispatcher& dispatcher, const Ipv4Address& addr, uint16_t port) : dispatcher(&dispatcher) {
  std::string message;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == -1) {
    message = "socket failed, " + lastErrorMessage();
  } else {
    int flags = fcntl(listener, F_GETFL, 0);
    if (flags == -1 || fcntl(listener, F_SETFL, flags | O_NONBLOCK) == -1) {
      message = "fcntl failed, " + lastErrorMessage();
    } else {
      int on = 1;
      if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == -1) {
        message = "setsockopt failed, " + lastErrorMessage();
      } else {
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl( addr.getValue());
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
          message = "bind failed, " + lastErrorMessage();
        } else if (listen(listener, SOMAXCONN) != 0) {
          message = "listen failed, " + lastErrorMessage();
        } else {
          epoll_event listenEvent;
          listenEvent.events = 0;
          listenEvent.data.ptr = nullptr;

          if (epoll_ctl(dispatcher.getEpoll(), EPOLL_CTL_ADD, listener, &listenEvent) == -1) {
            message = "epoll_ctl failed, " + lastErrorMessage();
          } else {
            context = nullptr;
            return;
          }
        }
      }
    }

    int result = close(listener);
    assert(result != -1);
  }

  throw std::runtime_error("TcpListener::TcpListener, " + message);
}

TcpListener::TcpListener(TcpListener&& other) : dispatcher(other.dispatcher) {
  if (other.dispatcher != nullptr) {
    assert(other.context == nullptr);
    listener = other.listener;
    context = nullptr;
    other.dispatcher = nullptr;
  }
}

TcpListener::~TcpListener() {
  if (dispatcher != nullptr) {
    assert(context == nullptr);
    int result = close(listener);
    assert(result != -1);
  }
}

TcpListener& TcpListener::operator=(TcpListener&& other) {
  if (dispatcher != nullptr) {
    assert(context == nullptr);
    if (close(listener) == -1) {
      throw std::runtime_error("TcpListener::operator=, close failed, " + lastErrorMessage());
    }
  }

  dispatcher = other.dispatcher;
  if (other.dispatcher != nullptr) {
    assert(other.context == nullptr);
    listener = other.listener;
    context = nullptr;
    other.dispatcher = nullptr;
  }

  return *this;
}

TcpConnection TcpListener::accept() {
  assert(dispatcher != nullptr);
  assert(context == nullptr);
  if (dispatcher->interrupted()) {
    throw InterruptedException();
  }

  ContextPair contextPair;
  OperationContext listenerContext;
  listenerContext.interrupted = false;
  listenerContext.context = dispatcher->getCurrentContext();

  contextPair.writeContext = nullptr;
  contextPair.readContext = &listenerContext;

  epoll_event listenEvent;
  listenEvent.events = EPOLLIN | EPOLLONESHOT;
  listenEvent.data.ptr = &contextPair;
  std::string message;
  if (epoll_ctl(dispatcher->getEpoll(), EPOLL_CTL_MOD, listener, &listenEvent) == -1) {
    message = "epoll_ctl failed, " + lastErrorMessage();
  } else {
    context = &listenerContext;
    dispatcher->getCurrentContext()->interruptProcedure = [&]() {
        assert(dispatcher != nullptr);
        assert(context != nullptr);
        OperationContext* listenerContext = static_cast<OperationContext*>(context);
        if (!listenerContext->interrupted) {
          epoll_event listenEvent;
          listenEvent.events = 0;
          listenEvent.data.ptr = nullptr;

          if (epoll_ctl(dispatcher->getEpoll(), EPOLL_CTL_MOD, listener, &listenEvent) == -1) {
            throw std::runtime_error("TcpListener::stop, epoll_ctl failed, " + lastErrorMessage() );
          }

          listenerContext->interrupted = true;
          dispatcher->pushContext(listenerContext->context);
        }
    };

    dispatcher->dispatch();
    dispatcher->getCurrentContext()->interruptProcedure = nullptr;
    assert(dispatcher != nullptr);
    assert(listenerContext.context == dispatcher->getCurrentContext());
    assert(contextPair.writeContext == nullptr);
    assert(context == &listenerContext);
    context = nullptr;
    listenerContext.context = nullptr;
    if (listenerContext.interrupted) {
      throw InterruptedException();
    }

    if((listenerContext.events & (EPOLLERR | EPOLLHUP)) != 0) {
      throw std::runtime_error("TcpListener::accept, accepting failed");
    }

    sockaddr inAddr;
    socklen_t inLen = sizeof(inAddr);
    int connection = ::accept(listener, &inAddr, &inLen);
    if (connection == -1) {
      message = "accept failed, " + lastErrorMessage();
    } else {
      int flags = fcntl(connection, F_GETFL, 0);
      if (flags == -1 || fcntl(connection, F_SETFL, flags | O_NONBLOCK) == -1) {
        message = "fcntl failed, " + lastErrorMessage();
      } else {
        return TcpConnection(*dispatcher, connection);
      }

      int result = close(connection);
      assert(result != -1);
    }
  }

  throw std::runtime_error("TcpListener::accept, " + message);
}

}
