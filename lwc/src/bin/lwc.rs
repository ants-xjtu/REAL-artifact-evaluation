use clap::{Command, Arg};
use std::process::exit;
use std::string::String;
use std::path::Path;

use lwc::subcmd::{create, exec, remove, start, stop, cp};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let matches = Command::new("lwc")
        .version("0.1.0")
        .about("Light-Weight Container")
        .subcommand(
            Command::new("create")
                .about("Create a container")
                .arg(Arg::new("image-name")
                    .required(true)
                    .index(1))
                .arg(Arg::new("container-name")
                    .required(true)
                    .index(2))
                .arg(Arg::new("volume")
                    .short('v')
                    .required(false)
                    .action(clap::ArgAction::Append))
                .arg(Arg::new("cpuset")
                    .short('c')
                    .required(false))
        )
        .subcommand(
            Command::new("cp")
                .about("Copy file from/into a container into/from host.")
                .arg(Arg::new("src-path")
                    .required(true)
                    .index(1))
                .arg(Arg::new("dest-path")
                    .required(true)
                    .index(2))
        )
        .subcommand(
            Command::new("remove")
                .about("Remove a container")
                .arg(Arg::new("container-name")
                    .required(true)
                    .index(1))
        )
        .subcommand(
            Command::new("exec")
                .about("Execute a command inside a container")
                .arg(Arg::new("detach")
                    .short('d')
                    .long("detach")
                    .required(false)
                    .action(clap::ArgAction::SetTrue))
                .arg(Arg::new("container-name")
                    .required(true)
                    .index(1))
                .arg(Arg::new("command")
                    .required(true)
                    .index(2)
                    .trailing_var_arg(true)
                    .num_args(1..))
                .arg(Arg::new("env")
                    .short('e')
                    .required(false)
                    .action(clap::ArgAction::Append))
        )
        .subcommand(
            Command::new("start")
                .about("Start a container")
                .arg(Arg::new("container-name")
                    .required(true)
                    .index(1))
                .arg(Arg::new("command")
                    .required(true)
                    .index(2)
                    .trailing_var_arg(true)
                    .num_args(1..))
        )
        .subcommand(
            Command::new("stop")
                .about("Stop a container")
                .arg(Arg::new("container-name")
                    .required(true)
                    .index(1))
        )
        .get_matches();

    match matches.subcommand() {
        Some(("create", sub_matches)) => {
            let image_name: &str = sub_matches.get_one::<String>("image-name").unwrap();
            let container_name: &str = sub_matches.get_one::<String>("container-name").unwrap();
            let volumes: Vec<_> = sub_matches.get_many::<String>("volume").unwrap_or_default().collect();
            let volumes = volumes.into_iter().map(|s| s.clone()).collect();
            let cpuset: Option<String> = sub_matches.get_one::<String>("cpuset").cloned();
            create::create_main(image_name, container_name, volumes, cpuset)
        }
        Some(("cp", sub_matches)) => {
            let source: &str = sub_matches.get_one::<String>("src-path").unwrap();
            let destination: &str = sub_matches.get_one::<String>("dest-path").unwrap();
            let to_container: bool = if source.contains(':') {
                // container -> host
                false
            } else if destination.contains(':') {
                // host -> container
                true
            } else {
                return Err("Invalid input: one of the paths must be a container path.".into());
            };

            if to_container {
                let parts: Vec<&str> = destination.splitn(2, ':').collect();
                let container_name = parts[0];
                let container_path = Path::new(parts[1]);
                cp::cp_main(container_name, &source, container_path.to_str().unwrap(), true)
            } else {
                let parts: Vec<&str> = source.splitn(2, ':').collect();
                let container_name = parts[0];
                let container_path = Path::new(parts[1]);
                cp::cp_main(container_name, container_path.to_str().unwrap(), &destination, false)
            }
        }
        Some(("remove", sub_matches)) => {
            let container_name: &str = sub_matches.get_one::<String>("container-name").unwrap();
            remove::remove_main(container_name)
        }
        Some(("exec", sub_matches)) => {
            let container_name: &str = sub_matches.get_one::<String>("container-name").unwrap();
            let command: Vec<_> = sub_matches.get_many::<String>("command").unwrap().collect();
            let command = command.into_iter().map(|s| s.clone()).collect();
            let detach = sub_matches.get_flag("detach");

            let env_cli: Vec<_> = sub_matches.get_many::<String>("env").unwrap_or_default().collect();
            let env_cli = env_cli.into_iter().map(|s| s.clone()).collect();
            exec::exec_main(container_name, command, env_cli, detach)
        }
        Some(("start", sub_matches)) => {
            let container_name: &str = sub_matches.get_one::<String>("container-name").unwrap();
            let command: Vec<_> = sub_matches.get_many::<String>("command").unwrap().collect();
            let command = command.into_iter().map(|s| s.clone()).collect();
            start::start_main(container_name, command)
        }
        Some(("stop", sub_matches)) => {
            let container_name: &str = sub_matches.get_one::<String>("container-name").unwrap();
            stop::stop_main(container_name)
        }
        _ => {
            eprintln!("No valid subcommand was used");
            exit(1);
        }
    }
}
