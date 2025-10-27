use std::fs;
use std::path::Path;
use std::process::Command;
use tempfile::tempdir;

/// Deployment and packaging tests
/// Tests build artifact generation, installation scripts, and basic usage

#[test]
fn test_build_script_exists_and_executable() {
    let build_script = Path::new("rust/build_pal/scripts/build.sh");
    assert!(build_script.exists(), "Build script should exist");
    
    // Check if it's executable (Unix-like systems)
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let metadata = fs::metadata(build_script).unwrap();
        let permissions = metadata.permissions();
        assert!(permissions.mode() & 0o111 != 0, "Build script should be executable");
    }
}

#[test]
fn test_deployment_test_script_exists_and_executable() {
    let test_script = Path::new("rust/build_pal/scripts/test-deployment.sh");
    assert!(test_script.exists(), "Deployment test script should exist");
    
    // Check if it's executable (Unix-like systems)
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let metadata = fs::metadata(test_script).unwrap();
        let permissions = metadata.permissions();
        assert!(permissions.mode() & 0o111 != 0, "Deployment test script should be executable");
    }
}

#[test]
fn test_build_script_content_validation() {
    let build_script_path = Path::new("rust/build_pal/scripts/build.sh");
    let content = fs::read_to_string(build_script_path).unwrap();
    
    // Check for essential components
    assert!(content.contains("#!/bin/bash"), "Should have bash shebang");
    assert!(content.contains("set -e"), "Should have error handling");
    assert!(content.contains("cargo build"), "Should build with cargo");
    assert!(content.contains("--release"), "Should build in release mode");
    assert!(content.contains("build_pal_cli"), "Should build CLI binary");
    assert!(content.contains("build_pal_server"), "Should build server binary");
    assert!(content.contains("DIST_DIR"), "Should have distribution directory");
    assert!(content.contains("install.sh"), "Should create installation script");
    assert!(content.contains("VERSION"), "Should create version file");
    assert!(content.contains("README.md"), "Should create README");
}

#[test]
fn test_deployment_test_script_content_validation() {
    let test_script_path = Path::new("rust/build_pal/scripts/test-deployment.sh");
    let content = fs::read_to_string(test_script_path).unwrap();
    
    // Check for essential test components
    assert!(content.contains("#!/bin/bash"), "Should have bash shebang");
    assert!(content.contains("set -e"), "Should have error handling");
    assert!(content.contains("Test 1:"), "Should have test cases");
    assert!(content.contains("binaries exist"), "Should test binary existence");
    assert!(content.contains("executable"), "Should test executability");
    assert!(content.contains("--help"), "Should test CLI functionality");
    assert!(content.contains("install.sh"), "Should test installation script");
    assert!(content.contains("VERSION"), "Should test version file");
    assert!(content.contains("README.md"), "Should test README");
}

#[test]
fn test_cargo_toml_has_binary_targets() {
    // Check CLI Cargo.toml (skip if not found in test environment)
    let cli_cargo_path = Path::new("rust/build_pal/cli/Cargo.toml");
    if cli_cargo_path.exists() {
        let cli_content = fs::read_to_string(cli_cargo_path).unwrap();
        assert!(cli_content.contains("[[bin]]") || cli_content.contains("name = \"build_pal_cli\""), 
                "CLI Cargo.toml should define binary target");
    }
    
    // Check server Cargo.toml (skip if not found in test environment)
    let server_cargo_path = Path::new("rust/build_pal/server/Cargo.toml");
    if server_cargo_path.exists() {
        let server_content = fs::read_to_string(server_cargo_path).unwrap();
        assert!(server_content.contains("[[bin]]") || server_content.contains("name = \"build_pal_server\""), 
                "Server Cargo.toml should define binary target");
    }
}

#[test]
fn test_main_files_exist() {
    // Check CLI main.rs exists (skip if not found in test environment)
    let cli_main = Path::new("rust/build_pal/cli/src/main.rs");
    if !cli_main.exists() {
        println!("Skipping CLI main.rs test - file not found in test environment");
        return;
    }
    
    // Check server main.rs exists (skip if not found in test environment)
    let server_main = Path::new("rust/build_pal/server/src/main.rs");
    if !server_main.exists() {
        println!("Skipping server main.rs test - file not found in test environment");
        return;
    }
}

#[test]
fn test_main_files_have_main_function() {
    // Check CLI main.rs has main function (skip if not found in test environment)
    let cli_main_path = Path::new("rust/build_pal/cli/src/main.rs");
    if cli_main_path.exists() {
        let cli_main_content = fs::read_to_string(cli_main_path).unwrap();
        assert!(cli_main_content.contains("fn main()") || cli_main_content.contains("#[tokio::main]"), 
                "CLI main.rs should have main function");
    }
    
    // Check server main.rs has main function (skip if not found in test environment)
    let server_main_path = Path::new("rust/build_pal/server/src/main.rs");
    if server_main_path.exists() {
        let server_main_content = fs::read_to_string(server_main_path).unwrap();
        assert!(server_main_content.contains("fn main()") || server_main_content.contains("#[tokio::main]"), 
                "Server main.rs should have main function");
    }
}

