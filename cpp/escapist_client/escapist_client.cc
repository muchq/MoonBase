#include "escapist_client.h"

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

using grpc::ClientContext;
using grpc::Status;

namespace escapist {

std::string EscapistClient::InsertDoc(const std::string& collection, const std::string& bytes,
                                      const std::unordered_map<std::string, std::string> tags) {
  InsertDocRequest request;

  DocumentEgg doc_egg;
  doc_egg.set_bytes(bytes);
  auto& doc_egg_tags = *doc_egg.mutable_tags();
  for (auto& kv : tags) {
    doc_egg_tags.emplace(kv.first, kv.second);
  }

  request.set_collection(collection);
  request.set_allocated_doc(&doc_egg);

  InsertDocResponse reply;
  ClientContext context;

  Status status = stub_->InsertDoc(&context, request, &reply);

  // Act upon its status.
  if (status.ok()) {
    // return id and version and whatnot
    return "ok";
  } else {
    std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    return "RPC failed";
  }
}
}  // namespace escapist