package main

import (
	"flag"
	"fmt"
	doc_db_client "github.com/muchq/moonbase/domains/platform/libs/doc_db_client_go"
	"log"

	pb "github.com/muchq/moonbase/domains/platform/protos/doc_db"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	addr = flag.String("addr", "localhost:50051", "the address to connect to")
)

func main() {
	flag.Parse()
	conn, err := grpc.NewClient(*addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()
	stub := pb.NewDocDbClient(conn)
	client := doc_db_client.NewDocDbClient(stub, "demo")

	docEgg := doc_db_client.DocEgg{
		Bytes: []byte("hello this is nice"),
		Tags:  map[string]string{"player_1": "Tippy"},
	}

	res1, err := client.InsertDoc("golf", docEgg)
	printDocIdAndVersion("InsertDoc", res1, err)

	res2, err := client.FindDocByTags("golf", map[string]string{"player_1": "Tippy"})
	printDoc("FindDocByTags", res2, err)

	idToUpdate := doc_db_client.DocIdAndVersion{
		Id:      res2.Id,
		Version: res2.Version,
	}

	updatedDoc := doc_db_client.DocEgg{
		Bytes: []byte("new bytes yo"),
		Tags:  map[string]string{"player_1": "Tippy", "is_over": "true"},
	}

	res3, err := client.UpdateDoc("golf", idToUpdate, updatedDoc)
	printDocIdAndVersion("UpdateDoc", res3, err)

	res4, err := client.FindDocById("golf", res3.Id)
	printDoc("FindDocById", res4, err)
}

func printDocIdAndVersion(op string, doc_id doc_db_client.DocIdAndVersion, err error) {
	fmt.Println(op, ":")
	if err != nil {
		fmt.Println("    error:", err.Error())
	} else {
		fmt.Println("    id:", doc_id.Id)
		fmt.Println("    version:", doc_id.Version)
		fmt.Println()
	}
}

func printDoc(op string, doc doc_db_client.Doc, err error) {
	fmt.Println(op, ":")
	if err != nil {
		fmt.Println("    error:", err.Error())
	} else {
		fmt.Println("    id:", doc.Id)
		fmt.Println("    version:", doc.Version)
		fmt.Println("    bytes:", string(doc.Bytes))
		fmt.Println("    tags:")
		for k, v := range doc.Tags {
			fmt.Println("        ", k, ":", v)
		}
		fmt.Println()
	}
}
