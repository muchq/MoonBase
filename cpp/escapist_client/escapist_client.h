#ifndef CPP_ESCAPIST_CLIENT_H
#define CPP_ESCAPIST_CLIENT_H

#include "protos/escapist/escapist.grpc.pb.h"

#include "absl/status/statusor.h"

namespace escapist {

using absl::StatusOr;

struct DocIdAndVersion {
  std::string id;
  std::string version;
};

class EscapistClient {
 public:
  EscapistClient(std::shared_ptr<Escapist::StubInterface> stub) : stub_(stub) {}

  StatusOr<DocIdAndVersion> InsertDoc(const std::string& collection, const std::string& bytes,
                        const std::unordered_map<std::string, std::string>& tags);

 private:
  std::shared_ptr<Escapist::StubInterface> stub_;
};

}  // namespace escapist

#endif  // CPP_ESCAPIST_CLIENT_H
