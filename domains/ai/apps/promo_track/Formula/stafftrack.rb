class Stafftrack < Formula
  desc "Local-first AI agent helping senior engineers demonstrate Staff-level impact"
  homepage "https://github.com/muchq/MoonBase"
  license "MIT"
  version "0.1.0"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/muchq/MoonBase/releases/download/stafftrack-v#{version}/stafftrack-#{version}-aarch64-apple-darwin.tar.gz"
      sha256 "PLACEHOLDER"
    else
      url "https://github.com/muchq/MoonBase/releases/download/stafftrack-v#{version}/stafftrack-#{version}-x86_64-apple-darwin.tar.gz"
      sha256 "PLACEHOLDER"
    end
  end

  on_linux do
    url "https://github.com/muchq/MoonBase/releases/download/stafftrack-v#{version}/stafftrack-#{version}-x86_64-unknown-linux-gnu.tar.gz"
    sha256 "PLACEHOLDER"
  end

  def install
    bin.install "stafftrack"
  end

  # For development: build from source instead of downloading a release
  # To use: `brew install --build-from-source ./Formula/stafftrack.rb`
  head "https://github.com/muchq/MoonBase.git", branch: "main"

  head do
    depends_on "rust" => :build

    def install
      system "cargo", "build", "--release", "--manifest-path", "rust/promo_track/Cargo.toml"
      bin.install "target/release/stafftrack"
    end
  end

  test do
    assert_match "StaffTrack", shell_output("#{bin}/stafftrack --version")
  end
end
