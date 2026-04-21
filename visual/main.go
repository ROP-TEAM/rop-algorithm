package main

import (
	"embed"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os/exec"
	"runtime"
	"time"
)

//go:embed web
var webFiles embed.FS

const port = "7171"

func main() {
	sub, err := fs.Sub(webFiles, "web")
	if err != nil {
		log.Fatal(err)
	}
	http.Handle("/", http.FileServer(http.FS(sub)))

	url := "http://localhost:" + port
	fmt.Println("CVRP Visualizer running at", url)

	go openBrowser(url)

	log.Fatal(http.ListenAndServe(":"+port, nil))
}

func openBrowser(url string) {
	time.Sleep(300 * time.Millisecond)
	switch runtime.GOOS {
	case "windows":
		exec.Command("cmd", "/c", "start", url).Start()
	case "darwin":
		exec.Command("open", url).Start()
	default:
		exec.Command("xdg-open", url).Start()
	}
}
