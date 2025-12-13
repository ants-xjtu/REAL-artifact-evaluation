use crate::common::{config,utils};
use std::process::exit;
use std::fs;
use std::path::Path;
use std::os::unix::io::RawFd;
use libc::{pid_t, c_int, c_uint};
use nix::unistd::{fork, ForkResult, close};

unsafe fn pidfd_open(pid: pid_t, flags: c_uint) -> Result<RawFd, std::io::Error> {
    let fd = libc::syscall(libc::SYS_pidfd_open, pid, flags) as c_int;
    if fd < 0 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(fd)
}

unsafe fn wait_pidfd(pidfd: RawFd) -> Result<(), std::io::Error> {
    let mut siginfo: libc::siginfo_t = std::mem::zeroed();
    const P_PIDFD: c_uint = 3;
    const WEXITED: c_int = 0x00000004;

    if libc::waitid(P_PIDFD, pidfd as _, &mut siginfo, WEXITED) != 0 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}


pub fn exec_main(container_name: &str, command: Vec<String>, env_cli: Vec<String>, detach: bool) -> Result<(), Box<dyn std::error::Error>> {
    let container_dir = format!("{}{}", config::CONTAINER_PATH, container_name);

    if !Path::new(&container_dir).is_dir() {
        eprintln!("Error: {} is not a valid directory.", container_dir);
        exit(1);
    }

    let config_file = format!("{}/config.json", container_dir);

    let config_data = fs::read_to_string(&config_file)?;
    let config: config::Config = serde_json::from_str(&config_data)?;

    let mut env_vars = config.env.clone().unwrap_or_default();
    env_vars.extend(env_cli);

    let pid = utils::number_to_pid(&config.pid.unwrap())?;

    utils::join_namespace(pid, "pid")?;
    match unsafe { fork() }? {
        ForkResult::Child => {
            if detach {
                utils::redirect_stdfd(container_name)?;
                close(nix::libc::STDIN_FILENO)?;
            }
            utils::close_extra_fds()?;
            utils::join_namespace(pid, "uts")?;
            utils::join_namespace(pid, "net")?;
            utils::join_namespace(pid, "mnt")?;
            std::env::set_current_dir("/")?;
            utils::exec_command(command, env_vars)?;
            println!("exec_command failed.");
        }
        ForkResult::Parent { child } => {
            if !detach {
                unsafe {
                    let pidfd = pidfd_open(child.as_raw(), 0)?;
                    wait_pidfd(pidfd)?;
                    libc::close(pidfd);
                }
            }
        }
    }

    Ok(())
}
