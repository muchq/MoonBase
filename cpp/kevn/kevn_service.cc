#include "cpp/kevn/kevn_service.h"

#include <grpcpp/grpcpp.h>

#include <string>

#include "protos/kevn/kevn.grpc.pb.h"

namespace kevn {

using grpc::ServerContext;
using grpc::Status;

Status KevnServiceImpl::ReadMetaData(ServerContext* context, const ReadMetaDataRequest* request,
                                     ReadMetaDataResponse* reply) {
  std::string prefix("Hello ");
  reply->set_key(prefix + request->key());
  return Status::OK;
};

Status KevnServiceImpl::ReadContent(ServerContext* context, const ReadContentRequest* request,
                                    ServerWriter<ReadContentResponse>* writer) {
  return Status::OK;
}

Status KevnServiceImpl::Write(ServerContext* context, const WriteRequest* request,
                              WriteResponse* reply) {
  return Status::OK;
};

}  // namespace kevn
