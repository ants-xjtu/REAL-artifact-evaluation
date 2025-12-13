use crate::common::config::{CONTAINER_PATH};
use debug_print::{debug_println};
use std::os::unix::net::UnixStream;
use std::io::{BufRead, BufReader, Write};

pub fn stop_main(container_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    let container_dir = format!("{}{}", CONTAINER_PATH, container_name);
    let sock = format!("{}/shim.sock", container_dir);
    let mut stream = UnixStream::connect(&sock)?;

    stream.write_all(b"stop\n")?;
    stream.flush()?;

    let mut reader = BufReader::new(&stream);
    let mut line = String::new();
    if reader.read_line(&mut line)? > 0 && line.trim() == "OK" {
        debug_println!("Container stop complete.");
    } else {
        eprintln!("Stop command sent, but no OK received: {}", line);
    }

    Ok(())
}