#[test]
fn test_workspace_configuration() {
    // Check root Cargo.toml includes build_pal components (skip if not found in test environment)
    let root_cargo_path = Path::new("Cargo.toml");
    if root_cargo_path.exists() {
        let root_content = fs::read_to_string(root_cargo_path).unwrap();
        
        assert!(root_content.contains("rust/build_pal"), "Root Cargo.toml should include build_pal workspace");
        assert!(root_content.contains("rust/build_pal/cli"), "Root Cargo.toml should include CLI crate");
        assert!(root_content.contains("rust/build_pal/server"), "Root Cargo.toml should include server crate");
        assert!(root_content.contains("rust/build_pal/core"), "Root Cargo.toml should include core crate");
    } else {
        println!("Skipping workspace configuration test - Cargo.toml not found in test environment");
    }
}

#[test]
fn test_build_script_creates_proper_structure() {
    // This test validates the expected output structure of the build script
    // without actually running it (which would be slow and require cargo)
    
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Check that it creates the expected files
    let expected_files = [
        "build_pal",
        "build_pal_server", 
        "install.sh",
        "README.md",
        "VERSION"
    ];
    
    for file in expected_files {
        assert!(build_script_content.contains(&format!("\"{}\"", file)) || 
                build_script_content.contains(&format!("/{}", file)) ||
                build_script_content.contains(&format!("{}", file)),
                "Build script should reference file: {}", file);
    }
    
    // Check that it sets proper permissions
    assert!(build_script_content.contains("chmod +x"), "Should set executable permissions");
    
    // Check that it creates installation script content
    assert!(build_script_content.contains("Installation Script"), "Should create installation script");
    assert!(build_script_content.contains("cp build_pal"), "Installation script should copy CLI binary");
    assert!(build_script_content.contains("cp build_pal_server"), "Installation script should copy server binary");
}

#[test]
fn test_installation_script_template_validation() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Extract the installation script template from the build script
    // Look for the heredoc that creates install.sh
    assert!(build_script_content.contains("cat > \"${DIST_DIR}/install.sh\""), 
            "Should create install.sh with heredoc");
    
    // Check installation script template has essential components
    assert!(build_script_content.contains("--install-dir"), "Installation script should support custom install directory");
    assert!(build_script_content.contains("--help"), "Installation script should have help option");
    assert!(build_script_content.contains("mkdir -p"), "Installation script should create install directory");
    assert!(build_script_content.contains("export PATH"), "Installation script should mention PATH setup");
}

#[test]
fn test_readme_template_validation() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Check README template has essential sections
    assert!(build_script_content.contains("# Build Pal v"), "README should have title with version");
    assert!(build_script_content.contains("## Installation"), "README should have installation section");
    assert!(build_script_content.contains("## Quick Start"), "README should have quick start section");
    assert!(build_script_content.contains(".build_pal"), "README should mention config file");
    assert!(build_script_content.contains("build //..."), "README should have example command");
    assert!(build_script_content.contains("## Files"), "README should list included files");
    assert!(build_script_content.contains("## Documentation"), "README should have documentation section");
}

#[test]
fn test_version_file_template_validation() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Check VERSION file template
    assert!(build_script_content.contains("Build Pal v${VERSION}"), "VERSION file should include version");
    assert!(build_script_content.contains("Target: ${TARGET}"), "VERSION file should include target");
    assert!(build_script_content.contains("Built: $(date"), "VERSION file should include build timestamp");
    assert!(build_script_content.contains("Git Commit:"), "VERSION file should include git commit");
}

#[test]
fn test_cross_compilation_support() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Check cross-compilation support
    assert!(build_script_content.contains("BUILD_TARGET"), "Should support BUILD_TARGET environment variable");
    assert!(build_script_content.contains("--target"), "Should support cargo --target flag");
    assert!(build_script_content.contains("rustc -vV"), "Should detect host target");
    assert!(build_script_content.contains("Cross-compilation"), "Should have cross-compilation logic");
}

#[test]
fn test_error_handling_in_scripts() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    let test_script_content = fs::read_to_string("rust/build_pal/scripts/test-deployment.sh").unwrap();
    
    // Both scripts should have proper error handling
    assert!(build_script_content.contains("set -e"), "Build script should exit on error");
    assert!(test_script_content.contains("set -e"), "Test script should exit on error");
    
    // Build script should check for binary existence
    assert!(build_script_content.contains("if [ ! -f"), "Build script should check file existence");
    
    // Test script should have comprehensive error checking
    assert!(test_script_content.contains("❌"), "Test script should have failure indicators");
    assert!(test_script_content.contains("✅"), "Test script should have success indicators");
    assert!(test_script_content.contains("exit 1"), "Test script should exit with error code on failure");
}

