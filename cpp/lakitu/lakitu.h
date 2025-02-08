#ifndef CPP_LAKITU_LAKITU_H
#define CPP_LAKITU_LAKITU_H

#include <grpcpp/grpcpp.h>

#include <string>

namespace lakitu {
uint16_t ReadPort(uint16_t default_port);

class Server {
 public:
  void AddListeningPort(const std::string& address, std::shared_ptr<grpc::ServerCredentials> creds);
  void EnableHealthChecks();
  void DisableHealthChecks();
  void EnableReflection();
  void DisableReflection();
  void AddInterceptorFactory(
      std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface> interceptor_factory);
  void AddService(grpc::Service* service);
  void StartServer();
  void Wait() const;
  void StartAndWait();

 private:
  bool health_checks_enabled_ = true;
  bool reflection_ = true;
  std::string server_address_;
  grpc::ServerBuilder builder;
  std::unique_ptr<grpc::Server> delegate_;
  std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
      interceptor_creators;
};
}  // namespace lakitu

#endif
