#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liora/json.hpp"

namespace liora {

// JSON-RPC 2.0 error codes plus application-specific ones.
enum RpcCode : int {
  kRpcParseError = -32700,
  kRpcInvalidRequest = -32600,
  kRpcMethodNotFound = -32601,
  kRpcInvalidParams = -32602,
  kRpcInternalError = -32603,
  kRpcUnavailable = -32000,   // feature not available on this kernel/platform
  kRpcPermission = -32001,    // the caller may not use this method
  kRpcNotFound = -32002,
  kRpcConflict = -32003,
  kRpcUnsupported = -32004,
};

struct RpcError {
  int code = kRpcInternalError;
  std::string message;
  Json data = nullptr;

  Json toJson() const { return {{"code", code}, {"message", message}, {"data", data}}; }
};

// Thrown by handlers to return a structured error.
struct RpcException {
  RpcError error;
};

[[noreturn]] inline void rpcFail(int code, std::string message, Json data = nullptr) {
  throw RpcException{{code, std::move(message), std::move(data)}};
}

class RpcRouter {
 public:
  using Handler = std::function<Json(const Json& params)>;
  // Receives outgoing notifications, e.g. to post them into the WebUI.
  using Sink = std::function<void(const Json& message)>;

  void add(std::string method, Handler handler);
  // Every method under `prefix` (e.g. "settings") is listed by rpc.discover.
  void setNamespaceDescription(std::string prefix, std::string description);
  bool has(std::string_view method) const;
  std::vector<std::string> methods() const;

  void setSink(Sink sink);
  void notify(std::string_view method, const Json& params = nullptr);

  // Handles one request. Returns nullopt for notifications.
  Json handle(const Json& request);
  // Handles a batch or a single request, already parsed.
  Json handleDocument(const Json& document);

  Json describe() const;

 private:
  Json dispatch(const std::string& method, const Json& params, bool notification);

  mutable std::mutex mutex_;
  std::map<std::string, Handler, std::less<>> handlers_;
  std::map<std::string, std::string, std::less<>> namespaces_;
  Sink sink_;
};

// Parameter helpers. All of them raise kRpcInvalidParams on type errors.
namespace params {

const Json& object(const Json& value, const char* name);
std::optional<Json> optional(const Json& value, const char* name);
std::string string(const Json& value, const char* name);
std::optional<std::string> optionalString(const Json& value, const char* name);
int64_t integer(const Json& value, const char* name);
std::optional<int64_t> optionalInteger(const Json& value, const char* name);
bool boolean(const Json& value, const char* name, bool fallback);
std::vector<std::string> stringList(const Json& value, const char* name);

}  // namespace params

}  // namespace liora
