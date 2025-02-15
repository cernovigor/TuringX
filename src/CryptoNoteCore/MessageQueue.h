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

#pragma once

#include <queue>

#include "IntrusiveLinkedList.h"

#include "System/Event.h"
#include "System/InterruptedException.h"

namespace CryptoNote {

template<class MessageType> class MessageQueue {
public:
  MessageQueue(System::Dispatcher& dispatcher);

  const MessageType& front();
  void pop();
  void push(const MessageType& message);

  void stop();

  typename IntrusiveLinkedList<MessageQueue<MessageType>>::hook& getHook();
  
private:
  void wait();
  std::queue<MessageType> messageQueue;
  System::Event event;
  bool stopped;

  typename IntrusiveLinkedList<MessageQueue<MessageType>>::hook hook;
};

template<class MessageQueueContainer, class MessageType>
class MesageQueueGuard {
public:
  MesageQueueGuard(MessageQueueContainer& container, MessageQueue<MessageType>& messageQueue) : container(container), messageQueue(messageQueue) {
    container.addMessageQueue(messageQueue);
  }

  MesageQueueGuard(const MesageQueueGuard& other) = delete;
  MesageQueueGuard& operator=(const MesageQueueGuard& other) = delete;

  ~MesageQueueGuard() {
    container.removeMessageQueue(messageQueue);
  }
private:
  MessageQueueContainer& container;
  MessageQueue<MessageType>& messageQueue;
};

template<class MessageType>
MessageQueue<MessageType>::MessageQueue(System::Dispatcher& dispatcher) : event(dispatcher), stopped(false) {}

template<class MessageType>
void MessageQueue<MessageType>::wait() {
  if (messageQueue.empty()) {
    if (stopped) {
      throw System::InterruptedException();
    }

    event.clear();
    while (!event.get()) {
      event.wait();
    }
  }
}

template<class MessageType>
const MessageType& MessageQueue<MessageType>::front() {
  wait();
  return messageQueue.front();
}

template<class MessageType>
void MessageQueue<MessageType>::pop() {
  wait();
  messageQueue.pop();
}

template<class MessageType>
void MessageQueue<MessageType>::push(const MessageType& message) {
  messageQueue.push(message);
  event.set();
}

template<class MessageType>
void MessageQueue<MessageType>::stop() {
  stopped = true;
  event.set();
}

template<class MessageType>
typename IntrusiveLinkedList<MessageQueue<MessageType>>::hook& MessageQueue<MessageType>::getHook() {
  return hook;
}

}
