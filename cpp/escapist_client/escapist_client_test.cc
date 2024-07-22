#include "escapist_client.h"
#include "protos/escapist/escapist_mock.grpc.pb.h"

using namespace escapist;
using ::testing::_;
using ::testing::Return;


TEST(EscapistClient, InsertDocSuccess) {
  auto stub = std::make_shared<MockEscapistStub>();
  ON_CALL(*stub, InsertDoc(_, _, _))
    .WillByDefault(Return(grpc::Status::OK));
  EscapistClient client(stub);

  // do stuff here
}
