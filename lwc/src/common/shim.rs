use std::fs;
use std::os::unix::net::{UnixListener, UnixStream};
use std::os::fd::{AsRawFd};
use std::io::{BufRead, BufReader,Write};
use std::path::Path;
use nix::unistd::{ForkResult, mkdir, Pid};
use nix::sched::{unshare, CloneFlags};
use nix::sys::wait::{waitpid, WaitPidFlag, WaitStatus};
use nix::sys::signal;
use nix::poll::{poll, PollFd, PollFlags};
use std::time::{Duration,Instant};
use nix::mount::{mount, umount2, MntFlags, MsFlags};
use std::env;
use nix::sys::stat::{mknod, Mode, SFlag};
use debug_print::{debug_println};
use std::os::unix::io::{FromRawFd, RawFd};
use nix::unistd::{pipe, close};

use crate::common::config::{CONTAINER_PATH, Config};
use crate::common::utils::{exec_command, close_extra_fds, redirect_stdfd, enable_loopback, flush_config, wait_for_peer_ready};


// perform pivot_root
fn pivot_root(new_root: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // change current directory to new root
    env::set_current_dir(new_root)?;

    // pivot_root requires that the source and target mounts are not PRIVATE,
    // so we adjust mount flags here.
    // This must be done inside a separate mount namespace to avoid affecting the host.
    mount(None::<&Path>, "/", None::<&str>,
        MsFlags::MS_REC | MsFlags::MS_SLAVE, None::<&str>)?;

    mount(None::<&Path>, ".", None::<&str>,
        MsFlags::MS_PRIVATE, None::<&str>)?;

    mount(None::<&Path>, ".", None::<&str>,
        MsFlags::MS_SLAVE, None::<&str>)?;

    // perform pivot_root(".", ".")
    nix::unistd::pivot_root(".", ".")?;

    // unmount the old root filesystem
    umount2(".", MntFlags::MNT_DETACH)?;
    Ok(())
}


// Setup procfs
fn mount_procfs() -> Result<(), Box<dyn std::error::Error>> {
    // mount procfs
    mount(
        Some("proc"),
        "/proc",
        Some("proc"),
        MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID | MsFlags::MS_NODEV,
        None::<&str>
    )?;

    Ok(())
}


fn create_device_node(path: &Path, major: u64, minor: u64, mode: Mode) -> Result<(), Box<dyn std::error::Error>> {
    // create device node
    let dev = nix::sys::stat::makedev(major, minor);
    let sflag = SFlag::S_IFCHR; // character device
    mknod(path, sflag, mode, dev)?;

    Ok(())
}


fn mount_dev() -> Result<(), Box<dyn std::error::Error>> {
    // create /dev/ directory (if not exists)
    let dev_dir = Path::new("/dev");
    if !dev_dir.exists() {
        mkdir(dev_dir, Mode::S_IRWXU)?;
    }

    // create /dev/null
    create_device_node(Path::new("/dev/null"), 1, 3,
        Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IWGRP | Mode::S_IROTH | Mode::S_IWOTH)?;

    // create /dev/zero
    create_device_node(Path::new("/dev/zero"), 1, 5,
        Mode::S_IRUSR | Mode::S_IWUSR | Mode::S_IRGRP | Mode::S_IWGRP | Mode::S_IROTH | Mode::S_IWOTH)?;

    Ok(())
}


fn mount_sysfs_and_cgroupfs() -> Result<(), Box<dyn std::error::Error>> {
    // mount sysfs
    let sys_dir = Path::new("/sys");
    if !sys_dir.exists() {
        mkdir(sys_dir, Mode::S_IRWXU)?;
    }
    mount(
        Some("sysfs"),
        "/sys",
        Some("sysfs"),
        MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID | MsFlags::MS_NODEV,
        None::<&str>,
    )?;

    // mount cgroupfs (assuming cgroup v2)
    let cgroup_dir = Path::new("/sys/fs/cgroup");
    if !cgroup_dir.exists() {
        mkdir(cgroup_dir, Mode::S_IRWXU)?;
    }
    mount(
        Some("cgroup2"),
        "/sys/fs/cgroup",
        Some("cgroup2"),
        MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID | MsFlags::MS_NODEV,
        None::<&str>,
    )?;

    Ok(())
}


struct Stopper {
    stream: UnixStream,
    term_sent_at: Instant
}


