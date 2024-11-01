package server

import (
	"context"
	pb "github.com/muchq/moonbase/protos/escapist"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

type EscapistServer struct {
	pb.UnimplementedEscapistServer
	dao *DaoInterface
}

func (*EscapistServer) InsertDoc(context.Context, *pb.InsertDocRequest) (*pb.InsertDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method InsertDoc not implemented")
}
func (*EscapistServer) UpdateDoc(context.Context, *pb.UpdateDocRequest) (*pb.UpdateDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method UpdateDoc not implemented")
}
func (*EscapistServer) FindDocById(context.Context, *pb.FindDocByIdRequest) (*pb.FindDocByIdResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method FindDocById not implemented")
}
func (*EscapistServer) FindDoc(context.Context, *pb.FindDocRequest) (*pb.FindDocResponse, error) {
	return nil, status.Errorf(codes.Unimplemented, "method FindDoc not implemented")
}

func NewEscapistServer() *grpc.Server {
	escapistServer := &EscapistServer{}
	s := grpc.NewServer()
	pb.RegisterEscapistServer(s, escapistServer)
	return s
}
