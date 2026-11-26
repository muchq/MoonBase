use std::net::TcpListener;
use std::path::PathBuf;
use std::process::{Child, Command};
use std::thread;
use std::time::{Duration, Instant};

struct Server(Child);

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[test]
fn the_packaged_solver_uses_its_own_graph() {
    let directory = PathBuf::from(std::env::var_os("TEST_TMPDIR").unwrap());
    assert!(
        Command::new("tar")
            .args(["-xf", "domains/games/apis/mithril/mithril_tar.tar", "-C"])
            .arg(&directory)
            .status()
            .unwrap()
            .success()
    );

    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    drop(listener);
    let mut server = Server(
        Command::new(directory.join("mithril"))
            .env("PORT", address.port().to_string())
            .env("RUNFILES_DIR", &directory)
            .spawn()
            .unwrap(),
    );

    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        assert!(server.0.try_wait().unwrap().is_none(), "Mithril exited");
        let output = Command::new("curl")
            .args([
                "--fail",
                "--silent",
                "--noproxy",
                "*",
                "--max-time",
                "1",
                "-H",
                "Content-Type: application/json",
                "-d",
                r#"{"start":"sake","end":"sail"}"#,
            ])
            .arg(format!("http://{address}/mithril/v1/wordchain"))
            .output()
            .unwrap();
        if output.status.success() {
            assert_eq!(
                output.stdout,
                br#"{"path":["sake","sane","sand","said","sail"]}"#
            );
            return;
        }
        thread::sleep(Duration::from_millis(50));
    }
    panic!("Mithril did not answer at {address}");
}
