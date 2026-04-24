//go:generate protoc --go_out=../grpc/pb --go_opt=paths=source_relative --go-grpc_out=../grpc/pb --go-grpc_opt=paths=source_relative solver.proto
package proto
