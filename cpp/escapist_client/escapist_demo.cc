#include <grpcpp/create_channel.h>

#include "absl/log/initialize.h"
#include "cpp/escapist_client/escapist_client.h"

using namespace escapist;

void print_doc_id_and_version(const string& op, StatusOr<DocIdAndVersion>& status_doc_id) {
  std::cout << op << ":" << std::endl;
  std::cout << "     status: " << status_doc_id.ok() << std::endl;
  std::cout << "         id: " << status_doc_id->id << std::endl;
  std::cout << "    version: " << status_doc_id->version << std::endl << std::endl;
}

void print_doc(const string& op, StatusOr<Doc> status_doc) {
  std::cout << op << ":" << std::endl;
  std::cout << "     status: " << status_doc.ok() << std::endl;
  std::cout << "         id: " << status_doc->id << std::endl;
  std::cout << "    version: " << status_doc->version << std::endl;
  std::cout << "      bytes: " << status_doc->bytes << std::endl;
  std::cout << "       tags: " << std::endl;
  for (const auto& [tag_name, tag_value] : status_doc->tags) {
    std::cout << "             " << tag_name << ": " << tag_value << std::endl;
  }
  std::cout << std::endl;
}

int main() {
  absl::InitializeLog();

  auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
  auto stub = std::make_shared<Escapist::Stub>(Escapist::Stub(channel));
  EscapistClient client(stub, "demo");

  DocEgg doc_egg{"hello this is nice", {{"player_1", "Tippy"}}};

  auto result1 = client.InsertDoc("golf", doc_egg);
  print_doc_id_and_version("InsertDoc", result1);

  auto result2 = client.FindDocByTags("golf", {{"player_1", "Tippy"}});
  print_doc("FindDocByTags", result2);

  DocIdAndVersion id_to_update{result2->id, result2->version};
  DocEgg update_doc{"new bytes yo", {{"player_1", "Tippy"}, {"is_over", "true"}}};

  auto result3 = client.UpdateDoc("golf", id_to_update, update_doc);
  print_doc_id_and_version("UpdateDoc", result3);

  auto result4 = client.FindDocById("golf", result3->id);
  print_doc("FindDocById", result4);

  return 0;
}
