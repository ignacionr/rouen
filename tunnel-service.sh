#!/bin/bash

# Create and check an SSH tunnel service on the remote server
ssh -i ~/.ssh/id_ignacio root@164.90.227.49 "cat > /etc/systemd/system/ssh-tunnel-listener.service << 'EOFINNER'
[Unit]
Description=SSH Tunnel Listener Service
After=network.target

[Service]
ExecStart=/usr/bin/nc -l -p 2222 -k -e '/bin/nc localhost 22'
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOFINNER

# Make the service executable and start it
systemctl daemon-reload
systemctl enable ssh-tunnel-listener.service
systemctl start ssh-tunnel-listener.service

# Check if the service is running
systemctl status ssh-tunnel-listener.service

# Check if the port is actually listening on all interfaces
ss -tuln | grep 2222
"
