#ifndef CPP_ESCAPIST_CLIENT_H
#define CPP_ESCAPIST_CLIENT_H

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "absl/status/statusor.h"
#include "protos/escapist/escapist.grpc.pb.h"

namespace escapist {

using absl::StatusOr;
using std::shared_ptr;
using std::string;
using std::unordered_map;

struct DocIdAndVersion {
  string id;
  string version;
};

struct Doc {
  string id;
  string version;
  string bytes;
  unordered_map<string, string> tags;
};

struct DocEgg {
  string bytes;
  unordered_map<string, string> tags;
};

class EscapistClient {
 public:
  explicit EscapistClient(shared_ptr<Escapist::StubInterface> stub, string db) : stub_(std::move(stub)), db_(db) {}

  StatusOr<DocIdAndVersion> InsertDoc(const string& collection, const DocEgg& input_doc_egg);

  StatusOr<DocIdAndVersion> UpdateDoc(const string& collection,
                                      const DocIdAndVersion& doc_id_and_version,
                                      const DocEgg& input_doc_egg);

  StatusOr<Doc> FindDocById(const string& collection, const string& id);

  StatusOr<Doc> FindDocByTags(const string& collection, const unordered_map<string, string>& tags);

 private:
  std::unique_ptr<grpc::ClientContext> MakeClientContext();
  static void PopulateDocEgg(DocumentEgg* doc, const DocEgg& docEgg);
  static StatusOr<DocIdAndVersion> HandleIdAndVersionResponse(const grpc::Status& rpc_status,
                                                              const string& id,
                                                              const string& version);
  static StatusOr<Doc> HandleDocResponse(const grpc::Status& rpc_status, const Document& doc);

  shared_ptr<Escapist::StubInterface> stub_;
  string db_;
};

}  // namespace escapist

#endif  // CPP_ESCAPIST_CLIENT_H
