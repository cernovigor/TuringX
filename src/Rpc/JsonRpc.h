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

#include <boost/optional.hpp>
#include <boost/foreach.hpp>
#include <functional>

#include "CoreRpcServerCommandsDefinitions.h"
#include <Common/JsonValue.h>
#include "Serialization/ISerializer.h"
#include "Serialization/SerializationTools.h"

namespace CryptoNote {

class HttpClient;
  
namespace JsonRpc {

const int errParseError = -32700;
const int errInvalidRequest = -32600;
const int errMethodNotFound = -32601;
const int errInvalidParams = -32602;
const int errInternalError = -32603;

class JsonRpcError: public std::exception {
public:
  JsonRpcError();
  JsonRpcError(int c);
  JsonRpcError(int c, const std::string& msg);

#ifdef _MSC_VER
  virtual const char* what() const override {
#else
  virtual const char* what() const noexcept override {
#endif
    return message.c_str();
  }

  void serialize(ISerializer& s) {
    s(code, "code");
    s(message, "message");
  }

  int code;
  std::string message;
};

typedef boost::optional<Common::JsonValue> OptionalId;

class JsonRpcRequest {
public:
  
  JsonRpcRequest() : psReq(Common::JsonValue::OBJECT) {}

  bool parseRequest(const std::string& requestBody) {
    try {
      psReq = Common::JsonValue::fromString(requestBody);
    } catch (std::exception&) {
      throw JsonRpcError(errParseError);
    }

    if (!psReq.contains("method")) {
      throw JsonRpcError(errInvalidRequest);
    }

    method = psReq("method").getString();

    if (psReq.contains("id")) {
      id = psReq("id");
    }

    return true;
  }

  template <typename T>
  bool loadParams(T& v) const {
    loadFromJsonValue(v, psReq.contains("params") ? 
      psReq("params") : Common::JsonValue(Common::JsonValue::NIL));
    return true;
  }

  template <typename T>
  bool setParams(const T& v) {
    psReq.set("params", storeToJsonValue(v));
    return true;
  }

  const std::string& getMethod() const {
    return method;
  }

  void setMethod(const std::string& m) {
    method = m;
  }

  const OptionalId& getId() const {
    return id;
  }

  std::string getBody() {
    psReq.set("jsonrpc", std::string("2.0"));
    psReq.set("method", method);
    return psReq.toString();
  }

private:

  Common::JsonValue psReq;
  OptionalId id;
  std::string method;
};


class JsonRpcResponse {
public:

  JsonRpcResponse() : psResp(Common::JsonValue::OBJECT) {}

  void parse(const std::string& responseBody) {
    try {
      psResp = Common::JsonValue::fromString(responseBody);
    } catch (std::exception&) {
      throw JsonRpcError(errParseError);
    }
  }

  void setId(const OptionalId& id) {
    if (id.is_initialized()) {
      psResp.insert("id", id.get());
    }
  }

  void setError(const JsonRpcError& err) {
    psResp.set("error", storeToJsonValue(err));
  }

  bool getError(JsonRpcError& err) const {
    if (!psResp.contains("error")) {
      return false;
    }

    loadFromJsonValue(err, psResp("error"));
    return true;
  }

  std::string getBody() {
    psResp.set("jsonrpc", std::string("2.0"));
    return psResp.toString();
  }

  template <typename T>
  bool setResult(const T& v) {
    psResp.set("result", storeToJsonValue(v));
    return true;
  }

  template <typename T>
  bool getResult(T& v) const {
    if (!psResp.contains("result")) {
      return false;
    }

    loadFromJsonValue(v, psResp("result"));
    return true;
  }

private:
  Common::JsonValue psResp;
};


void invokeJsonRpcCommand(HttpClient& httpClient, JsonRpcRequest& req, JsonRpcResponse& res);

template <typename Request, typename Response>
void invokeJsonRpcCommand(HttpClient& httpClient, const std::string& method, const Request& req, Response& res) {
  JsonRpcRequest jsReq;
  JsonRpcResponse jsRes;

  jsReq.setMethod(method);
  jsReq.setParams(req);

  invokeJsonRpcCommand(httpClient, jsReq, jsRes);

  jsRes.getResult(res);
}

template <typename Request, typename Response, typename Handler>
bool invokeMethod(const JsonRpcRequest& jsReq, JsonRpcResponse& jsRes, Handler handler) {
  Request req;
  Response res;

  if (!std::is_same<Request, CryptoNote::EMPTY_STRUCT>::value && !jsReq.loadParams(req)) {
    throw JsonRpcError(JsonRpc::errInvalidParams);
  }

  bool result = handler(req, res);

  if (result) {
    if (!jsRes.setResult(res)) {
      throw JsonRpcError(JsonRpc::errInternalError);
    }
  }
  return result;
}

typedef std::function<bool(void*, const JsonRpcRequest& req, JsonRpcResponse& res)> JsonMemberMethod;

template <typename Class, typename Params, typename Result>
JsonMemberMethod makeMemberMethod(bool (Class::*handler)(const Params&, Result&)) {
  return [handler](void* obj, const JsonRpcRequest& req, JsonRpcResponse& res) {
    return JsonRpc::invokeMethod<Params, Result>(
      req, res, std::bind(handler, static_cast<Class*>(obj), std::placeholders::_1, std::placeholders::_2));
  };
}


}


}
