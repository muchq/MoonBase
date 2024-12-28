#include "doc_db_client.h"

#include "protos/doc_db/doc_db_mock.grpc.pb.h"

using namespace doc_db;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

DocEgg MakeDocEgg(string bytes, unordered_map<string, string> tags) {
  DocEgg input_doc_egg;
  input_doc_egg.bytes = bytes;
  input_doc_egg.tags = tags;
  return input_doc_egg;
}

DocIdAndVersion MakeInputIds(string id, string version) {
  DocIdAndVersion input_ids;
  input_ids.id = id;
  input_ids.version = version;
  return input_ids;
}

TEST(DocDbClient, InsertDocRpcSuccess) {
  // Arrange
  InsertDocResponse resp;
  resp.set_id("foo");
  resp.set_version("123");

  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, InsertDoc(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});

  // Act
  auto status = client.InsertDoc("foo_col", input_doc_egg);

  // Assert
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status->id, "foo");
  EXPECT_EQ(status->version, "123");
}

TEST(DocDbClient, InsertDocRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, InsertDoc(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});

  // Act
  auto status = client.InsertDoc("foo_col", input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::CANCELLED));
}

TEST(DocDbClient, InsertDocClientValidatesCollection) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});

  // Act
  auto status = client.InsertDoc("", input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, InsertDocClientValidatesBytes) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("", {});

  // Act
  auto status = client.InsertDoc("foo_col", input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, UpdateDocRpcSuccess) {
  // Arrange
  UpdateDocResponse resp;
  resp.set_id("foo");
  resp.set_version("123");

  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, UpdateDoc(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});
  DocIdAndVersion input_id;
  input_id.id = "foo";
  input_id.version = "001";

  // Act
  auto status = client.UpdateDoc("foo_col", input_id, input_doc_egg);

  // Assert
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status->id, "foo");
  EXPECT_EQ(status->version, "123");
}

TEST(DocDbClient, UpdateDocRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, UpdateDoc(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});
  DocIdAndVersion input_ids = MakeInputIds("foo", "001");

  // Act
  auto status = client.UpdateDoc("foo_col", input_ids, input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::CANCELLED));
}

TEST(DocDbClient, UpdateDocClientValidatesCollection) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});
  DocIdAndVersion input_ids = MakeInputIds("foo", "001");

  // Act
  auto status = client.UpdateDoc("", input_ids, input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, UpdateDocClientValidatesId) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});
  DocIdAndVersion input_ids = MakeInputIds("", "001");

  // Act
  auto status = client.UpdateDoc("foo_col", input_ids, input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, UpdateDocClientValidatesVersion) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("cool bytes", {});
  DocIdAndVersion input_ids = MakeInputIds("foo", "");

  // Act
  auto status = client.UpdateDoc("foo_col", input_ids, input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, UpdateDocClientValidatesBytes) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  DocEgg input_doc_egg = MakeDocEgg("", {});
  DocIdAndVersion input_ids = MakeInputIds("foo", "001");

  // Act
  auto status = client.UpdateDoc("foo_col", input_ids, input_doc_egg);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, FindDocByIdRpcSuccess) {
  // Arrange
  FindDocByIdResponse resp;
  auto doc = resp.mutable_doc();
  doc->set_id("foo");
  doc->set_version("123");
  doc->set_bytes("neat bytes");

  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, FindDocById(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  DocDbClient client(stub, "test");

  // Act
  auto status = client.FindDocById("foo_col", "foo");

  // Assert
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status->id, "foo");
  EXPECT_EQ(status->version, "123");
  EXPECT_EQ(status->bytes, "neat bytes");
  EXPECT_TRUE(status->tags.empty());
}

TEST(DocDbClient, FindDocByIdRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, FindDocById(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));
  DocDbClient client(stub, "test");

  // Act
  auto status = client.FindDocById("foo_col", "foo");

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::CANCELLED));
}

TEST(DocDbClient, FindDocByIdClientValidatesCollection) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");

  // Act
  auto status = client.FindDocById("", "foo");

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, FindDocByIdClientValidatesId) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");

  // Act
  auto status = client.FindDocById("foo_col", "");

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, FindDocByTagsRpcSuccess) {
  // Arrange
  FindDocResponse resp;
  auto doc = resp.mutable_doc();
  doc->set_id("foo");
  doc->set_version("123");
  doc->set_bytes("neat bytes");
  auto& tags = *doc->mutable_tags();
  tags["player_1"] = "Tippy";

  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, FindDoc(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(resp), Return(grpc::Status::OK)));

  DocDbClient client(stub, "test");
  unordered_map<string, string> input_tags;
  input_tags["player_1"] = "Tippy";

  // Act
  auto status = client.FindDocByTags("foo_col", input_tags);

  // Assert
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(status->id, "foo");
  EXPECT_EQ(status->version, "123");
  EXPECT_EQ(status->bytes, "neat bytes");
  EXPECT_EQ(status->tags["player_1"], "Tippy");
}

TEST(DocDbClient, FindDocByTagsRpcFailure) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  ON_CALL(*stub, FindDoc(_, _, _)).WillByDefault(Return(grpc::Status::CANCELLED));

  DocDbClient client(stub, "test");
  unordered_map<string, string> input_tags;
  input_tags["player_1"] = "Tippy";

  // Act
  auto status = client.FindDocByTags("foo_col", input_tags);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::CANCELLED));
}

TEST(DocDbClient, FindDocByTagsClientValidatesCollection) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  unordered_map<string, string> input_tags;
  input_tags["player_1"] = "Tippy";

  // Act
  auto status = client.FindDocByTags("", input_tags);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST(DocDbClient, FindDocByTagsClientValidatesTags) {
  // Arrange
  auto stub = std::make_shared<MockDocDbStub>();
  DocDbClient client(stub, "test");
  unordered_map<string, string> input_tags;

  // Act
  auto status = client.FindDocByTags("foo_col", input_tags);

  // Assert
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode(grpc::StatusCode::INVALID_ARGUMENT));
}
