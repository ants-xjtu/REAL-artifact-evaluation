use nix::unistd::{ForkResult, fork};
use std::fs::{self};
use std::path::Path;
use std::process::exit;
use crate::common::utils::{wait_for_peer_ready};
use crate::common::shim::run_shim;
use crate::common::config::{CONTAINER_PATH, Config};
use debug_print::{debug_println};
use nix::unistd::{pipe, close};

pub fn start_main(container_name: &str, command: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let container_dir = format!("{}{}", CONTAINER_PATH, container_name);

    // Check whether container_dir is a valid directory
    if !Path::new(&container_dir).is_dir() {
        eprintln!("Error: {} is not a valid directory.", container_dir);
        exit(1);
    }

    let config_file = format!("{}/config.json", container_dir);

    // Read config.json
    let config_data = fs::read_to_string(&config_file)?;
    let config: Config = serde_json::from_str(&config_data)?;
    if config.state == Some("running".to_string()) {
        eprintln!("Error: container is already running.");
        exit(1);
    }

    debug_println!("Read config done.");
    let (pr, pw) = pipe()?;

    match unsafe { fork() }? {
        ForkResult::Child => {
            close(pr)?;
            run_shim(&container_name, command, config, pw)?;
        }
        ForkResult::Parent { child: _child } => {
            debug_println!("parent proc fork done, child {}.", _child);
            close(pw)?;
            wait_for_peer_ready(pr)?;
        }
    }

    Ok(())
}