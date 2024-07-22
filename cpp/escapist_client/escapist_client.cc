#include "escapist_client.h"

#include <grpcpp/client_context.h>

namespace escapist {

using grpc::ClientContext;
using grpc::Status;

StatusOr<DocIdAndVersion> EscapistClient::InsertDoc(
    const std::string& collection, const std::string& bytes,
    const std::unordered_map<std::string, std::string>& tags) {
  InsertDocRequest request;

  DocumentEgg doc_egg;
  doc_egg.set_bytes(bytes);
  auto& doc_egg_tags = *doc_egg.mutable_tags();
  for (auto& kv : tags) {
    doc_egg_tags[kv.first] = kv.second;
  }

  request.set_collection(collection);
  request.set_allocated_doc(&doc_egg);

  InsertDocResponse reply;
  ClientContext context;

  Status rpc_status = stub_->InsertDoc(&context, request, &reply);

  if (rpc_status.ok()) {
    // return id and version and whatnot
    DocIdAndVersion docIdAndVersion;
    docIdAndVersion.id = reply.id();
    docIdAndVersion.version = reply.version();
    return docIdAndVersion;
  } else {
    // TODO: handle client errors
    std::cout << rpc_status.error_code() << ": " << rpc_status.error_message() << std::endl;
    return absl::InternalError(rpc_status.error_message());
  }
}
}  // namespace escapist
