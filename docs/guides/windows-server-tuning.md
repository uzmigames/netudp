# Windows Server Tuning Guide

Performance tuning for netudp on Windows dedicated game servers.

## 1. Windows Filtering Platform (WFP)

WFP adds ~2µs per packet (~28% of sendto cost). On dedicated servers where you control the firewall, disabling WFP recovers up to 40% PPS.

### Check if WFP is active

```c
int active = netudp_windows_is_wfp_active();
// 1 = BFE running (WFP active, ~2µs/pkt overhead)
// 0 = BFE stopped (no WFP overhead)
```

### Disable WFP (admin required)

```powershell
# Stop Base Filtering Engine (disables Windows Firewall)
net stop BFE

# Prevent auto-start on boot
sc config BFE start=disabled

# Re-enable if needed
sc config BFE start=auto
net start BFE
```

**Warning:** Disabling BFE disables Windows Firewall entirely. Only do this on dedicated servers behind a hardware firewall or in a VPC with security groups.

## 2. Socket Options (applied automatically by netudp)

### UDP_SEND_MSG_SIZE (Windows 10 1703+)

Enables kernel-level UDP segmentation offload. netudp sets this automatically to `NETUDP_MTU` (1200 bytes) on socket creation.

### SIO_LOOPBACK_FAST_PATH (Windows 8+)

Bypasses the network stack for loopback traffic (127.0.0.1). Reduces localhost latency from ~7µs to ~1µs. Applied automatically by netudp.

## 3. Registered I/O (RIO)

Build with `-DNETUDP_ENABLE_RIO=ON` to use RIO polled mode. Pre-registers buffers and uses zero-syscall completion queue polling.

```bash
cmake -B build -DNETUDP_ENABLE_RIO=ON
cmake --build build --config Release
```

Expected: 4-8x PPS improvement over sendto (138K -> 500K-1M PPS).

## 4. NIC Tuning

### Receive Side Scaling (RSS)

Distribute packets across CPU cores at the NIC level:

```powershell
# Check current RSS settings
Get-NetAdapterRss

# Enable RSS with N queues (match num_io_threads)
Set-NetAdapterRss -Name "Ethernet" -NumberOfReceiveQueues 4

# Verify
Get-NetAdapterRss -Name "Ethernet"
```

### Interrupt Moderation

Reduce interrupt coalescing for lower latency:

```powershell
# Check current setting
Get-NetAdapterAdvancedProperty -Name "Ethernet" -RegistryKeyword "*InterruptModeration"

# Disable for lowest latency (increases CPU usage)
Set-NetAdapterAdvancedProperty -Name "Ethernet" -RegistryKeyword "*InterruptModeration" -RegistryValue 0
```

### Receive Buffers

Increase NIC receive buffer count to prevent drops under burst:

```powershell
Get-NetAdapterAdvancedProperty -Name "Ethernet" -RegistryKeyword "*ReceiveBuffers"
Set-NetAdapterAdvancedProperty -Name "Ethernet" -RegistryKeyword "*ReceiveBuffers" -RegistryValue 2048
```

## 5. OS Settings

### Power Plan

```powershell
# High Performance (prevents CPU frequency scaling)
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
```

### Process Priority

```powershell
# Run game server at high priority
Start-Process -FilePath "game_server.exe" -ArgumentList "--port 27015" -Priority High
```

### Disable Nagle (TCP — not UDP, but may affect some Winsock internals)

```c
int nodelay = 1;
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
```

Note: This is TCP-only and does not affect UDP. Listed here because some Winsock implementations share internal state.

## 6. Expected Results

| Configuration | PPS (estimated) | Latency p50 |
|---------------|----------------:|------------:|
| Default (sendto) | ~138K | ~7.2 µs |
| + SIO_LOOPBACK_FAST_PATH | ~150K | ~5 µs |
| + RIO Polled | ~500K-1M | ~1-2 µs |
| + RIO + WFP disabled | ~1M-2.3M | <1 µs |
| + RSS (4 queues) | scales 4x | = |
