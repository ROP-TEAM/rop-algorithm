package process

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"runtime"
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

	env, err := solverEnv()
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(binaryPath, addr)
	cmd.Env = env
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

// solverEnv returns the current environment with a discovered MSYS2 mingw64
// bin dir prepended on Windows. Checks MSYS2_ROOT env var first, then scans
// common install locations across all drive letters. Errors if none found,
// since the solver binary requires mingw64 DLLs to start.
func solverEnv() ([]string, error) {
	env := os.Environ()
	if runtime.GOOS != "windows" {
		return env, nil
	}
	msys2Bin, found := findMsys2Bin()
	if !found {
		return nil, fmt.Errorf(
			"solver requires MSYS2 mingw64 DLLs: set MSYS2_ROOT env var to your MSYS2 install directory (e.g. C:\\msys64)",
		)
	}
	for i, e := range env {
		if len(e) >= 5 && e[:5] == "PATH=" {
			env[i] = "PATH=" + msys2Bin + string(os.PathListSeparator) + e[5:]
			return env, nil
		}
	}
	return append(env, "PATH="+msys2Bin), nil
}

func findMsys2Bin() (string, bool) {
	if root := os.Getenv("MSYS2_ROOT"); root != "" {
		p := root + `\mingw64\bin`
		if _, err := os.Stat(p); err == nil {
			return p, true
		}
	}
	for _, drive := range "CDEFGHIJKLMNOPQRSTUVWXYZ" {
		for _, name := range []string{"msys64", "msys2"} {
			p := fmt.Sprintf(`%c:\%s\mingw64\bin`, drive, name)
			if _, err := os.Stat(p); err == nil {
				return p, true
			}
		}
	}
	return "", false
}

func freePort() (int, error) {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}
