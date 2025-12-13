use std::fs::{self};
use std::io::{self};
use std::path::Path;
use std::process::Command;
use serde_json::Value;
use nix::mount::{mount, MsFlags};

use crate::common::config::{BASE_PATH, Config};
use crate::common::utils::{flush_config};

fn setup_overlayfs(layers_dir: Vec<String>, rootfs_dir: &str) -> io::Result<String> {
    if Path::new(rootfs_dir).exists() {
        fs::remove_dir_all(rootfs_dir)?;
    }
    fs::create_dir_all(rootfs_dir)?;

    // Create work directories for overlay
    let overlay_workdir = format!("{}/overlay-work", rootfs_dir);
    let overlay_upperdir = format!("{}/overlay-upper", rootfs_dir);
    fs::create_dir_all(&overlay_workdir)?;
    fs::create_dir_all(&overlay_upperdir)?;

    let lowerdirs_str = layers_dir.join(":");
    let overlay_dir = format!("{}/rootfs", rootfs_dir);
    fs::create_dir_all(&overlay_dir)?;

    // Create overlay filesystem using OverlayFS
    Command::new("mount")
        .arg("--make-private")
        .arg("-t")
        .arg("overlay")
        .arg("overlay")
        .arg("-o")
        .arg(format!("lowerdir={},upperdir={},workdir={}", lowerdirs_str, overlay_upperdir, overlay_workdir))
        .arg(&overlay_dir)
        .status()
        .expect("Failed to mount overlayfs");

    Ok(overlay_dir)
}

fn setup_volume(volume_str: &str, rootfs_dir: &str) -> io::Result<()> {
    let str_list: Vec<&str> = volume_str.split(':').collect();
    if str_list.len() != 2 {
        println!("Invalid volume string");
        return Ok(());
    }

    let host_path = format!("{}/{}/{}", BASE_PATH, "volumes", &str_list[0]);
    let volume_dir = format!("{}/{}", rootfs_dir, &str_list[1][1..]);
    fs::create_dir_all(&host_path)?;
    fs::create_dir_all(&volume_dir)?;

    Command::new("mount")
        .arg("--bind")
        .arg(host_path)
        .arg(&volume_dir)
        .status()
        .expect("Failed to mount volume");

    Ok(())
}

fn setup_shm(rootfs_dir: &str) -> io::Result<()> {
    let host_path = "/dev/shm";
    let dev_dir = format!("{}/dev", rootfs_dir);
    fs::create_dir_all(&dev_dir)?;

    mount(
        Some("tmpfs"),
        dev_dir.as_str(),
        Some("tmpfs"),
        MsFlags::MS_NOEXEC | MsFlags::MS_NOSUID,
        None::<&str>
    )?;

    let shm_dir = format!("{}/dev/shm", rootfs_dir);
    fs::create_dir_all(&shm_dir)?;
    Command::new("mount")
        .arg("--bind")
        .arg(host_path)
        .arg(&shm_dir)
        .status()
        .expect("Failed to mount shm");

    Ok(())
}

fn extract_layer(layer_tar_path: &str, extract_to: &str) -> io::Result<()> {
    if Path::new(extract_to).exists() {
        return Ok(());
    }

    std::fs::create_dir_all(extract_to)?;
    Command::new("tar")
        .arg("-xpf")
        .arg(layer_tar_path)
        .arg("-C")
        .arg(extract_to)
        .status()
        .expect("Failed to mount overlayfs");

    Ok(())
}

fn extract_image(image_name: &str) -> io::Result<(Value, Vec<String>)> {
    let image_path = format!("{}/image/{}", BASE_PATH, image_name);
    if !Path::new(&image_path).is_dir() {
        println!("Image directory not found: {}", image_path);
        return Err(io::Error::new(io::ErrorKind::NotFound, "Image directory not found"));
    }

    let manifest_path = format!("{}/manifest.json", image_path);
    if !Path::new(&manifest_path).is_file() {
        println!("manifest.json not found in {}", image_path);
        return Err(io::Error::new(io::ErrorKind::NotFound, "manifest.json not found"));
    }

    let manifest_content = fs::read_to_string(manifest_path)?;
    let manifest: Value = serde_json::from_str(&manifest_content)?;

    let layers = manifest[0]["Layers"]
        .as_array()
        .expect("Layers field is not an array")
        .iter()
        .rev()
        .map(|layer| layer.as_str().unwrap().to_string())
        .collect::<Vec<_>>();

    let layers_tar_dir = format!("{}/layers", BASE_PATH);
    if !Path::new(&layers_tar_dir).exists() {
        fs::create_dir_all(&layers_tar_dir)?;
    }

    let mut layer_dir_list = Vec::new();
    for layer in layers {
        let layer_tar_path = format!("{}/{}", image_path, layer);
        if !Path::new(&layer_tar_path).is_file() {
            println!("Layer file not found: {}", layer_tar_path);
            continue;
        }

        let layer_name = Path::new(&layer_tar_path)
            // .parent()
            // .unwrap()
            .file_name()
            .unwrap()
            .to_str()
            .unwrap();
        let layer_extract_dir = format!("{}/{}", layers_tar_dir, layer_name);

        extract_layer(&layer_tar_path, &layer_extract_dir)?;

        layer_dir_list.push(layer_extract_dir);
    }

    Ok((manifest, layer_dir_list))
}

pub fn create_main(image_name: &str, container_name: &str, volumes: Vec<String>, cpuset: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let (manifest, layer_dir_list) = extract_image(image_name)?;

    // Setup overlayfs
    let rootfs_dir = setup_overlayfs(layer_dir_list, &format!("{}/containers/{}", BASE_PATH, container_name))?;

    // Setup volume if provided
    for volume in &volumes {
        setup_volume(&volume, &rootfs_dir)?;
    }

    setup_shm(&rootfs_dir)?;

    let image_path = format!("{}/image/{}", BASE_PATH, image_name);
    let config_file = format!("{}/{}", image_path, manifest[0]["Config"].as_str().unwrap());
    if !Path::new(&config_file).is_file() {
        println!("Config file not found: {}", config_file);
        return Err(Box::new(io::Error::new(io::ErrorKind::NotFound, "Config file not found")));
    }

    let config_content = fs::read_to_string(config_file)?;
    let image_config: Value = serde_json::from_str(&config_content)?;

    let container_config = Config {
        state: Some("created".to_string()),
        exitcode: None,
        env: Some(image_config["config"]
            .get("Env").unwrap() // Environment Variable
            .as_array().unwrap() // Decode as array
            .iter()
            .filter_map(|v| v.as_str().map(String::from))
        .collect()),
        cpuset: cpuset,
        pid: None,
        volumes: Some(volumes)
    };
    let config_str = serde_json::to_string(&container_config)?;
    let container_dir = format!("{}/containers/{}/", BASE_PATH, container_name);
    flush_config(&container_dir, config_str)?;

    Ok(())
}
