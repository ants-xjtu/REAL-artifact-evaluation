#!/bin/bash

tar_path=$1
img_name=$2

cd lwc
cargo build --release
cd ..

docker load -i $1
sudo mkdir -p /opt/lwc/{image,containers,volumes,layers}
sudo mkdir -p /opt/lwc/image/$2
sudo tar -xvpf $1 -C /opt/lwc/image/$2
./lwc/target/release/lwc create $2 test-$2
./lwc/target/release/lwc remove test-$2