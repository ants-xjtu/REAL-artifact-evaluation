use std::fs;
use std::path::{Path, PathBuf};
use std::io::{self, ErrorKind};
use crate::common::config::CONTAINER_PATH;

/// Copy files from the host to the container
fn copy_to_container(src: &Path, container_name: &str, dest_in_container: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // Get the container's rootfs path
    let container_rootfs = PathBuf::from(format!("{}/{}/rootfs", CONTAINER_PATH, container_name));
    
    // Determine the destination path
    let dest_path = container_rootfs.join(dest_in_container.strip_prefix("/")?);

    // Create destination directory (if it doesn't exist)
    if let Some(parent_dir) = dest_path.parent() {
        fs::create_dir_all(parent_dir)?;
    }

    // Perform file copy
    if src.is_file() {
        fs::copy(src, &dest_path)?;
        // println!("Copied file from {} to {}", src.display(), dest_path.display());
    } else if src.is_dir() {
        // Recursively copy directory
        copy_dir_recursively(src, &dest_path)?;
    } else {
        return Err(Box::new(io::Error::new(ErrorKind::NotFound, "Source path is invalid")));
    }

    Ok(())
}

/// Copy files from the container to the host
fn copy_from_container(container_name: &str, src_in_container: &Path, dest: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // Get the container's rootfs path
    let container_rootfs = PathBuf::from(format!("{}/{}/rootfs", CONTAINER_PATH, container_name));

    // Determine the source path
    let src_path = container_rootfs.join(src_in_container.strip_prefix("/")?);

    // Perform file copy
    if src_path.is_file() {
        fs::copy(&src_path, dest)?;
        println!("Copied file from {} to {}", src_path.display(), dest.display());
    } else if src_path.is_dir() {
        // Recursively copy directory
        copy_dir_recursively(&src_path, dest)?;
    } else {
        return Err(Box::new(io::Error::new(ErrorKind::NotFound, "Source path in container is invalid")));
    }

    Ok(())
}

/// Recursively copy a directory
fn copy_dir_recursively(src: &Path, dest: &Path) -> Result<(), Box<dyn std::error::Error>> {
    fs::create_dir_all(dest)?;
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let src_path = entry.path();
        let dest_path = dest.join(entry.file_name());
        if src_path.is_dir() {
            copy_dir_recursively(&src_path, &dest_path)?;
        } else {
            fs::copy(&src_path, &dest_path)?;
        }
    }
    Ok(())
}

/// Main function: perform copy based on command-line arguments
pub fn cp_main(container_name: &str, src: &str, dest: &str, to_container: bool) -> Result<(), Box<dyn std::error::Error>> {
    let src_path = Path::new(src);
    let dest_path = Path::new(dest);

    if to_container {
        // Copy from host to container
        copy_to_container(src_path, container_name, dest_path)?;
    } else {
        // Copy from container to host
        copy_from_container(container_name, src_path, dest_path)?;
    }

    Ok(())
}
