#ifndef CPP_KEVN_KEVN_SERVICE_H
#define CPP_KEVN_KEVN_SERVICE_H

#include <grpcpp/grpcpp.h>

#include <string>
#include <utility>

#include "protos/kevn/kevn.grpc.pb.h"

namespace kevn {
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using std::string;

class KevnServiceImpl final : public Kevn::Service {
 public:
  explicit KevnServiceImpl(string  base_path) : instance_id_("foo"), base_path_(std::move(base_path)) {}

  Status ReadMetaData(ServerContext* context, const ReadMetaDataRequest* request,
                      ReadMetaDataResponse* reply) override;
  Status ReadContent(ServerContext* context, const ReadContentRequest* request,
                     ServerWriter<ReadContentResponse>* writer) override;
  Status Write(ServerContext* context, const WriteRequest* request, WriteResponse* reply) override;

 private:
  string instance_id_;
  string base_path_;
};

}  // namespace kevn

#endif