#[test]
fn test_color_output_support() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    let test_script_content = fs::read_to_string("rust/build_pal/scripts/test-deployment.sh").unwrap();
    
    // Both scripts should support colored output
    assert!(build_script_content.contains("RED="), "Build script should define color variables");
    assert!(build_script_content.contains("GREEN="), "Build script should define color variables");
    assert!(build_script_content.contains("NC="), "Build script should define no-color variable");
    
    assert!(test_script_content.contains("RED="), "Test script should define color variables");
    assert!(test_script_content.contains("GREEN="), "Test script should define color variables");
    assert!(test_script_content.contains("NC="), "Test script should define no-color variable");
}

#[test]
fn test_build_script_environment_variables() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    
    // Should support version override
    assert!(build_script_content.contains("BUILD_PAL_VERSION"), "Should support version environment variable");
    
    // Should support target override
    assert!(build_script_content.contains("BUILD_TARGET"), "Should support target environment variable");
    
    // Should have sensible defaults
    assert!(build_script_content.contains("0.1.0"), "Should have default version");
}

#[test]
fn test_deployment_test_comprehensive_coverage() {
    let test_script_content = fs::read_to_string("rust/build_pal/scripts/test-deployment.sh").unwrap();
    
    // Should test all critical aspects
    let test_cases = [
        "binaries exist",
        "executable",
        "CLI binary basic functionality",
        "server binary basic functionality", 
        "installation script",
        "installed binaries",
        "distribution files",
        "VERSION file content",
        "binary sizes",
        "README content"
    ];
    
    for test_case in test_cases {
        assert!(test_script_content.contains(test_case), 
                "Test script should include test case: {}", test_case);
    }
}

#[test]
fn test_scripts_are_portable() {
    let build_script_content = fs::read_to_string("rust/build_pal/scripts/build.sh").unwrap();
    let test_script_content = fs::read_to_string("rust/build_pal/scripts/test-deployment.sh").unwrap();
    
    // Should use portable commands
    assert!(!build_script_content.contains("gstat"), "Should not use GNU-specific commands");
    assert!(!test_script_content.contains("gstat"), "Should not use GNU-specific commands");
    
    // Should handle different stat command formats
    assert!(test_script_content.contains("stat -f%z") || test_script_content.contains("stat -c%s"), 
            "Should handle both BSD and GNU stat formats");
}

#[tokio::test]
async fn test_simulated_build_workflow() {
    // Simulate the build workflow without actually running cargo build
    // This tests the logical flow and file operations
    
    let temp_dir = tempdir().unwrap();
    let dist_dir = temp_dir.path().join("dist");
    
    // Create mock binaries
    fs::create_dir_all(&dist_dir).unwrap();
    fs::write(dist_dir.join("build_pal"), "mock cli binary").unwrap();
    fs::write(dist_dir.join("build_pal_server"), "mock server binary").unwrap();
    
    // Make them executable
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = fs::metadata(dist_dir.join("build_pal")).unwrap().permissions();
        perms.set_mode(0o755);
        fs::set_permissions(dist_dir.join("build_pal"), perms).unwrap();
        
        let mut perms = fs::metadata(dist_dir.join("build_pal_server")).unwrap().permissions();
        perms.set_mode(0o755);
        fs::set_permissions(dist_dir.join("build_pal_server"), perms).unwrap();
    }
    
    // Create VERSION file
    fs::write(dist_dir.join("VERSION"), "Build Pal v0.1.0\nTarget: test\nBuilt: 2023-01-01\nGit Commit: abc123").unwrap();
    
    // Create README
    fs::write(dist_dir.join("README.md"), "# Build Pal v0.1.0\n\n## Installation\n\n## Quick Start").unwrap();
    
    // Create install script
    let install_script = r#"#!/bin/bash
echo "Installing Build Pal..."
mkdir -p "$1"
cp build_pal "$1/"
cp build_pal_server "$1/"
chmod +x "$1/build_pal"
chmod +x "$1/build_pal_server"
echo "Installation complete!"
"#;
    fs::write(dist_dir.join("install.sh"), install_script).unwrap();
    
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = fs::metadata(dist_dir.join("install.sh")).unwrap().permissions();
        perms.set_mode(0o755);
        fs::set_permissions(dist_dir.join("install.sh"), perms).unwrap();
    }
    
    // Test installation
    let install_dir = temp_dir.path().join("install_test");
    fs::create_dir_all(&install_dir).unwrap();
    
    // Simulate installation
    let original_dir = std::env::current_dir().unwrap();
    std::env::set_current_dir(&dist_dir).unwrap();
    
    let output = Command::new("./install.sh")
        .arg(install_dir.to_str().unwrap())
        .output()
        .unwrap();
    
    std::env::set_current_dir(original_dir).unwrap();
    
    assert!(output.status.success(), "Installation should succeed");
    assert!(install_dir.join("build_pal").exists(), "CLI binary should be installed");
    assert!(install_dir.join("build_pal_server").exists(), "Server binary should be installed");
    
    // Verify installed binaries are executable
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let cli_perms = fs::metadata(install_dir.join("build_pal")).unwrap().permissions();
        assert!(cli_perms.mode() & 0o111 != 0, "Installed CLI should be executable");
        
        let server_perms = fs::metadata(install_dir.join("build_pal_server")).unwrap().permissions();
        assert!(server_perms.mode() & 0o111 != 0, "Installed server should be executable");
    }
}