#include "liora/rpc.hpp"

#include <algorithm>

namespace liora {

void RpcRouter::add(std::string method, Handler handler) {
  std::lock_guard lock(mutex_);
  handlers_[std::move(method)] = std::move(handler);
}

void RpcRouter::setNamespaceDescription(std::string prefix, std::string description) {
  std::lock_guard lock(mutex_);
  namespaces_[std::move(prefix)] = std::move(description);
}

bool RpcRouter::has(std::string_view method) const {
  std::lock_guard lock(mutex_);
  return handlers_.find(method) != handlers_.end();
}

std::vector<std::string> RpcRouter::methods() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
  out.reserve(handlers_.size());
  for (const auto& [name, handler] : handlers_) out.push_back(name);
  return out;
}

void RpcRouter::setSink(Sink sink) {
  std::lock_guard lock(mutex_);
  sink_ = std::move(sink);
}

void RpcRouter::notify(std::string_view method, const Json& params) {
  Sink sink;
  {
    std::lock_guard lock(mutex_);
    sink = sink_;
  }
  if (!sink) return;
  Json message = {{"jsonrpc", "2.0"}, {"method", std::string(method)}};
  if (!params.is_null()) message["params"] = params;
  sink(message);
}

Json RpcRouter::dispatch(const std::string& method, const Json& params, bool notification) {
  Handler handler;
  {
    std::lock_guard lock(mutex_);
    auto it = handlers_.find(method);
    if (it != handlers_.end()) handler = it->second;
  }
  if (!handler) {
    RpcError error{kRpcMethodNotFound, "unknown method: " + method, nullptr};
    if (notification) return nullptr;
    return {{"jsonrpc", "2.0"}, {"error", error.toJson()}};
  }
  try {
    Json result = handler(params);
    if (notification) return nullptr;
    return {{"jsonrpc", "2.0"}, {"result", std::move(result)}};
  } catch (const RpcException& e) {
    if (notification) return nullptr;
    return {{"jsonrpc", "2.0"}, {"error", e.error.toJson()}};
  } catch (const std::exception& e) {
    RpcError error{kRpcInternalError, e.what()};
    if (notification) return nullptr;
    return {{"jsonrpc", "2.0"}, {"error", error.toJson()}};
  }
}

Json RpcRouter::handle(const Json& request) {
  if (!request.is_object()) {
    return {{"jsonrpc", "2.0"}, {"error", RpcError{kRpcInvalidRequest, "request must be an object"}.toJson()}};
  }
  if (request.value("jsonrpc", "") != "2.0") {
    return {{"jsonrpc", "2.0"}, {"error", RpcError{kRpcInvalidRequest, "jsonrpc must be \"2.0\""}.toJson()}};
  }
  auto method = request.find("method");
  if (method == request.end() || !method->is_string()) {
    return {{"jsonrpc", "2.0"}, {"error", RpcError{kRpcInvalidRequest, "method must be a string"}.toJson()}};
  }
  bool notification = !request.contains("id");
  Json params = request.contains("params") ? request["params"] : Json::object();

  Json response = dispatch(method->get<std::string>(), params, notification);
  if (notification || !request.contains("id")) return response;
  response["id"] = request["id"];
  return response;
}

Json RpcRouter::handleDocument(const Json& document) {
  if (!document.is_array()) return handle(document);
  if (document.empty()) {
    return {{"jsonrpc", "2.0"}, {"error", RpcError{kRpcInvalidRequest, "empty batch"}.toJson()}};
  }
  Json out = Json::array();
  for (const auto& item : document) {
    Json response = handle(item);
    if (!response.is_null()) out.push_back(std::move(response));
  }
  if (out.empty()) return nullptr;
  return out;
}

Json RpcRouter::describe() const {
  Json list = Json::array();
  std::lock_guard lock(mutex_);
  for (const auto& [name, handler] : handlers_) list.push_back(name);
  Json groups = Json::object();
  for (const auto& [prefix, description] : namespaces_) groups[prefix] = description;
  return {{"methods", list}, {"namespaces", groups}};
}

namespace params {

namespace {

[[noreturn]] void invalid(const std::string& message) { rpcFail(kRpcInvalidParams, message); }

const Json* find(const Json& value, const char* name) {
  if (!value.is_object()) invalid("params must be an object");
  auto it = value.find(name);
  return it == value.end() || it->is_null() ? nullptr : &*it;
}

}  // namespace

const Json& object(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) invalid(std::string("missing parameter: ") + name);
  if (!v->is_object()) invalid(std::string(name) + " must be an object");
  return *v;
}

std::optional<Json> optional(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) return std::nullopt;
  return *v;
}

std::string string(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) invalid(std::string("missing parameter: ") + name);
  if (!v->is_string()) invalid(std::string(name) + " must be a string");
  return v->get<std::string>();
}

std::optional<std::string> optionalString(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) return std::nullopt;
  if (!v->is_string()) invalid(std::string(name) + " must be a string");
  return v->get<std::string>();
}

int64_t integer(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) invalid(std::string("missing parameter: ") + name);
  if (!v->is_number_integer()) invalid(std::string(name) + " must be an integer");
  return v->get<int64_t>();
}

std::optional<int64_t> optionalInteger(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) return std::nullopt;
  if (!v->is_number_integer()) invalid(std::string(name) + " must be an integer");
  return v->get<int64_t>();
}

bool boolean(const Json& value, const char* name, bool fallback) {
  const Json* v = find(value, name);
  if (!v) return fallback;
  if (!v->is_boolean()) invalid(std::string(name) + " must be a boolean");
  return v->get<bool>();
}

std::vector<std::string> stringList(const Json& value, const char* name) {
  const Json* v = find(value, name);
  if (!v) return {};
  if (!v->is_array()) invalid(std::string(name) + " must be a list of strings");
  std::vector<std::string> out;
  for (const auto& item : *v) {
    if (!item.is_string()) invalid(std::string(name) + " must be a list of strings");
    out.push_back(item.get<std::string>());
  }
  return out;
}

}  // namespace params

}  // namespace liora
