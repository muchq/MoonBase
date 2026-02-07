package lib

import (
	"context"
	pb "github.com/muchq/moonbase/domains/platform/protos/doc_db"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

type DocDbServer struct {
	pb.UnimplementedDocDbServer
	dao *DaoInterface
}

func (*DocDbServer) InsertDoc(context.Context, *pb.InsertDocRequest) (*pb.InsertDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method InsertDoc not implemented")
}
func (*DocDbServer) UpdateDoc(context.Context, *pb.UpdateDocRequest) (*pb.UpdateDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method UpdateDoc not implemented")
}
func (*DocDbServer) FindDocById(context.Context, *pb.FindDocByIdRequest) (*pb.FindDocByIdResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method FindDocById not implemented")
}
func (*DocDbServer) FindDoc(context.Context, *pb.FindDocRequest) (*pb.FindDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method FindDoc not implemented")
}

func NewDocDbServer() *grpc.Server {
	docDbServer := &DocDbServer{}
	s := grpc.NewServer()
	pb.RegisterDocDbServer(s, docDbServer)
	return s
}
