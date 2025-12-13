use std::ffi::CString;
use nix::sched::{setns, CloneFlags};
use nix::sys::stat::Mode;
use nix::unistd::{execvpe, Pid, dup2, close};
use nix::fcntl::{open, OFlag};
use std::path::Path;
use std::os::fd::{RawFd,AsRawFd};
use std::io::{self, Write};
use std::fs::{self, OpenOptions};
use serde_json::Number;
use rand::Rng;
use std::os::unix::io::{FromRawFd};
use std::io::{BufRead, BufReader};
use libc::{close_range, CLOSE_RANGE_CLOEXEC};

use crate::common::config::CONTAINER_PATH;


pub fn exec_command(command: Vec<String>, env_vars: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<CString> = command.iter().map(|arg| CString::new(arg.as_bytes()).unwrap()).collect();
    for arg in &args {
        println!("arg: {}", arg.clone().into_string()?);
    }

    // Convert environment variables to CString
    let env_cvars: Vec<CString> = env_vars.iter().map(|var| CString::new(var.as_bytes()).unwrap()).collect();
    for env in &env_cvars {
        println!("env: {}", env.clone().into_string()?);
    }

    execvpe(&args[0], &args, &env_cvars)?; // Use execve to explicitly set environment variables
    Ok(())
}

pub fn number_to_pid(num: &Number) -> Result<Pid, String> {
    if let Some(value) = num.as_i64() {
    // Check if value is within i32 range
        if value >= i64::from(i32::MIN) && value <= i64::from(i32::MAX) {
            Ok(Pid::from_raw(value as i32))
        } else {
            Err(format!("Value {} is out of range for Pid", value))
        }
    } else {
        Err("Number is not an integer".to_string())
    }
}

pub fn join_namespace(pid: Pid, namespace: &str) -> io::Result<()> {
    let ns_path = format!("/proc/{}/ns/{}", pid, namespace);

    let fd: RawFd = open(Path::new(&ns_path), nix::fcntl::OFlag::O_RDONLY, Mode::empty())
        .map_err(|e| io::Error::new(io::ErrorKind::Other, format!("Failed to open {}: {}", ns_path, e)))?;

    let flag = match namespace {
        "mnt"     => CloneFlags::CLONE_NEWNS,
        "pid"     => CloneFlags::CLONE_NEWPID,
        "net"     => CloneFlags::CLONE_NEWNET,
        "uts"     => CloneFlags::CLONE_NEWUTS,
        // "ipc"     => CloneFlags::CLONE_NEWIPC, // share IPC namespace with host
        _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "Unsupported namespace")),
    };

    setns(fd, flag).map_err(|e| io::Error::new(io::ErrorKind::Other, format!("Failed to setns: {}", e)))?;

    Ok(())
}

pub fn generate_random_name() -> String {
    let mut rng = rand::thread_rng();
    let name: String = (0..16).map(|_| {
        let idx = rng.gen_range(0..16);
        format!("{:x}", idx)
    }).collect();
    name
}

pub fn redirect_stdfd(container_name: &str) -> io::Result<()> {
    // Open files for redirection
    let stdout_path = format!("{}/{}/stdout.log", CONTAINER_PATH, container_name);
    let stderr_path = format!("{}/{}/stderr.log", CONTAINER_PATH, container_name);

    println!("stdout_path: {}", stdout_path);

    let stdout_file = OpenOptions::new().write(true).create(true).open(&stdout_path.as_str())?;
    let stderr_file = OpenOptions::new().write(true).create(true).open(&stderr_path.as_str())?;

    // Get file descriptors
    let stdout_fd = stdout_file.as_raw_fd();
    let stderr_fd = stderr_file.as_raw_fd();

    dup2(stdout_fd, nix::libc::STDOUT_FILENO)?;
    dup2(stderr_fd, nix::libc::STDERR_FILENO)?;

    close(stdout_fd)?;
    close(stderr_fd)?;

    let dev_null_fd = open("/dev/null", OFlag::O_RDWR, nix::sys::stat::Mode::empty())?;
    dup2(dev_null_fd, nix::libc::STDIN_FILENO)?;
    Ok(())
}

pub fn close_extra_fds() -> io::Result<()> {
    unsafe {
        if close_range(3, u32::MAX, CLOSE_RANGE_CLOEXEC.try_into().unwrap()) != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    Ok(())
}

pub fn enable_loopback() -> Result<(), Box<dyn std::error::Error>> {
    use std::process::Command;

    Command::new("ip")
        .args(&["link", "set", "lo", "up"])
        .status()
        .map_err(|e| format!("Failed to enable loopback: {}", e))?;

    Ok(())
}


pub fn flush_config(container_dir: &str, config_str: String) -> Result<(), Box<dyn std::error::Error>> {
    let config_path = format!("{}/config.json", container_dir);
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(&config_path)?;
    file.write_all(config_str.as_bytes())?;
    file.sync_all()?;
    let dir = std::fs::File::open(container_dir)?;
    dir.sync_all()?;
    Ok(())
}

pub fn wait_for_peer_ready(pr: RawFd) -> Result<(), Box<dyn std::error::Error>> {
    let file = unsafe { std::fs::File::from_raw_fd(pr) };
    let mut reader = BufReader::new(file);
    let mut line = String::new();
    reader.read_line(&mut line)?;
    if line.trim() == "READY" {
        Ok(())
    } else {
        Err("shim failed to start container".into())
    }
}