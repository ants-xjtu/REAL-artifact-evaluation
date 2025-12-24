# Remote Access Guide (For NSDI AEC Only)

Please first download the SSH key pair provided via the AE HotCRP system and save them as `~/.ssh/ae_key` (private key) and `~/.ssh/ae_key.pub` (public key), respectively.
If you store the keys in a different location, please update the paths accordingly in the SSH configuration below.

Next, append the following entries to the end of your `~/.ssh/config` file:

```
Host real-ae-jumper
    HostName ae.wilsonxia.cn
    User real-ae
    IdentityFile ~/.ssh/ae_key

Host real-ae
    HostName 100.82.35.55
    User real-ae
    ProxyJump real-ae-jumper
    IdentityFile ~/.ssh/ae_key
```

> The `real-ae-jumper` machine serves strictly as an SSH bastion host to provide public IPv4 connectivity.
> The `real-ae` account on this machine is restricted to port forwarding only and cannot execute commands or access files.
> Password authentication is disabled, and the account password is locked. Access is possible exclusively via the provided restricted SSH key.

Finally, connect to the prepared evaluation environment by running:

```bash
ssh real-ae
```

After logging in, please navigate to the artifact directory:

```bash
cd nsdi26-artifact-evaluation
```

You may then proceed with the artifact evaluation following the instructions in the repository.