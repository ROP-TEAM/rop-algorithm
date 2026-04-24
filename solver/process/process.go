package process

import (
	"fmt"
	"net"
	"os/exec"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// Handle holds a running solver subprocess and its gRPC connection.
type Handle struct {
	Conn *grpc.ClientConn
	stop func()
}

// Start launches binaryPath as a subprocess on a free port and waits until ready.
func Start(binaryPath string) (*Handle, error) {
	port, err := freePort()
	if err != nil {
		return nil, fmt.Errorf("find free port: %w", err)
	}
	addr := fmt.Sprintf("127.0.0.1:%d", port)

	cmd := exec.Command(binaryPath, addr)
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start solver binary: %w", err)
	}

	if err := waitForReady(addr, 10*time.Second); err != nil {
		cmd.Process.Kill()
		return nil, err
	}

	conn, err := grpc.NewClient(addr, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		cmd.Process.Kill()
		return nil, fmt.Errorf("dial solver: %w", err)
	}

	return &Handle{
		Conn: conn,
		stop: func() { conn.Close(); cmd.Process.Kill() },
	}, nil
}

// Stop closes the gRPC connection and terminates the subprocess.
func (h *Handle) Stop() { h.stop() }

// waitForReady polls the TCP address until it accepts a connection or timeout.
func waitForReady(addr string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		conn, err := net.DialTimeout("tcp", addr, 100*time.Millisecond)
		if err == nil {
			conn.Close()
			return nil
		}
		time.Sleep(50 * time.Millisecond)
	}
	return fmt.Errorf("solver not ready at %s after %s", addr, timeout)
}

func freePort() (int, error) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}
