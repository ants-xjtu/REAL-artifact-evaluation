use std::path::Path;
use crate::common::config::{BASE_PATH,Config};
use nix::mount::{umount2,MntFlags};
use std::fs::{self};
use std::io::{self};
use debug_print::{debug_println};


fn umount_volume(volume_str: &str, rootfs_path: &Path) -> io::Result<()> {
    let str_list: Vec<&str> = volume_str.split(':').collect();
    if str_list.len() != 2 {
        // TODO: throw a proper Error
        println!("Invalid volume string");
        return Ok(());
    }

    let path = format!("{}/{}", rootfs_path.display(), str_list[1]);
    umount2(
        path.as_str(),
        MntFlags::MNT_FORCE
    )?;

    Ok(())
}


pub fn remove_main(container_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    // Get the container's rootfs path
    let rootfs_path = Path::new(BASE_PATH)
        .join("containers")
        .join(container_name)
        .join("rootfs");

    // Get the container path
    let container_path = Path::new(BASE_PATH)
        .join("containers")
        .join(container_name);

    let config_file = format!("{}/config.json", container_path.display());

    // Read config.json
    let config_data = fs::read_to_string(&config_file)?;
    let config: Config = serde_json::from_str(&config_data)?;

    debug_println!("Read config done.");

    // TODO: check whether init process is still alive
    for volume in config.volumes.unwrap_or(vec![]) {
        umount_volume(&volume, &rootfs_path)?;
    }

    let shm_path = format!("{}/{}", rootfs_path.display(), "/dev/shm");
    umount2(
        shm_path.as_str(),
        MntFlags::MNT_FORCE
    )?;

    let dev_path = format!("{}/{}", rootfs_path.display(), "/dev");
    umount2(
        dev_path.as_str(),
        MntFlags::MNT_FORCE
    )?;

    // Call nix::mount::umount2 to unmount the container's rootfs
    umount2(&rootfs_path, MntFlags::MNT_FORCE)?;

    // Remove the container directory
    fs::remove_dir_all(&container_path)?;

    Ok(())
}