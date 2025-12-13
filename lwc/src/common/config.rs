use serde::{Deserialize, Serialize};
use serde_json::Number;

pub const BASE_PATH: &str = "/opt/lwc/";
pub const CONTAINER_PATH: &str = "/opt/lwc/containers/";

#[derive(Deserialize, Serialize, Debug)]
pub struct Config {
    pub state: Option<String>, // created, running, exitted
    pub exitcode: Option<Number>,
    pub cpuset: Option<String>,
    pub pid: Option<Number>,
    pub env: Option<Vec<String>>,
    pub volumes: Option<Vec<String>>
}

// Docker states, as reference:
// "State": {
//     "Status": "exited",
//     "Running": false,
//     "Paused": false,
//     "Restarting": false,
//     "OOMKilled": false,
//     "Dead": false,
//     "Pid": 0,
//     "ExitCode": 0,
//     "Error": "",
//     "StartedAt": "2024-09-19T06:46:44.331937742Z",
//     "FinishedAt": "2024-09-19T06:46:45.378138716Z"
// }
// "State": {
//     "Status": "running",
//     "Running": true,
//     "Paused": false,
//     "Restarting": false,
//     "OOMKilled": false,
//     "Dead": false,
//     "Pid": 724488,
//     "ExitCode": 0,
//     "Error": "",
//     "StartedAt": "2024-09-19T06:48:30.591912679Z",
//     "FinishedAt": "0001-01-01T00:00:00Z"
// }