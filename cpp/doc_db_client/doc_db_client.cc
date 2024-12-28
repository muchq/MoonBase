#include "doc_db_client.h"

#include <grpcpp/client_context.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace doc_db {

using grpc::ClientContext;
using std::string;
using std::unordered_map;

StatusOr<DocIdAndVersion> DocDbClient::InsertDoc(const string& collection,
                                                 const DocEgg& input_doc_egg) {
  if (collection.empty()) {
    return absl::InvalidArgumentError("collection cannot be empty");
  }
  if (input_doc_egg.bytes.empty()) {
    return absl::InvalidArgumentError("bytes cannot be empty");
  }

  InsertDocRequest request;
  request.set_collection(collection);
  PopulateDocEgg(request.mutable_doc(), input_doc_egg);

  InsertDocResponse rpc_reply;
  auto context = MakeClientContext();

  auto rpc_status = stub_->InsertDoc(context.get(), request, &rpc_reply);
  return HandleIdAndVersionResponse(rpc_status, rpc_reply.id(), rpc_reply.version());
}

StatusOr<DocIdAndVersion> DocDbClient::UpdateDoc(const string& collection,
                                                 const DocIdAndVersion& input_id,
                                                 const DocEgg& input_doc_egg) {
  if (collection.empty()) {
    return absl::InvalidArgumentError("collection cannot be empty");
  }
  if (input_id.id.empty()) {
    return absl::InvalidArgumentError("id cannot be empty");
  }
  if (input_id.version.empty()) {
    return absl::InvalidArgumentError("version cannot be empty");
  }
  if (input_doc_egg.bytes.empty()) {
    return absl::InvalidArgumentError("bytes cannot be empty");
  }

  UpdateDocRequest request;
  request.set_collection(collection);
  request.set_id(input_id.id);
  request.set_version(input_id.version);
  PopulateDocEgg(request.mutable_doc(), input_doc_egg);

  UpdateDocResponse rpc_reply;
  auto context = MakeClientContext();

  auto rpc_status = stub_->UpdateDoc(context.get(), request, &rpc_reply);
  return HandleIdAndVersionResponse(rpc_status, rpc_reply.id(), rpc_reply.version());
}

StatusOr<Doc> DocDbClient::FindDocById(const string& collection, const string& id) {
  if (collection.empty()) {
    return absl::InvalidArgumentError("collection cannot be empty");
  }
  if (id.empty()) {
    return absl::InvalidArgumentError("id cannot be empty");
  }

  FindDocByIdRequest request;
  request.set_collection(collection);
  request.set_id(id);

  FindDocByIdResponse rpc_reply;
  auto context = MakeClientContext();

  auto rpc_status = stub_->FindDocById(context.get(), request, &rpc_reply);

  return HandleDocResponse(rpc_status, rpc_reply.doc());
}

StatusOr<Doc> DocDbClient::FindDocByTags(const string& collection,
                                         const unordered_map<string, string>& tags) {
  if (collection.empty()) {
    return absl::InvalidArgumentError("collection cannot be empty");
  }
  if (tags.empty()) {
    return absl::InvalidArgumentError("tags cannot be empty");
  }

  FindDocRequest request;
  request.set_collection(collection);
  auto& mutable_tags = *request.mutable_tags();
  for (auto& kv : tags) {
    mutable_tags[kv.first] = kv.second;
  }

  FindDocResponse rpc_reply;
  auto context = MakeClientContext();

  auto rpc_status = stub_->FindDoc(context.get(), request, &rpc_reply);

  return HandleDocResponse(rpc_status, rpc_reply.doc());
}

std::unique_ptr<ClientContext> DocDbClient::MakeClientContext() {
  std::unique_ptr<ClientContext> client_context = std::make_unique<ClientContext>();
  client_context->AddMetadata("db-name", db_);
  return client_context;
}

void DocDbClient::PopulateDocEgg(DocumentEgg* mutable_doc_egg, const DocEgg& input_doc_egg) {
  mutable_doc_egg->set_bytes(input_doc_egg.bytes);
  auto& doc_egg_tags = *mutable_doc_egg->mutable_tags();
  for (auto& kv : input_doc_egg.tags) {
    doc_egg_tags[kv.first] = kv.second;
  }
}

StatusOr<DocIdAndVersion> DocDbClient::HandleIdAndVersionResponse(const grpc::Status& rpc_status,
                                                                  const string& id,
                                                                  const string& version) {
  if (rpc_status.ok()) {
    DocIdAndVersion output_id;
    output_id.id = id;
    output_id.version = version;
    return output_id;
  } else {
    auto status_code = absl::StatusCode(rpc_status.error_code());
    return absl::Status(status_code, rpc_status.error_message());
  }
}

StatusOr<Doc> DocDbClient::HandleDocResponse(const grpc::Status& rpc_status, const Document& doc) {
  if (rpc_status.ok()) {
    Doc output_doc;
    output_doc.id = doc.id();
    output_doc.version = doc.version();
    output_doc.bytes = doc.bytes();
    output_doc.tags = unordered_map<string, string>(doc.tags().begin(), doc.tags().end());
    return output_doc;
  } else {
    auto status_code = absl::StatusCode(rpc_status.error_code());
    return absl::Status(status_code, rpc_status.error_message());
  }
}

}  // namespace doc_db