fn poll_socket(
    listener: &UnixListener,
    stopper: &mut Option<Stopper>,
    container_pid: Pid,
) -> Result<(), Box<dyn std::error::Error>> {
    let mut fds = [PollFd::new(listener.as_raw_fd(), PollFlags::POLLIN)];
    poll(&mut fds, 500)?;

    if !fds[0]
        .revents()
        .unwrap_or(PollFlags::empty())
        .contains(PollFlags::POLLIN)
    {
        return Ok(())
    }
    match listener.accept() {
    Ok((stream, _addr)) => {
        let mut reader = BufReader::new(&stream);
        let mut line = String::new();
        if reader.read_line(&mut line)? > 0 && line.trim() == "stop" {
            nix::sys::signal::kill(container_pid, signal::SIGTERM).ok();
            *stopper = Some(Stopper {
                stream,
                term_sent_at: Instant::now()
            });
            debug_println!("shim: received stop signal, sent SIGTERM");
        }
        Ok(())
    },
    Err(e) => Err(Box::new(e))
    }

}


pub fn run_shim(container_name: &str, command: Vec<String>, mut config: Config, ready_fd: RawFd) -> Result<(), Box<dyn std::error::Error>> {
    redirect_stdfd(container_name)?;
    let container_dir = format!("{}{}", CONTAINER_PATH, container_name);
    let sock_path = format!("{}/shim.sock", container_dir);
    if Path::new(&sock_path).exists() {
        fs::remove_file(&sock_path)?;
    }
    let listener = UnixListener::bind(&sock_path)?;

    let (pr, pw) = pipe()?;
    unshare(CloneFlags::CLONE_NEWPID | CloneFlags::CLONE_NEWNET | CloneFlags::CLONE_NEWUTS)?;
    match unsafe { nix::unistd::fork()? } {
        ForkResult::Child => {
            close(pr)?;
            close(ready_fd)?;
            // init process of container
            enable_loopback()?;

            unshare(CloneFlags::CLONE_NEWNS)?;
            let rootfs = format!("{}/rootfs", container_dir);
            pivot_root(Path::new(&rootfs))?;
            debug_println!("Unshare done.");

            mount_procfs()?;
            mount_dev()?;
            mount_sysfs_and_cgroupfs()?;

            debug_println!("mount done.");
            // execute start command
            let mut f = unsafe { std::fs::File::from_raw_fd(pw) };
            f.write_all(b"READY\n")?;
            f.flush()?;

            close_extra_fds()?;
            let env_vars = config.env.clone().unwrap_or_default();
            exec_command(command, env_vars)?;
            println!("exec_command failed.");
        }
        ForkResult::Parent { child: container_pid } => {
            // shim process
            config.pid = Some(container_pid.as_raw().into());
            config.state = Some("running".to_string());
            flush_config(&container_dir, serde_json::to_string(&config)?)?;

            close(pw)?;
            wait_for_peer_ready(pr)?;
            // notify start CLI that config file is ready
            let mut f = unsafe { std::fs::File::from_raw_fd(ready_fd) };
            f.write_all(b"READY\n")?;
            f.flush()?;

            let mut stopper: Option<Stopper> = None;
            loop {
                poll_socket(&listener, &mut stopper, container_pid)?;

                let wait_result = waitpid(container_pid, Some(WaitPidFlag::WNOHANG));
                let exit_code = match wait_result {
                    Ok(WaitStatus::Exited(_, code)) => code,
                    _ => -1,
                };
                match wait_result {
                    Ok(WaitStatus::StillAlive) => {
                        if stopper.as_ref().map(|s| s.term_sent_at.elapsed() > Duration::from_secs(10)).unwrap_or(false) {
                            nix::sys::signal::kill(container_pid, signal::SIGKILL).ok();
                        }
                    }
                    Ok(WaitStatus::Exited(..)) | Ok(WaitStatus::Signaled(..)) => {
                        config.exitcode = Some(exit_code.into());
                        config.state = Some("exited".to_string());
                        flush_config(&container_dir, serde_json::to_string(&config)?)?;
                        debug_println!("shim: container exited with {}", exit_code);
                        if let Some(s) = stopper.as_mut() {
                            s.stream.write_all(b"OK\n")?;
                            s.stream.flush()?;
                        }
                        break;
                    }
                    Ok(_) | Err(_) => unimplemented!("unexpected WaitStatus")
                }
            }
        }
    }

    Ok(())
}
