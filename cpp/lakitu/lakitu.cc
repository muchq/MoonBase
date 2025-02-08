#include "lakitu.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

namespace lakitu {

uint16_t ReadPort(const uint16_t default_port) {
  if (const char* env_p = std::getenv("PORT")) {
    return static_cast<uint16_t>(std::atoi(env_p));
  }
  return default_port;
}

void Server::AddListeningPort(const std::string& address,
                              std::shared_ptr<grpc::ServerCredentials> creds) {
  builder.AddListeningPort(address, std::move(creds));
  server_address_ = address;
}

void Server::EnableHealthChecks() { health_checks_enabled_ = true; }
void Server::DisableHealthChecks() { health_checks_enabled_ = false; }

void Server::EnableReflection() { reflection_ = true; }
void Server::DisableReflection() { reflection_ = false; }

void Server::AddInterceptorFactory(
    std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface> interceptor_factory) {
  interceptor_creators.emplace_back(std::move(interceptor_factory));
}

void Server::AddService(grpc::Service* service) { builder.RegisterService(service); }
void Server::StartServer() {
  builder.experimental().SetInterceptorCreators(std::move(interceptor_creators));
  grpc::EnableDefaultHealthCheckService(health_checks_enabled_);
  if (reflection_) {
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  }
  delegate_ = builder.BuildAndStart();
}
void Server::Wait() const { delegate_->Wait(); }
void Server::StartAndWait() {
  this->StartServer();
  std::cout << "Server listening on " << server_address_ << std::endl;
  delegate_->Wait();
}

}  // namespace lakitu