#ifndef CPP_ESCAPIST_CLIENT_H
#define CPP_ESCAPIST_CLIENT_H

#include "protos/escapist/escapist.grpc.pb.h"

namespace escapist {

class EscapistClient {
 public:
  EscapistClient(std::shared_ptr<Escapist::Stub> stub) : stub_(stub) {}

  std::string InsertDoc(const std::string& collection,
                                      const std::string& bytes,
                                      const std::unordered_map<std::string, std::string> tags);

 private:
  std::shared_ptr<Escapist::Stub> stub_;
};

}  // namespace escapist

#endif  // CPP_ESCAPIST_CLIENT_H
