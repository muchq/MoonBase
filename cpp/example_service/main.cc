#include "absl/strings/str_format.h"
#include "cpp/example_service/example_service.h"
#include "cpp/lakitu/lakitu.h"

using lakitu::ReadPort;
using lakitu::Server;

void RunServer(uint16_t port) {
  const std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  GreeterServiceImpl service;

  Server server;
  server.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  server.AddService(&service);
  server.StartAndWait();
}

int main() {
  RunServer(ReadPort(8080));
  return 0;
}
